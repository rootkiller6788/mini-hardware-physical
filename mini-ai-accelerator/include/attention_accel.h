#ifndef ATTENTION_ACCEL_H
#define ATTENTION_ACCEL_H

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * attention_accel.h — Multi-Head Attention Hardware Acceleration
 *
 * L7: Applications — Transformer block execution on accelerator fabric
 * L8: Advanced Topics — Flash Attention tiling, KV-cache management
 *
 * Implements the core attention computation as performed in modern
 * transformer models (Vaswani et al., NeurIPS 2017), with tiling
 * strategies for hardware efficiency (Dao et al., NeurIPS 2022).
 *
 * Stanford CS217 · MIT 6.5930 · Berkeley CS294
 * ========================================================================== */

/* ---- Configuration constants ---- */
#define MAX_SEQ_LEN         512
#define MAX_HEAD_DIM        128
#define MAX_NUM_HEADS       16
#define MAX_KV_CACHE_LEN    2048
#define FLASH_TILE_BLOCK_M  64
#define FLASH_TILE_BLOCK_N  64

/* ---- Attention mask types ---- */
typedef enum {
    MASK_NONE,          /* no masking */
    MASK_CAUSAL,        /* causal (lower-triangular) for autoregressive */
    MASK_PADDING,       /* padding mask for variable-length sequences */
    MASK_CUSTOM         /* user-provided mask */
} AttentionMaskType;

/* ---- Flash Attention tile descriptor ---- */
typedef struct {
    int block_m;           /* Q block rows */
    int block_n;           /* K block rows */
    int head_dim;          /* d_k dimension (common for Q,K,V) */
    float *Q_tile;         /* [block_m][head_dim] */
    float *K_tile;         /* [block_n][head_dim] */
    float *V_tile;         /* [block_n][head_dim] */
    float *O_tile;         /* [block_m][head_dim] — output accumulation */
    float *m_prev;         /* [block_m] running max for softmax (log-sum-exp trick) */
    float *l_prev;         /* [block_m] running sum-exp for softmax */
    float *m_curr;         /* [block_m] current tile max */
    float *l_curr;         /* [block_m] current tile sum-exp */
    float *P_tile;         /* [block_m][block_n] attention scores (scratch) */
    float scale;           /* 1/sqrt(d_k) */
} FlashAttentionTile;

/* ---- Multi-Head Attention descriptor ---- */
typedef struct {
    int num_heads;
    int head_dim;
    int seq_len;
    int kv_len;            /* may differ from seq_len for cross-attention */

    /* Weight tensors (packed): [num_heads * head_dim * head_dim] each */
    float *W_Q;
    float *W_K;
    float *W_V;
    float *W_O;            /* output projection */

    /* Bias (optional) */
    float *b_Q;
    float *b_K;
    float *b_V;
    float *b_O;

    /* KV cache for incremental decoding (L8 advanced) */
    float *kv_cache_K;     /* [MAX_KV_CACHE_LEN * num_heads * head_dim] */
    float *kv_cache_V;
    int kv_cache_len;      /* current cache length */

    /* Configuration */
    AttentionMaskType mask_type;
    float *custom_mask;    /* [seq_len * kv_len] if MASK_CUSTOM */

    double total_flops;
    double peak_utilization_ratio;
} MultiHeadAttention;

/* ---- Self-Attention computation result ---- */
typedef struct {
    float *output;         /* [seq_len * num_heads * head_dim] */
    float *attention_weights; /* [seq_len * num_heads * kv_len] — optional, large */
    int num_heads;
    int head_dim;
    int seq_len;
    int kv_len;
} AttentionResult;

/* ---- Core API: standard scaled dot-product attention ---- */

/* Compute Q*K^T / sqrt(d_k) → softmax → *V for a single head */
void attention_single_head(float *Q, float *K, float *V,
                            int seq_len, int kv_len, int head_dim,
                            float *output, float *attn_weights,
                            AttentionMaskType mask_type, float *mask);

/* Multi-head attention: split-apply-merge */
MultiHeadAttention *mha_create(int num_heads, int head_dim,
                                int seq_len, int kv_len);
void mha_destroy(MultiHeadAttention *mha);
void mha_set_weights(MultiHeadAttention *mha,
                     float *W_Q, float *W_K, float *W_V, float *W_O,
                     float *b_Q, float *b_K, float *b_V, float *b_O);
void mha_forward(MultiHeadAttention *mha, float *input_emb,
                  AttentionResult *result);

/* ---- Flash Attention (L8 advanced) ---- */
void flash_attention_tiled(float *Q, float *K, float *V,
                            int seq_len, int kv_len, int head_dim,
                            float *output, float *attn_weights,
                            int block_m, int block_n);

/* Softmax helper with numerical stability (log-sum-exp trick) */
void softmax_stable(float *x, int len);
void softmax_with_mask(float *x, int len, float *mask, float mask_value);

/* ---- KV Cache operations (L8 advanced) ---- */
void kv_cache_append(MultiHeadAttention *mha, float *new_K, float *new_V, int new_len);
void kv_cache_lookup(MultiHeadAttention *mha, int start, int len,
                      float *K_out, float *V_out);
int  kv_cache_evict_lru(MultiHeadAttention *mha, int num_to_evict);

/* ---- Attention-level operation counting ---- */
double attention_flops_count(int seq_len, int kv_len, int head_dim, int num_heads);
double attention_memory_bytes(int seq_len, int kv_len, int head_dim, int num_heads);

/* ---- Attention tiling planner (hardware-aware) ---- */
void attention_plan_tiling(int seq_len, int kv_len, int head_dim,
                            int sram_bytes, int *best_block_m, int *best_block_n);

/* ---- Utility: print attention weight heatmap (ASCII) ---- */
void attention_print_heatmap(float *attn_weights, int rows, int cols, int max_display);

#endif /* ATTENTION_ACCEL_H */
