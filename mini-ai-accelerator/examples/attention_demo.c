#include "attention_accel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SEQ_LEN  4
#define KV_LEN   4
#define HEAD_DIM 4
#define NUM_HEADS 2

static void print_vec(const char *label, float *v, int rows, int cols) {
    printf("%s:\n", label);
    for (int i = 0; i < rows; i++) {
        printf("  ");
        for (int j = 0; j < cols; j++) {
            printf("%7.2f ", v[i * cols + j]);
        }
        printf("\n");
    }
}

int main(void) {
    printf("=========================================================\n");
    printf("  Multi-Head Attention Acceleration Demo\n");
    printf("=========================================================\n\n");

    int seq_len = SEQ_LEN;
    int kv_len  = KV_LEN;
    int hd      = HEAD_DIM;
    int nh      = NUM_HEADS;
    int d_model = nh * hd;

    /* Generate simple input embeddings */
    float *input_emb = (float *)malloc(seq_len * d_model * sizeof(float));
    for (int i = 0; i < seq_len * d_model; i++) {
        input_emb[i] = (float)((i + 1) % 7) * 0.1f;
    }

    /* Create weight matrices (identity-ish for testing) */
    float *W_Q = (float *)calloc(d_model * d_model, sizeof(float));
    float *W_K = (float *)calloc(d_model * d_model, sizeof(float));
    float *W_V = (float *)calloc(d_model * d_model, sizeof(float));
    float *W_O = (float *)calloc(d_model * d_model, sizeof(float));

    for (int i = 0; i < d_model; i++) {
        W_Q[i * d_model + i] = 0.5f;
        W_K[i * d_model + i] = 0.5f;
        W_V[i * d_model + i] = 0.5f;
        W_O[i * d_model + i] = 0.3f;
    }

    printf("Input embeddings (%dx%d):\n", seq_len, d_model);
    print_vec("Embeddings", input_emb, seq_len, d_model);

    /* ---- Test: Single-Head Attention ---- */
    printf("\n--- Single-Head Scaled Dot-Product Attention ---\n");

    float *Q_head = (float *)calloc(seq_len * hd, sizeof(float));
    float *K_head = (float *)calloc(kv_len  * hd, sizeof(float));
    float *V_head = (float *)calloc(kv_len  * hd, sizeof(float));
    float *O_head = (float *)calloc(seq_len * hd, sizeof(float));
    float *attn_w = (float *)calloc(seq_len * kv_len, sizeof(float));

    /* Simple data */
    for (int i = 0; i < seq_len * hd; i++) Q_head[i] = (float)(i + 1) * 0.1f;
    for (int i = 0; i < kv_len  * hd; i++) K_head[i] = (float)(i + 1) * 0.05f;
    for (int i = 0; i < kv_len  * hd; i++) V_head[i] = (float)(i + 1) * 0.03f;

    attention_single_head(Q_head, K_head, V_head,
                           seq_len, kv_len, hd,
                           O_head, attn_w, MASK_NONE, NULL);

    print_vec("Attention weights", attn_w, seq_len, kv_len);
    print_vec("Output", O_head, seq_len, hd);

    /* ---- Test: Causal Mask ---- */
    printf("\n--- Single-Head Attention with Causal Mask ---\n");
    float *Q2 = (float *)calloc(seq_len * hd, sizeof(float));
    float *O2 = (float *)calloc(seq_len * hd, sizeof(float));
    float *attn2 = (float *)calloc(seq_len * kv_len, sizeof(float));

    for (int i = 0; i < seq_len * hd; i++) Q2[i] = (float)(i + 1) * 0.1f;

    attention_single_head(Q2, K_head, V_head,
                           seq_len, kv_len, hd,
                           O2, attn2, MASK_CAUSAL, NULL);

    printf("Causal attention weights (lower triangular):\n");
    attention_print_heatmap(attn2, seq_len, kv_len, 8);
    print_vec("Causal output", O2, 4, hd);

    /* ---- Test: Flash Attention ---- */
    printf("\n--- Flash Attention (Tiled) ---\n");
    float *O_flash = (float *)calloc(seq_len * hd, sizeof(float));
    flash_attention_tiled(Q_head, K_head, V_head,
                           seq_len, kv_len, hd,
                           O_flash, NULL, 2, 2);

    printf("Flash Attention output vs Standard Attention:\n");
    float max_diff = 0.0f;
    for (int i = 0; i < seq_len * hd; i++) {
        float diff = fabsf(O_head[i] - O_flash[i]);
        if (diff > max_diff) max_diff = diff;
    }
    printf("  Max absolute difference: %.6f\n", max_diff);
    if (max_diff < 0.1f) {
        printf("  Flash Attention result matches standard attention (within tolerance)\n");
    }

    /* ---- Test: KV Cache ---- */
    printf("\n--- KV Cache Operations ---\n");
    MultiHeadAttention *mha = mha_create(nh, hd, seq_len, kv_len);
    if (mha) {
        float *cache_K = (float *)malloc(seq_len * d_model * sizeof(float));
        float *cache_V = (float *)malloc(seq_len * d_model * sizeof(float));
        for (int i = 0; i < seq_len * d_model; i++) {
            cache_K[i] = (float)i * 0.01f;
            cache_V[i] = (float)i * 0.02f;
        }

        kv_cache_append(mha, cache_K, cache_V, seq_len);
        printf("  KV cache length after append: %d\n", mha->kv_cache_len);

        float *retrieved_K = (float *)calloc(seq_len * d_model, sizeof(float));
        float *retrieved_V = (float *)calloc(seq_len * d_model, sizeof(float));
        kv_cache_lookup(mha, 0, seq_len, retrieved_K, retrieved_V);

        /* Verify */
        int mismatches = 0;
        for (int i = 0; i < seq_len * d_model; i++) {
            if (fabsf(cache_K[i] - retrieved_K[i]) > 1e-6f) mismatches++;
        }
        printf("  KV cache lookup verification: %d mismatches (0=perfect)\n", mismatches);

        free(cache_K); free(cache_V);
        free(retrieved_K); free(retrieved_V);
        mha_destroy(mha);
    }

    /* ---- Test: Attention FLOPs & Memory ---- */
    printf("\n--- Attention Complexity Analysis ---\n");
    int test_lens[] = {128, 256, 512, 1024};
    printf("  %-12s %15s %15s\n", "SeqLen", "FLOPs", "Memory(MB)");
    printf("  %-12s %15s %15s\n", "------------", "---------------", "---------------");
    for (int i = 0; i < 4; i++) {
        int sl = test_lens[i];
        double flops = attention_flops_count(sl, sl, 64, 12);
        double mem = attention_memory_bytes(sl, sl, 64, 12) / (1024.0 * 1024.0);
        printf("  %-12d %15.2e %15.2f\n", sl, flops, mem);
    }

    /* ---- Tiling Plan ---- */
    printf("\n--- Hardware-Aware Tiling Planner ---\n");
    int best_m, best_n;
    attention_plan_tiling(512, 512, 64, 256 * 1024, &best_m, &best_n);
    printf("  For 512×512 attention (d=64), 256KB SRAM:\n");
    printf("    Optimal tile size: Q_block=%d, K_block=%d\n", best_m, best_n);

    printf("\n=========================================================\n");
    printf("  Demo Complete\n");
    printf("=========================================================\n");

    free(input_emb); free(W_Q); free(W_K); free(W_V); free(W_O);
    free(Q_head); free(K_head); free(V_head); free(O_head); free(attn_w);
    free(Q2); free(O2); free(attn2); free(O_flash);
    return 0;
}
