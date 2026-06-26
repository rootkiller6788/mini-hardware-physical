# Tensor Core Internals — Tensor Core 微架构

## Introduction

NVIDIA Tensor Core 是 Volta 架构 (2017) 引入的专用矩阵运算加速器，为深度学习计算提供了数量级的吞吐提升。每个 Tensor Core 每周期可完成 64 次浮点乘加 (FMA) 操作，远超 CUDA Core 的 2 FMA/cycle。

## Microarchitecture

### 每 SM 的 Tensor Core 配置

| GPU 架构   | Tensor Cores/SM | Total Tensor Cores | Peak FP16 TFLOPS |
|------------|-----------------|---------------------|-------------------|
| Volta (V100)  | 8               | 640                 | 125               |
| Turing (T4)   | 8               | 320                 | 65                |
| Ampere (A100) | 4 (3rd gen)     | 432                 | 312               |
| Hopper (H100) | 4 (4th gen)     | 528                 | 989 (FP8)         |

### 内部结构

```
  ┌─────────────────────────────────────────────────────┐
  │                  Tensor Core                        │
  │                                                     │
  │  A Matrix (4×4 FP16)    B Matrix (4×4 FP16)        │
  │  ┌───┬───┬───┬───┐     ┌───┬───┬───┬───┐           │
  │  │a00│a01│a02│a03│     │b00│b01│b02│b03│           │
  │  ├───┼───┼───┼───┤     ├───┼───┼───┼───┤           │
  │  │a10│a11│a12│a13│     │b10│b11│b12│b13│           │
  │  ├───┼───┼───┼───┤     ├───┼───┼───┼───┤           │
  │  │a20│a21│a22│a23│     │b20│b21│b22│b23│           │
  │  ├───┼───┼───┼───┤     ├───┼───┼───┼───┤           │
  │  │a30│a31│a32│a33│     │b30│b31│b32│b33│           │
  │  └───┴───┴───┴───┘     └───┴───┴───┴───┘           │
  │         │                      │                    │
  │         └──────┬───────────────┘                    │
  │                │                                    │
  │     ┌──────────▼──────────┐                         │
  │     │  4×4×4 Systolic     │                         │
  │     │      Array          │                         │
  │     │  ┌───┬───┬───┬───┐  │                         │
  │     │  │PE │PE │PE │PE │  │                         │
  │     │  ├───┼───┼───┼───┤  │                         │
  │     │  │PE │PE │PE │PE │  │                         │
  │     │  ├───┼───┼───┼───┤  │                         │
  │     │  │PE │PE │PE │PE │  │                         │
  │     │  ├───┼───┼───┼───┤  │                         │
  │     │  │PE │PE │PE │PE │  │                         │
  │     │  └───┴───┴───┴───┘  │                         │
  │     └──────────┬──────────┘                         │
  │                │                                    │
  │     ┌──────────▼──────────┐                         │
  │     │ C Matrix (4×4 FP32) │                         │
  │     │  (Accumulator)      │                         │
  │     └──────────┬──────────┘                         │
  │                │                                    │
  │     ┌──────────▼──────────┐                         │
  │     │ D Matrix (4×4 FP32) │                         │
  │     │  (Result = A×B + C) │                         │
  │     └─────────────────────┘                         │
  └─────────────────────────────────────────────────────┘
```

### Processing Element (PE) 细节

```
  每个 PE 执行: c_ij += a_ik × b_kj

  ┌─────────────┐
  │    a_ik     │──────┐
  └─────────────┘      │
                       ▼
                 ┌───────────┐
  ┌──────────┐   │ Multiplier │───► product
  │   b_kj   │──►│  (×)       │
  └──────────┘   └───────────┘      │
                                    ▼
                              ┌───────────┐
  ┌──────────┐               │  Adder    │───► new_c
  │  c_ij    │──────────────►│  (+)      │
  └──────────┘               └───────────┘
  
  每个周期:
    1. 读取 a_ik, b_kj, c_ij
    2. 计算 product = a_ik × b_kj
    3. 累加 new_c = product + c_ij
    4. 沿 systolic array 传递 a 向右, b 向下
```

## HMMA 指令详解

### PTX 指令格式

