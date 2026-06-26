/* ============================================================================
 * attention_accel.c — Multi-Head Attention Hardware Acceleration
 *
 * L7: Applications — Full Transformer self-attention computation
 * L8: Advanced Topics — Flash Attention tiling, KV cache management
 *
 * Key formulas:
 *   Attention(Q,K,V) = softmax(Q·K^T / sqrt(d_k)) · V          (Vaswani 2017)
 *   Flash Attention: decompose softmax along K,V tiles using
 *     online softmax with running m (max) and l (sum-exp)       (Dao 2022)
 *
 * References:
 *   - Vaswani et al. "Attention Is All You Need", NeurIPS 2017
 *   - Dao et al. "FlashAttention: Fast and Memory-Efficient Exact Attention
 *                  with IO-Awareness", NeurIPS 2022
 *   - NVIDIA "NVIDIA TensorRT-LLM Multi-Head Attention"
 *
 * Stanford CS217 · MIT 6.5930 · Berkeley CS294
 * ========================================================================== */

#include "attention_accel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ==========================================================================
 * L5: Softmax with numerical stability (log-sum-exp trick)
 *
 * Standard softmax: σ(x)_i = exp(x_i) / Σ exp(x_j)
 * Stable version: subtract max(x) before exp to prevent overflow
 *
 * The log-sum-exp trick: m = max(x), then σ(x)_i = exp(x_i - m) / Σ exp(x_j - m)
 * ========================================================================== */

void softmax_stable(float *x, int len) {
    if (!x || len <= 0) return;

    /* Find maximum value for numerical stability */
    float max_val = x[0];
    for (int i = 1; i < len; i++) {
        if (x[i] > max_val) max_val = x[i];
    }

    /* Compute exp(x_i - max) and sum */
    double sum = 0.0;
    for (int i = 0; i < len; i++) {
        x[i] = expf(x[i] - max_val);
        sum += (double)x[i];
    }

    /* Normalize */
    if (sum > 1e-15) {
        float inv_sum = (float)(1.0 / sum);
        for (int i = 0; i < len; i++) {
            x[i] *= inv_sum;
        }
    } else {
        /* Uniform distribution fallback */
        float uniform = 1.0f / (float)len;
        for (int i = 0; i < len; i++) {
            x[i] = uniform;
        }
    }
}

/* Softmax with masking: mask_value is added to positions that should be ignored
 * (typically -inf for causal/padding mask), forcing exp(-inf) ≈ 0 */
void softmax_with_mask(float *x, int len, float *mask, float mask_value) {
    if (!x || len <= 0) return;

    /* Apply mask: add mask_value to masked positions */
    float max_val = -1e30f;
    if (mask) {
        for (int i = 0; i < len; i++) {
            if (mask[i] != 0.0f) {
                x[i] += mask_value;
            }
            if (x[i] > max_val) max_val = x[i];
        }
    } else {
        for (int i = 0; i < len; i++) {
            if (x[i] > max_val) max_val = x[i];
        }
    }

    double sum = 0.0;
    for (int i = 0; i < len; i++) {
        x[i] = expf(x[i] - max_val);
        sum += (double)x[i];
    }

    if (sum > 1e-15) {
        float inv_sum = (float)(1.0 / sum);
        for (int i = 0; i < len; i++) {
            x[i] *= inv_sum;
        }
    }
}

/* ==========================================================================
 * L7: Single-Head Scaled Dot-Product Attention
 *
 * Attention(Q,K,V) = softmax(Q·K^T / sqrt(d_k)) · V
 *
 * Computational complexity: O(seq_len × kv_len × head_dim)
 * Memory complexity: O(seq_len × kv_len) for attention weights
 * ========================================================================== */

