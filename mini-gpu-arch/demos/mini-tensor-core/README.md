# mini-tensor-core — Tensor Core 内部原理深度解析

## Overview 概述

NVIDIA Tensor Core 是 Volta 架构引入的专用矩阵运算单元，能够在单个时钟周期内完成 4×4×4 的矩阵乘法累加 (MMA) 操作。Tensor Core 为深度学习训练和推理提供了比传统 CUDA Core 高出一个数量级的计算吞吐量。

本模块 `mini-tensor-core` 模拟了 Tensor Core 的核心操作，包括 4×4 MMA、分块矩阵乘法和性能估算。

## Theory 理论基础

### 1. Tensor Core MMA 操作

Tensor Core 执行的原子操作是 D = A × B + C：

```
      4x4 Matrix A (FP16)        4x4 Matrix B (FP16)
    ┌                ┐        ┌                ┐
    │ a00 a01 a02 a03 │        │ b00 b01 b02 b03 │
    │ a10 a11 a12 a13 │   ×    │ b10 b11 b12 b13 │
    │ a20 a21 a22 a23 │        │ b20 b21 b22 b23 │
    │ a30 a31 a32 a33 │        │ b30 b31 b32 b33 │
    └                ┘        └                ┘
    
                    +
    
      4x4 Matrix C (FP32 accumulator)
    ┌                ┐
    │ c00 c01 c02 c03 │
    │ c10 c11 c12 c13 │
    │ c20 c21 c22 c23 │
    │ c30 c31 c32 c33 │
    └                ┘
    
                    =
    
      4x4 Matrix D (FP32 result)
    ┌                ┐
    │ d00 d01 d02 d03 │
    │ d10 d11 d12 d13 │
    │ d20 d21 d22 d23 │
    │ d30 d31 d32 d33 │
    └                ┘
```

### 2. HMMA 指令

NVIDIA PTX 中的 HMMA (Half-precision Matrix Multiply-Accumulate) 指令：

```
hmma.sync.aligned.m16n16k16.row.col.f16.f16.f32.f32
  d0, d1, d2, d3,    // 4 个 32-bit 寄存器，存 D 矩阵
  a0, a1, a2, a3,    // 4 个 32-bit 寄存器，存 A 矩阵 (2 个 f16/reg)
  b0, b1, b2, b3,    // 4 个 32-bit 寄存器，存 B 矩阵
  c0, c1, c2, c3;    // 4 个 32-bit 寄存器，存 C 累加器
```

### 3. 数据格式和精度

| 数据类型 | 输入  | 累加器 | 用途                    |
|----------|-------|--------|------------------------|
| FP16     | FP16  | FP32   | 通用训练/推理           |
| BF16     | BF16  | FP32   | 训练 (更宽范围)         |
| TF32     | TF32  | FP32   | A100 训练 (19-bit 尾数) |
| INT8     | INT8  | INT32  | 推理                    |
| INT4     | INT4  | INT32  | 推理 (低精度)           |
| FP64     | FP64  | FP64   | HPC (A100 支持)         |
| INT1     | INT1  | INT32  | 极限推理                 |

### 4. 4×4×4 收缩阵列 (Systolic Array)

Tensor Core 内部是一个 4×4×4 的收缩阵列：

```
         ┌───┬───┬───┬───┐
    ┌───►│PE │PE │PE │PE │───►
    │    ├───┼───┼───┼───┤
 A  │ ┌─►│PE │PE │PE │PE │───►
    │ │  ├───┼───┼───┼───┤    C (accumulate)
    │ │┌►│PE │PE │PE │PE │───►
    │ ││ ├───┼───┼───┼───┤
    │ ││││PE │PE │PE │PE │───►
    └─┼┼┼┴───┴───┴───┴───┘
      │││
      B  (broadcast vertically)
```

每个 PE (Processing Element) 执行: `C = A * B + C`

### 5. Tensor Core vs CUDA Core 吞吐量

```
A100 GPU (GA100):
  CUDA Cores: 6912 × 2 FMA/cycle = 13,824 FP32 FMA/cycle
  Tensor Cores: 432 × 1024 FMA/cycle = 442,368 FP16 FMA/cycle
  
  Speedup: 442,368 / 13,824 = 32x (for FP16 matrix ops)
  
  FP32 (CUDA):  19.5 TFLOPS
  TF32 (TC):    156 TFLOPS   (8x)
  FP16 (TC):    312 TFLOPS   (16x)
  INT8 (TC):    624 TOPS     (32x)
```

### 6. Sparse Tensor Core (2:4 稀疏性)

A100 引入的稀疏 Tensor Core：

```
Dense Matrix:          Sparse Matrix (2:4 pattern):
  [a b c d]              [a 0 0 d]  (50% zeros)
  
  Throughput: 2x (only non-zero elements participate in computation)
  Compression: 50% memory reduction
```

## Implementation Steps 实现步骤

### Step 1: 创建 TensorCore

```c
TensorCore tensor_core_create(int num_cores)
{
    TensorCore tc;
    tc.num_tensor_cores = num_cores;
    tc.throughput_fma_per_cycle = num_cores * 64;  // 每 TC 64 FMA/cycle
    tc.sparsity_enabled = false;
    return tc;
}
```

### Step 2: 4×4 MMA 实现

```c
void tensor_core_mma(const TileFragment *a, const TileFragment *b,
                     const TileFragment *c, TileFragment *d)
{
    for (int row = 0; row < TILE_SIZE; row++) {
        for (int col = 0; col < TILE_SIZE; col++) {
            float sum = c->data[row][col];
            for (int k = 0; k < TILE_SIZE; k++) {
                sum += a->data[row][k] * b->data[k][col];
            }
            d->data[row][col] = sum;
        }
    }
}
```