```
  hmma.sync.aligned.m16n16k16.row.col.f16.f16.f32.f32
    d0, d1, d2, d3,   // D 矩阵 (4 个 32-bit regs)
    a0, a1, a2, a3,   // A 矩阵 (4 个 32-bit regs, 每个含 2 个 f16)
    b0, b1, b2, b3,   // B 矩阵 (4 个 32-bit regs, 每个含 2 个 f16)
    c0, c1, c2, c3;   // C 矩阵 (4 个 32-bit regs, FP32 accumulator)
  
  字段含义:
    m16n16k16: M=16, N=16, K=16 (16×16×16 MMA)
    row: A 矩阵按行存储
    col: B 矩阵按列存储
    f16.f16: A 和 B 的数据类型 (FP16)
    f32.f32: C 和 D 的数据类型 (FP32 累加器)
```

### SASS 汇编级

```
  HMMA.16816.F32:
    - 1 条指令 = 16 个 FP16 × 16 个 FP16 + FP32 累加
    - 相当于 16×16 = 256 次乘法 + 256 次加法
    - 512 FLOPS/instruction/warp
    
  HMMA.884: (Volta 格式, m8n8k4)
    - 1 条指令 = 8×8×4 = 256 FLOPS
    - 8 个 Tensor Cores/SM 同时执行
    - 256 × 8 = 2048 操作/SM/cycle
```

### Warp 内数据分布

```
  32 threads 协作完成一个 16×16×16 MMA:
  
  Thread 0  holds: A[0:2][*], B[0:2][*], C[0:2][0:2]
  Thread 1  holds: A[2:4][*], B[2:4][*], C[2:4][2:4]
  ...
  Thread 31 holds: A[14:16][*], B[14:16][*], C[14:16][14:16]
  
  每个线程:
    持有 A 矩阵的 2 行 (×16 = 32 个 f16 值)
    持有 B 矩阵的 2 列 (×16 = 32 个 f16 值)
    持有 C/D 矩阵的 2×2 块 (4 个 f32 值)
  
  总共: 32 threads × 4 fragments = 128 fragments
  每个 fragment = 2×2 = 4 个 FP32 值
  总计: 512 个 FP32 累加器
```

## Data Formats 数据格式

### 精度对比

```
  Format  | Bits   | Sign | Exponent | Mantissa | Range          | Precision
  --------+--------+------+----------+----------+----------------+----------
  FP64    | 64     | 1    | 11       | 52       | ±10^±308       | ~15-16 decimal
  FP32    | 32     | 1    | 8        | 23       | ±3.4×10^38     | ~7 decimal
  TF32    | 19     | 1    | 8        | 10       | ±3.4×10^38     | ~3 decimal
  BF16    | 16     | 1    | 8        | 7        | ±3.4×10^38     | ~2 decimal
  FP16    | 16     | 1    | 5        | 10       | ±65504         | ~3 decimal
  INT8    | 8      | —    | —        | —        | —              | —
  INT4    | 4      | —    | —        | —        | —              | —
  
  Bit Layout Visualized:
  
  FP32:  [S][EEEEEEEE][MMMMMMMMMMMMMMMMMMMMMMM]
          1b    8b             23b
  
  TF32:  [S][EEEEEEEE][MMMMMMMMMM]──┬─┬─┬─┬─┬─┐
          1b    8b        10b         (truncated from FP32)
  
  BF16:  [S][EEEEEEEE][MMMMMMM]
          1b    8b         7b
  
  FP16:  [S][EEEEE][MMMMMMMMMM]
          1b   5b       10b
```

### 各精度模式吞吐量 (A100)

```
  CUDA Core Operations:
    FP64:   9.7 TFLOPS
    FP32:   19.5 TFLOPS
    FP16:   78 TFLOPS (packed)
    
  Tensor Core Operations:
    TF32:   156 TFLOPS  (8x over FP32 CUDA)
    FP16:   312 TFLOPS  (16x)
    BF16:   312 TFLOPS  (16x)
    INT8:   624 TOPS    (32x)
    INT4:   1248 TOPS   (64x)
    INT1:   2496 TOPS   (128x)
```

## Sparse Tensor Cores (2:4 稀疏性)

### 2:4 结构化稀疏

A100 引入了稀疏 Tensor Core，利用 2:4 结构化稀疏性：

```
  Original matrix row (4 values, 8 bytes each FP16):
    [v0  v1  v2  v3]
  
  After pruning (2:4 sparsity):
    [v0  0   v2  0 ]  (at least 2 of 4 must be zero)
  
  Compressed storage:
    Store only non-zero values: [v0, v2]
    Metadata bitmap: 1010 (1: valid, 0: zero)
  
  Throughput: 2x over dense Tensor Core
  Memory: 50% of dense storage
```

### 稀疏矩阵乘法