void attention_single_head(float *Q, float *K, float *V,
                            int seq_len, int kv_len, int head_dim,
                            float *output, float *attn_weights,
                            AttentionMaskType mask_type, float *mask) {
    if (!Q || !K || !V || !output) return;
    if (seq_len <= 0 || kv_len <= 0 || head_dim <= 0) return;

    float scale = 1.0f / sqrtf((float)head_dim);

    /* Step 1: Compute attention scores S = Q·K^T / sqrt(d_k) */
    /* S [seq_len × kv_len] stored in attn_weights if provided, else scratch */

    int scores_size = seq_len * kv_len;
    float *scores = attn_weights;
    int need_free_scores = 0;
    if (!scores) {
        scores = (float *)malloc(scores_size * sizeof(float));
        need_free_scores = 1;
        if (!scores) {
            fprintf(stderr, "attention_single_head: malloc failed for scores\n");
            return;
        }
    }

    /* Q [seq_len][head_dim] × K^T [head_dim][kv_len] */
    for (int i = 0; i < seq_len; i++) {
        for (int j = 0; j < kv_len; j++) {
            float dot = 0.0f;
            for (int d = 0; d < head_dim; d++) {
                dot += Q[i * head_dim + d] * K[j * head_dim + d];
            }
            scores[i * kv_len + j] = dot * scale;
        }
    }

    /* Step 2: Apply masking */
    if (mask_type == MASK_CAUSAL) {
        /* Causal mask: zero out positions where j > i (future tokens) */
        for (int i = 0; i < seq_len; i++) {
            for (int j = 0; j < kv_len; j++) {
                if (j > i) {
                    scores[i * kv_len + j] = -1e9f;
                }
            }
        }
    } else if (mask_type == MASK_CUSTOM && mask) {
        for (int i = 0; i < seq_len; i++) {
            for (int j = 0; j < kv_len; j++) {
                if (mask[i * kv_len + j] == 0.0f) {
                    scores[i * kv_len + j] = -1e9f;
                }
            }
        }
    }
    /* MASK_PADDING: assume input already handles padding */
    /* MASK_NONE: no masking */

    /* Step 3: Softmax per query row */
    for (int i = 0; i < seq_len; i++) {
        softmax_stable(scores + i * kv_len, kv_len);
    }

    /* Step 4: Weighted sum output = softmax(S) · V */
    /* output [seq_len][head_dim] */
    for (int i = 0; i < seq_len; i++) {
        for (int d = 0; d < head_dim; d++) {
            float sum = 0.0f;
            for (int j = 0; j < kv_len; j++) {
                sum += scores[i * kv_len + j] * V[j * head_dim + d];
            }
            output[i * head_dim + d] = sum;
        }
    }

    if (need_free_scores) {
        free(scores);
    }
}

/* ==========================================================================
 * L7: Multi-Head Attention — split-apply-merge pattern
 *
 * MultiHead(Q,K,V) = Concat(head_0, ..., head_{h-1}) · W_O
 *   where head_i = Attention(Q·W_Qi, K·W_Ki, V·W_Vi)
 *
 * Each head operates on d_k = d_model / num_heads dimensions.
 * ========================================================================== */

MultiHeadAttention *mha_create(int num_heads, int head_dim,
                                int seq_len, int kv_len) {
    MultiHeadAttention *mha = (MultiHeadAttention *)malloc(sizeof(MultiHeadAttention));
    if (!mha) {
        fprintf(stderr, "mha_create: malloc failed\n");
        return NULL;
    }
    memset(mha, 0, sizeof(MultiHeadAttention));
    mha->num_heads  = num_heads;
    mha->head_dim   = head_dim;
    mha->seq_len    = seq_len;
    mha->kv_len     = kv_len;
    mha->mask_type  = MASK_NONE;
    mha->total_flops = 0.0;
    mha->peak_utilization_ratio = 0.0;

    /* Allocate KV cache */
    size_t cache_bytes = (size_t)MAX_KV_CACHE_LEN * num_heads * head_dim * sizeof(float);
    mha->kv_cache_K = (float *)calloc(cache_bytes, 1);
    mha->kv_cache_V = (float *)calloc(cache_bytes, 1);
    if (!mha->kv_cache_K || !mha->kv_cache_V) {
        fprintf(stderr, "mha_create: KV cache malloc failed\n");
        free(mha->kv_cache_K);
        free(mha->kv_cache_V);
        free(mha);
        return NULL;
    }
    mha->kv_cache_len = 0;

    return mha;
}

