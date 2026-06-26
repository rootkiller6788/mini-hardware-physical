#include "tensor_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    printf("===== mini-gpu-arch: Tensor Core Operations Demo =====\n\n");

    /*
     * 演示1: 创建TensorCore
     */
    TensorCore tc = tensor_core_create(TENSOR_CORES_PER_SM);
    printf("[DEMO 1] Created TensorCore: %d cores, %d FMA/cycle\n",
           tc.num_tensor_cores, tc.throughput_fma_per_cycle);
    printf("\n");

    /*
     * 演示2: 4x4 MMA: D = A * B + C
     * 模拟NVIDIA Tensor Core的HMMA指令
     */
    printf("[DEMO 2] 4x4 Matrix Multiply-Accumulate (MMA)\n");

    TileFragment A = {
        .data = {
            {1.0f, 2.0f, 3.0f, 4.0f},
            {5.0f, 6.0f, 7.0f, 8.0f},
            {9.0f, 10.0f, 11.0f, 12.0f},
            {13.0f, 14.0f, 15.0f, 16.0f}
        }
    };

    TileFragment B = {
        .data = {
            {1.0f, 1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 1.0f, 1.0f}
        }
    };

    TileFragment C = {
        .data = {
            {0.1f, 0.1f, 0.1f, 0.1f},
            {0.2f, 0.2f, 0.2f, 0.2f},
            {0.3f, 0.3f, 0.3f, 0.3f},
            {0.4f, 0.4f, 0.4f, 0.4f}
        }
    };

    TileFragment D;
    memset(&D, 0, sizeof(D));

    tensor_core_print_tile(&A, "Matrix A (4x4)");
    tensor_core_print_tile(&B, "Matrix B (4x4)");
    tensor_core_print_tile(&C, "Matrix C (4x4, accumulator)");

    tensor_core_mma(&A, &B, &C, &D);
    tensor_core_print_tile(&D, "Result D = A*B + C (4x4)");
    printf("\n");

    /*
     * 演示3: 更大矩阵乘法 (8x8 分块成 4x4 tiles)
     */
    printf("[DEMO 3] 8x8 Matrix Multiply using 4x4 Tiling\n");

    float a_8x8[64], b_8x8[64], c_8x8[64];
    /* 初始化A: 递增值 */
    for (int i = 0; i < 64; i++) {
        a_8x8[i] = (float)(i + 1);
        b_8x8[i] = (float)(i + 1);
        c_8x8[i] = 0.0f;
    }

    /* 块化矩阵乘法 */
    tensor_core_matmul_tiled(8, 8, 8, a_8x8, b_8x8, c_8x8);

    printf("Matrix A (8x8, row-major):\n");
    for (int i = 0; i < 8; i++) {
        printf("  ");
        for (int j = 0; j < 8; j++) {
            printf("%6.0f ", a_8x8[i * 8 + j]);
        }
        printf("\n");
    }

    printf("\nMatrix B (8x8, row-major):\n");
    for (int i = 0; i < 8; i++) {
        printf("  ");
        for (int j = 0; j < 8; j++) {
            printf("%6.0f ", b_8x8[i * 8 + j]);
        }
        printf("\n");
    }

    printf("\nMatrix C = A * B (8x8):\n");
    for (int i = 0; i < 8; i++) {
        printf("  ");
        for (int j = 0; j < 8; j++) {
            printf("%8.0f ", c_8x8[i * 8 + j]);
        }
        printf("\n");
    }
    printf("\n");

    /*
     * 演示4: 性能估算
     */
    printf("[DEMO 4] Throughput Estimation\n");
    int cycles_4x4 = tensor_core_throughput_estimate(4, 4, 4);
    int cycles_8x8 = tensor_core_throughput_estimate(8, 8, 8);
    int cycles_16x16 = tensor_core_throughput_estimate(16, 16, 16);
    int cycles_32x32 = tensor_core_throughput_estimate(32, 32, 32);
    int cycles_64x64 = tensor_core_throughput_estimate(64, 64, 64);

    printf("  Matrix Size | FMAs   | Est. Cycles (8 TC @ 64 FMA/cycle)\n");
    printf("  ------------+--------+-----------------------------------\n");
    printf("  4x4x4       | %5d  | %5d\n", 4*4*4, cycles_4x4);
    printf("  8x8x8       | %5d  | %5d\n", 8*8*8, cycles_8x8);
    printf("  16x16x16    | %5d  | %5d\n", 16*16*16, cycles_16x16);
    printf("  32x32x32    | %5d  | %5d\n", 32*32*32, cycles_32x32);
    printf("  64x64x64    | %5d  | %5d\n", 64*64*64, cycles_64x64);
    printf("\n");

    /*
     * 演示5: Tensor Core vs CUDA Core 对比
     */
    printf("[DEMO 5] Tensor Core vs CUDA Core Throughput\n");
    int cuda_fma_per_cycle = 128; /* 假设128 CUDA cores @ 1 FMA/cycle */
    int tc_fma_per_cycle = tc.throughput_fma_per_cycle;
    printf("  CUDA Cores:  %d FMA/cycle\n", cuda_fma_per_cycle);
    printf("  Tensor Cores: %d FMA/cycle (%.1fx speedup)\n",
           tc_fma_per_cycle, (double)tc_fma_per_cycle / cuda_fma_per_cycle);
    printf("  Note: Tensor Cores use FP16 input, FP32 accumulate\n");
    printf("  Real hardware like A100: 312 TFLOPS FP16 with Tensor Cores\n");

    printf("\n===== Demo Complete =====\n");
    return 0;
}