### Step 3: 分块矩阵乘法 (Tiled MatMul)

```c
void tensor_core_matmul_tiled(int m, int n, int k,
                              const float *a, const float *b, float *c)
{
    // 三重循环分块: 最外层遍历输出 m×n 分块
    // 内层遍历 k 维度
    for (int i = 0; i < m; i += TILE_SIZE)
        for (int j = 0; j < n; j += TILE_SIZE)
            for (int kk = 0; kk < k; kk += TILE_SIZE)
                // 在每个 4×4 块上调用 MMA
                ...
}
```

### Step 4: 性能估算

```c
int tensor_core_throughput_estimate(int m, int n, int k)
{
    int total_fmas = m * n * k;
    int fmas_per_cycle = TENSOR_CORES_PER_SM * 64;  // 8 TC × 64 FMA
    return (total_fmas + fmas_per_cycle - 1) / fmas_per_cycle;
}
```

### Step 5: 演示程序

```c
int main() {
    TensorCore tc = tensor_core_create(8);
    
    // 4×4 MMA 演示
    TileFragment A = {{{1,2,3,4},{5,6,7,8},{9,10,11,12},{13,14,15,16}}};
    TileFragment B = {{{1,1,1,1},{1,1,1,1},{1,1,1,1},{1,1,1,1}}};
    TileFragment C = {0};
    TileFragment D;
    tensor_core_mma(&A, &B, &C, &D);
    
    // 8×8 分块乘法
    float a[64], b[64], c[64] = {0};
    // ... 初始化 ...
    tensor_core_matmul_tiled(8, 8, 8, a, b, c);
    
    return 0;
}
```

## Expected Output 预期输出

```
===== mini-gpu-arch: Tensor Core Operations Demo =====

[DEMO 1] Created TensorCore: 8 cores, 512 FMA/cycle

[DEMO 2] 4x4 Matrix Multiply-Accumulate (MMA)
Matrix A (4x4):
    1.00     2.00     3.00     4.00
    5.00     6.00     7.00     8.00
    9.00    10.00    11.00    12.00
   13.00    14.00    15.00    16.00

Matrix B (4x4):
    1.00     1.00     1.00     1.00
    1.00     1.00     1.00     1.00
    1.00     1.00     1.00     1.00
    1.00     1.00     1.00     1.00

Matrix C (4x4, accumulator):
    0.10     0.10     0.10     0.10
    0.20     0.20     0.20     0.20
    0.30     0.30     0.30     0.30
    0.40     0.40     0.40     0.40

Result D = A*B + C (4x4):
   10.10    10.10    10.10    10.10
   26.20    26.20    26.20    26.20
   42.30    42.30    42.30    42.30
   58.40    58.40    58.40    58.40

[DEMO 3] 8x8 Matrix Multiply using 4x4 Tiling
Matrix A (8x8): values 1..64
Matrix B (8x8): values 1..64
Matrix C = A * B (8x8): computed result

[DEMO 4] Throughput Estimation
  Matrix Size | FMAs   | Est. Cycles (8 TC @ 64 FMA/cycle)
    4x4x4     |    64  |     1
    8x8x8     |   512  |     1
   16x16x16   |  4096  |     8
   32x32x32   | 32768  |    64
   64x64x64   | 262144 |   512

[DEMO 5] Tensor Core vs CUDA Core Throughput
  CUDA Cores:   128 FMA/cycle
  Tensor Cores: 512 FMA/cycle (4.0x speedup)
```

## Build Instructions 构建说明

```bash
cd mini-gpu-arch
make tensor_op_demo
./bin/tensor_op_demo
```

## Key Concepts 核心概念

1. **Warp-Level Matrix Multiply**
   Tensor Core 操作是 warp 级别的——一个 warp 的 32 个线程协作完成一次 MMA。每个线程持有矩阵的一部分。

2. **FP16 vs FP32 权衡**
   FP16 提供 2x 吞吐量和一半的内存占用，但精度有限。FP32 累加器确保结果精度足够。TF32 (A100) 在两者之间取得平衡。

3. **Systolic Array 设计**
   数据在 PE 之间"流动"，减少寄存器文件读写。每个 PE 只做简单的乘加操作。

4. **Sparsity 加速**
   2:4 结构化稀疏性：每 4 个值中至少 2 个为 0。Tensor Core 可以跳过零值计算，获得 2x 加速。

5. **Warp Matrix Functions (WMMA)**
   CUDA 9.0 引入的 WMMA API，允许程序员直接使用 Tensor Core，而不需要 PTX inline assembly。

6. **CuBLAS 和 CuDNN 集成**
   大多数用户通过 cuBLAS (GEMM) 和 cuDNN (卷积) 间接使用 Tensor Core，这些库已经深度优化。

7. **INT8/INT4 推理加速**
   Tensor Core 支持 INT8 (×2) 和 INT4 (×4) 计算吞吐量，对推理场景至关重要。

8. **异步拷贝 (Async Copy)**
   A100 引入了从全局内存到共享内存的异步拷贝操作，与 Tensor Core 计算重叠执行。

## References 参考资料

- NVIDIA Volta Architecture Whitepaper: Tensor Core
- NVIDIA A100 Tensor Core GPU Architecture Whitepaper (2020)
- "Modeling Deep Learning Accelerator Enabled GPUs" - NVIDIA
- CUDA C Programming Guide: Warp Matrix Functions
- Stanford CS149: Lecture on Hardware Specialization (Tensor Cores)