void mha_destroy(MultiHeadAttention *mha) {
    if (!mha) return;
    free(mha->kv_cache_K);
    free(mha->kv_cache_V);
    /* Note: weight tensors are externally managed, don't free here */
    free(mha);
}

void mha_set_weights(MultiHeadAttention *mha,
                     float *W_Q, float *W_K, float *W_V, float *W_O,
                     float *b_Q, float *b_K, float *b_V, float *b_O) {
    if (!mha) return;
    mha->W_Q = W_Q;
    mha->W_K = W_K;
    mha->W_V = W_V;
    mha->W_O = W_O;
    mha->b_Q = b_Q;
    mha->b_K = b_K;
    mha->b_V = b_V;
    mha->b_O = b_O;
}

void mha_forward(MultiHeadAttention *mha, float *input_emb,
                  AttentionResult *result) {
    if (!mha || !input_emb || !result) return;
    if (!mha->W_Q || !mha->W_K || !mha->W_V || !mha->W_O) {
        fprintf(stderr, "mha_forward: weights not set\n");
        return;
    }

    int seq_len = mha->seq_len;
    int kv_len  = mha->kv_len;
    int hd      = mha->head_dim;
    int nh      = mha->num_heads;
    int d_model = nh * hd;

    /* Scratch: one head's Q, K, V, output */
    float *Q_head = (float *)malloc(seq_len * hd * sizeof(float));
    float *K_head = (float *)malloc(kv_len  * hd * sizeof(float));
    float *V_head = (float *)malloc(kv_len  * hd * sizeof(float));
    float *O_head = (float *)malloc(seq_len * hd * sizeof(float));
    if (!Q_head || !K_head || !V_head || !O_head) {
        fprintf(stderr, "mha_forward: scratch malloc failed\n");
        free(Q_head); free(K_head); free(V_head); free(O_head);
        return;
    }

    /* Final output: [seq_len * d_model] */
    if (!result->output) {
        result->output = (float *)calloc(seq_len * d_model, sizeof(float));
    }

    double total_flops = 0.0;
    /* Project to Q, K, V: input [seq_len][d_model] × W [d_model][d_model] */
    /* FLOPS: Q: 2*seq_len*d_model*d_model, K+V: 2*kv_len*d_model*d_model */
    total_flops += 6.0 * seq_len * d_model * d_model; /* approximate for all projections */

    for (int h = 0; h < nh; h++) {
        /* Extract per-head Q, K, V via slicing from projections */
        /* Note: in real HW, this is done as part of the matmul, but we model it
         * as extracting from pre-projected inputs for simplicity */
        int head_offset = h * hd;

        /* Build per-head Q from input projection */
        for (int i = 0; i < seq_len; i++) {
            for (int d = 0; d < hd; d++) {
                float q_val = 0.0f;
                for (int dm = 0; dm < d_model; dm++) {
                    q_val += input_emb[i * d_model + dm] * mha->W_Q[(head_offset + d) * d_model + dm];
                }
                if (mha->b_Q) q_val += mha->b_Q[head_offset + d];
                Q_head[i * hd + d] = q_val;
            }
        }

        for (int i = 0; i < kv_len; i++) {
            for (int d = 0; d < hd; d++) {
                float k_val = 0.0f;
                float v_val = 0.0f;
                for (int dm = 0; dm < d_model; dm++) {
                    k_val += input_emb[i * d_model + dm] * mha->W_K[(head_offset + d) * d_model + dm];
                    v_val += input_emb[i * d_model + dm] * mha->W_V[(head_offset + d) * d_model + dm];
                }
                if (mha->b_K) k_val += mha->b_K[head_offset + d];
                if (mha->b_V) v_val += mha->b_V[head_offset + d];
                K_head[i * hd + d] = k_val;
                V_head[i * hd + d] = v_val;
            }
        }

        /* Attention per head */
        attention_single_head(Q_head, K_head, V_head,
                               seq_len, kv_len, hd,
                               O_head, NULL, mha->mask_type, mha->custom_mask);

        /* FLOPS for attention: 2 * seq_len * kv_len * hd (QK^T) + 2 * seq_len * kv_len * hd (×V) */
        total_flops += 4.0 * seq_len * kv_len * hd;

        /* Merge into output: output projection later */
        for (int i = 0; i < seq_len; i++) {
            for (int d = 0; d < hd; d++) {
                result->output[i * d_model + head_offset + d] = O_head[i * hd + d];
            }
        }
    }

    /* Output projection: concat [seq_len][d_model] × W_O [d_model][d_model] */
    float *concat_out = result->output;
    float *proj_out   = (float *)calloc(seq_len * d_model, sizeof(float));
    if (proj_out) {
        for (int i = 0; i < seq_len; i++) {
            for (int j = 0; j < d_model; j++) {
                float val = 0.0f;
                for (int k = 0; k < d_model; k++) {
                    val += concat_out[i * d_model + k] * mha->W_O[j * d_model + k];
                }
                if (mha->b_O) val += mha->b_O[j];
                proj_out[i * d_model + j] = val;
            }
        }
        memcpy(result->output, proj_out, seq_len * d_model * sizeof(float));
        total_flops += 2.0 * seq_len * d_model * d_model;
        free(proj_out);
    }

    mha->total_flops = total_flops;

    free(Q_head); free(K_head); free(V_head); free(O_head);
}