```
  Dense MMA (4×4):
    All 16 elements of A and B participate → 64 FMA
    
  Sparse MMA (4×4, 2:4 pattern):
    Only 8 non-zero elements in A (50% density)
    → 32 FMA (50% of computation)
    → 2x speedup
```

## Warp-Level Matrix Operations

### WMMA API (CUDA)

```
  #include <cuda_fp16.h>
  #include <mma.h>
  
  using namespace nvcuda;
  
  // 声明 fragments
  wmma::fragment<wmma::matrix_a, 16, 16, 16, half, wmma::row_major> a_frag;
  wmma::fragment<wmma::matrix_b, 16, 16, 16, half, wmma::col_major> b_frag;
  wmma::fragment<wmma::accumulator, 16, 16, 16, float> c_frag;
  
  // 加载到 fragment
  wmma::load_matrix_sync(a_frag, d_A, lda);
  wmma::load_matrix_sync(b_frag, d_B, ldb);
  wmma::load_matrix_sync(c_frag, d_C, ldc, wmma::mem_row_major);
  
  // MMA 操作
  wmma::mma_sync(c_frag, a_frag, b_frag, c_frag);
  
  // 存回
  wmma::store_matrix_sync(d_D, c_frag, ldd, wmma::mem_row_major);
```

### 分块策略

```
  大矩阵乘法的分块 (Block Tiling):
  
  M × K × N:
    Global memory: [M × K] × [K × N]
    ↓ (tile into shared memory)
    Shared memory tiles: [Tm × Tk] × [Tk × Tn]
    ↓ (load as fragments)
    Warp fragments: [16 × 16] × [16 × 16]
    ↓ (MMA instruction)
    Accumulator: [16 × 16] FP32
    
  Typical tiling:
    Tm = 128, Tn = 128, Tk = 16
    Thread block: 256 threads = 8 warps
    Each warp: 4 × 2 sub-tiles of 16×16
```

## Performance Comparison

### 理论峰值对比

```
  Operation: 1024×1024×1024 matrix multiply (GEMM)
  
  CUDA Core (FP32):
    FLOPs: 2 × 1024^3 = 2.15 GFLOP
    Peak: 19.5 TFLOPS (A100)
    Time: ~110 μs (theoretical min)
    
  Tensor Core (FP16):
    FLOPs: 2 × 1024^3 = 2.15 GFLOP (same math)
    Peak: 312 TFLOPS (A100)
    Time: ~6.9 μs (theoretical min)
    
  Speedup: 312/19.5 = 16x (peak)
  Practical speedup: ~10-12x (due to memory bandwidth, launch overhead)
```

### 实测数据 (cuBLAS GEMM)

```
  Matrix Size  | cuBLAS FP32 | cuBLAS FP16 (TC) | Speedup
  -------------+-------------+-------------------+--------
  1024×1024    | 10.2 TFLOPS | 85.3 TFLOPS       | 8.4x
  2048×2048    | 14.8 TFLOPS | 178.6 TFLOPS      | 12.1x
  4096×4096    | 17.2 TFLOPS | 256.4 TFLOPS      | 14.9x
  8192×8192    | 18.6 TFLOPS | 289.1 TFLOPS      | 15.5x
  16384×16384  | 19.0 TFLOPS | 302.3 TFLOPS      | 15.9x
  
  (Measurements on A100-80GB, CUDA 11.8, cuBLAS 11.11)
```

---

## Summary

Tensor Core 是当下 GPU 深度学习性能的核心：

1. **4×4×4 Systolic Array**: 每个 TC 每周期 64 次 FMA
2. **8 TC/SM (Volta/Turing)**: 512 FMA/cycle/SM
3. **FP16/BF16/TF32**: 不同精度需求的不同格式
4. **Sparsity**: 2:4 结构化稀疏 → 2x 加速
5. **WMMA API**: C++ 级别的友好 API

关键洞察：Tensor Core 不只是一个"矩阵乘法更快"的单元；它从根本上改变了 GPU 上数值计算的范式——从标量/向量到矩阵级别的批量操作。

## References

- "NVIDIA Volta Architecture Whitepaper" (2017)
- "NVIDIA A100 Tensor Core GPU Architecture" (2020)
- "NVIDIA H100 Tensor Core GPU Architecture" (2022)
- "CUDA C Programming Guide: Warp Matrix Functions"
- "Accelerating AI with Tensor Cores" - NVIDIA Developer Blog
- "Modeling Deep Learning Accelerator Enabled GPUs" - Jia et al., ISPASS 2019