/* ==========================================================================
 * L8: Flash Attention — Tiled Exact Attention with IO-Awareness
 *
 * Core idea (Dao et al., NeurIPS 2022):
 * - Split Q into blocks (outer loop)
 * - Split K,V into blocks (inner loop)
 * - Maintain running statistics: m (row max) and l (row sum-exp)
 * - Rescale previous output when new max is found
 *
 * Algorithm (per Q-block):
 *   For each K,V block:
 *     S_ij = Q_i × K_j^T
 *     m_ij = rowmax(S_ij)
 *     P_ij = exp(S_ij - m_ij)
 *     l_ij = rowsum(P_ij)
 *     m_new = max(m_old, m_ij)
 *     O_i = diag(exp(m_old - m_new)) × O_i + exp(m_ij - m_new) × P_ij × V_j
 *     l_new = exp(m_old - m_new) × l_old + exp(m_ij - m_new) × l_ij
 *     m_old = m_new; l_old = l_new
 *   O_i = diag(1/l_new) × O_i
 * ========================================================================== */

void flash_attention_tiled(float *Q, float *K, float *V,
                            int seq_len, int kv_len, int head_dim,
                            float *output, float *attn_weights,
                            int block_m, int block_n) {
    if (!Q || !K || !V || !output) return;
    if (block_m <= 0) block_m = FLASH_TILE_BLOCK_M;
    if (block_n <= 0) block_n = FLASH_TILE_BLOCK_N;

    float scale = 1.0f / sqrtf((float)head_dim);

    /* Initialize output to zero */
    memset(output, 0, seq_len * head_dim * sizeof(float));

    /* Running m (row max) and l (row sum-exp) */
    float *m = (float *)malloc(seq_len * sizeof(float));
    float *l = (float *)malloc(seq_len * sizeof(float));
    if (!m || !l) {
        fprintf(stderr, "flash_attention_tiled: malloc failed\n");
        free(m); free(l);
        return;
    }
    for (int i = 0; i < seq_len; i++) {
        m[i] = -1e30f;
        l[i] = 0.0f;
    }

    /* Scratch tile for S_ij = Q_i × K_j^T */
    int tile_S_size = block_m * block_n;
    float *S_tile  = (float *)malloc(tile_S_size * sizeof(float));
    float *P_tile  = (float *)malloc(tile_S_size * sizeof(float));
    float *PV_tile = (float *)malloc(block_m * head_dim * sizeof(float));

    if (!S_tile || !P_tile || !PV_tile) {
        fprintf(stderr, "flash_attention_tiled: tile malloc failed\n");
        free(S_tile); free(P_tile); free(PV_tile); free(m); free(l);
        return;
    }

    /* Optional attn_weights output: fill with scores (approximate, last tile only) */
    if (attn_weights) {
        memset(attn_weights, 0, seq_len * kv_len * sizeof(float));
    }

    int num_m_blocks = (seq_len + block_m - 1) / block_m;
    int num_n_blocks = (kv_len  + block_n - 1) / block_n;

    for (int mi = 0; mi < num_m_blocks; mi++) {
        int m_start = mi * block_m;
        int m_actual = (m_start + block_m <= seq_len) ? block_m : (seq_len - m_start);

        /* Per Q-block running statistics */
        float *m_i = m + m_start;
        float *l_i = l + m_start;

        for (int ni = 0; ni < num_n_blocks; ni++) {
            int n_start = ni * block_n;
            int n_actual = (n_start + block_n <= kv_len) ? block_n : (kv_len - n_start);

            /* S_ij = Q_i × K_j^T / sqrt(d_k) */
            for (int r = 0; r < m_actual; r++) {
                for (int c = 0; c < n_actual; c++) {
                    float dot = 0.0f;
                    for (int d = 0; d < head_dim; d++) {
                        dot += Q[(m_start + r) * head_dim + d]
                             * K[(n_start + c) * head_dim + d];
                    }
                    S_tile[r * block_n + c] = dot * scale;
                }
            }

            /* Row-wise max for numerical stability */
            float m_ij[FLASH_TILE_BLOCK_M];
            for (int r = 0; r < m_actual; r++) {
                float row_max = S_tile[r * block_n];
                for (int c = 1; c < n_actual; c++) {
                    float v = S_tile[r * block_n + c];
                    if (v > row_max) row_max = v;
                }
                m_ij[r] = row_max;
            }

            /* P_ij = exp(S_ij - m_ij) and l_ij = rowsum(P_ij) */
            float P_rowsum[FLASH_TILE_BLOCK_M];
            for (int r = 0; r < m_actual; r++) {
                double row_sum = 0.0;
                for (int c = 0; c < n_actual; c++) {
                    float p = expf(S_tile[r * block_n + c] - m_ij[r]);
                    P_tile[r * block_n + c] = p;
                    row_sum += (double)p;
                    if (attn_weights && ni == num_n_blocks - 1) {
                        /* Store last-tile attention weights (approximation) */
                        attn_weights[(m_start + r) * kv_len + (n_start + c)] = p;
                    }
                }
                P_rowsum[r] = (float)row_sum;
            }

            /* Update O: rescale old output and accumulate new */
            for (int r = 0; r < m_actual; r++) {
                float m_old = m_i[r];
                float m_new = (m_ij[r] > m_old) ? m_ij[r] : m_old;
                float exp_diff_old = expf(m_old - m_new);
                float exp_diff_new = expf(m_ij[r] - m_new);

                float l_new = exp_diff_old * l_i[r] + exp_diff_new * P_rowsum[r];

                /* O_i = exp(m_old - m_new) × O_i + exp(m_ij - m_new) × Σ(P_ij × V_j) */
                for (int d = 0; d < head_dim; d++) {
                    /* Compute PV tile: Σ_c P_ij[r][c] × V_j[c][d] */
                    float pv_sum = 0.0f;
                    for (int c = 0; c < n_actual; c++) {
                        pv_sum += P_tile[r * block_n + c]
                                * V[(n_start + c) * head_dim + d];
                    }
                    /* Rescale old + add new */
                    output[(m_start + r) * head_dim + d] =
                        exp_diff_old * output[(m_start + r) * head_dim + d]
                        + exp_diff_new * pv_sum;
                }

                m_i[r] = m_new;
                l_i[r] = l_new;
            }
        }
    }

    /* Final normalization: O = diag(1/l) × O */
    for (int r = 0; r < seq_len; r++) {
        if (l[r] > 1e-15f) {
            float inv_l = 1.0f / l[r];
            for (int d = 0; d < head_dim; d++) {
                output[r * head_dim + d] *= inv_l;
            }
        }
    }

    free(S_tile); free(P_tile); free(PV_tile); free(m); free(l);
}

/* ==========================================================================
 * L8: KV Cache Operations for Incremental Decoding
 *
 * During autoregressive decoding, we cache K and V from previous tokens
 * to avoid recomputing them. This is critical for LLM inference.
 *
 * Cache eviction policy: LRU (Least Recently Used) based on position.
 * For constrained cache size, evict oldest tokens first.
 * ========================================================================== */

void kv_cache_append(MultiHeadAttention *mha, float *new_K, float *new_V, int new_len) {
    if (!mha || !new_K || !new_V || new_len <= 0) return;

    int nh = mha->num_heads;
    int hd = mha->head_dim;
    int old_len = mha->kv_cache_len;
    int total   = old_len + new_len;

    /* Evict if needed */
    if (total > MAX_KV_CACHE_LEN) {
        int to_evict = total - MAX_KV_CACHE_LEN;
        kv_cache_evict_lru(mha, to_evict);
        old_len = mha->kv_cache_len;
    }

    int per_token_size = nh * hd;
    int offset = old_len * per_token_size;

    memcpy(mha->kv_cache_K + offset, new_K, new_len * per_token_size * sizeof(float));
    memcpy(mha->kv_cache_V + offset, new_V, new_len * per_token_size * sizeof(float));
    mha->kv_cache_len = old_len + new_len;
}

void kv_cache_lookup(MultiHeadAttention *mha, int start, int len,
                      float *K_out, float *V_out) {
    if (!mha || !K_out || !V_out) return;
    if (start < 0 || len <= 0) return;
    if (start + len > mha->kv_cache_len) {
        len = mha->kv_cache_len - start;
        if (len <= 0) return;
    }

    int per_token_size = mha->num_heads * mha->head_dim;
    int offset = start * per_token_size;
    int copy_bytes = len * per_token_size * (int)sizeof(float);

    memcpy(K_out, mha->kv_cache_K + offset, copy_bytes);
    memcpy(V_out, mha->kv_cache_V + offset, copy_bytes);
}

int kv_cache_evict_lru(MultiHeadAttention *mha, int num_to_evict) {
    if (!mha || num_to_evict <= 0) return 0;
    if (num_to_evict > mha->kv_cache_len) {
        num_to_evict = mha->kv_cache_len;
    }

    int keep_len = mha->kv_cache_len - num_to_evict;
    int per_token_size = mha->num_heads * mha->head_dim;
    int shift_bytes = keep_len * per_token_size * (int)sizeof(float);
    int evict_bytes = num_to_evict * per_token_size * (int)sizeof(float);

    /* Shift remaining cache to beginning (evict oldest) */
    memmove(mha->kv_cache_K, mha->kv_cache_K + evict_bytes / (int)sizeof(float), shift_bytes);
    memmove(mha->kv_cache_V, mha->kv_cache_V + evict_bytes / (int)sizeof(float), shift_bytes);
    mha->kv_cache_len = keep_len;

    return num_to_evict;
}

/* ==========================================================================
 * L7: Attention Complexity Analysis
 *
 * FLOPS for Multi-Head Attention:
 *   QK^T: 2 × seq_len × kv_len × d_k × num_heads = 2 × seq_len × kv_len × d_model
 *   Softmax: ~5 × seq_len × kv_len (approximate)
 *   ×V: 2 × seq_len × kv_len × d_k × num_heads = 2 × seq_len × kv_len × d_model
 *   Total attention: ~4 × seq_len × kv_len × d_model
 *
 * Memory (FP32):
 *   QK^T matrix: seq_len × kv_len × 4 bytes
 *   Total: dominated by attention matrix when seq_len is large
 * ========================================================================== */

double attention_flops_count(int seq_len, int kv_len, int head_dim, int num_heads) {
    double d_model = (double)(head_dim * num_heads);
    /* QK^T: 2*d_model*seq_len*kv_len, ×V: 2*d_model*seq_len*kv_len, softmax: ~5*seq_len*kv_len */
    double flops  = 4.0 * d_model * seq_len * kv_len;
    flops        += 5.0 * seq_len * kv_len; /* softmax */
    return flops;
}

double attention_memory_bytes(int seq_len, int kv_len, int head_dim, int num_heads) {
    double d_model = (double)(head_dim * num_heads);
    /* Q, K, V: 3 × seq_len × d_model × 4 bytes each */
    double mem  = 3.0 * seq_len * d_model * 4.0;
    /* Attention matrix: seq_len × kv_len × 4 bytes */
    mem        += (double)seq_len * kv_len * 4.0;
    /* Output: seq_len × d_model × 4 bytes */
    mem        += seq_len * d_model * 4.0;
    return mem;
}

/* ==========================================================================
 * L8: Hardware-Aware Tiling Planner
 *
 * Given SRAM capacity, determine optimal tile sizes for Flash Attention.
 *
 * SRAM requirement per tile (approximate):
 *   Q_tile: block_m × head_dim × 4 bytes
 *   K_tile: block_n × head_dim × 4 bytes
 *   V_tile: block_n × head_dim × 4 bytes
 *   S_tile: block_m × block_n × 4 bytes (scores)
 *   O_tile: block_m × head_dim × 4 bytes
 *   Statistics: O(small)
 *   Total ≈ (2·block_m·head_dim + 2·block_n·head_dim + block_m·block_n) × 4
 *
 * We pick block sizes that fit in SRAM while minimizing DRAM accesses.
 * ========================================================================== */

void attention_plan_tiling(int seq_len, int kv_len, int head_dim,
                            int sram_bytes, int *best_block_m, int *best_block_n) {
    *best_block_m = 64;
    *best_block_n = 64;

    /* Try all power-of-2 block sizes to find best fit */
    int candidates[] = {16, 32, 64, 128, 256};
    int num_candidates = 5;
    int best_m = 16, best_n = 16;
    double best_efficiency = 0.0;

    for (int mi = 0; mi < num_candidates; mi++) {
        int bm = candidates[mi];
        if (bm > seq_len) bm = seq_len;
        if (bm < 16) continue;

        for (int ni = 0; ni < num_candidates; ni++) {
            int bn = candidates[ni];
            if (bn > kv_len) bn = kv_len;
            if (bn < 16) continue;

            /* SRAM usage estimation */
            int sram_used = (2 * bm * head_dim + 2 * bn * head_dim + bm * bn) * 4;

            if (sram_used <= sram_bytes) {
                /* Prefer larger blocks for better reuse */
                double efficiency = (double)(bm * bn) / (double)(bm + bn);
                if (efficiency > best_efficiency) {
                    best_efficiency = efficiency;
                    best_m = bm;
                    best_n = bn;
                }
            }
        }
    }

    *best_block_m = best_m;
    *best_block_n = best_n;
}

/* ==========================================================================
 * Utility: ASCII Heatmap for Attention Weights Visualization
 * ========================================================================== */

void attention_print_heatmap(float *attn_weights, int rows, int cols, int max_display) {
    if (!attn_weights) return;
    int display_r = rows < max_display ? rows : max_display;
    int display_c = cols < max_display ? cols : max_display;
    /* Unicode shading characters: ·░▒▓█ */
    static const char *shades[] = {" ", "░", "▒", "▓", "█"};

    printf("Attention Weights (%dx%d, showing %dx%d):\n", rows, cols, display_r, display_c);
    printf("  ");
    for (int j = 0; j < display_c; j++) {
        printf("%2d ", j);
    }
    printf("\n");

    for (int i = 0; i < display_r; i++) {
        printf("%2d ", i);
        for (int j = 0; j < display_c; j++) {
            float val = attn_weights[i * cols + j];
            int shade = (int)(val * 4.0f);
            if (shade < 0) shade = 0;
            if (shade > 4) shade = 4;
            printf(" %s ", shades[shade]);
        }
        printf("\n");
    }
}
