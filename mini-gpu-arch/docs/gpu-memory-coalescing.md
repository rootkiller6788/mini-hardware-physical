# GPU Memory Coalescing — GPU 内存访问合并

## Introduction

GPU 内存访问合并 (Memory Coalescing) 是将一个 warp 内多个线程的独立内存请求合并为少量 DRAM 事务的技术。合并访问是 GPU 性能优化的最基本也是最重要的手段之一。

## Coalescing Mechanics

### 基本原则

一个 warp (32 threads) 同时执行 load/store 指令时，内存控制器尝试将它们的地址合并：

```
  Warp-wide memory request (32 addresses):
    addr[0], addr[1], addr[2], ..., addr[31]
    
  Memory Controller:
    1. 确定每个地址属于哪个 128B 对齐段
    2. 属于同一段的地址合并为 1 次事务
    3. 每个事务读取/写入 128 字节 (或 32 字节 with L1 cache)
    
  最佳情况: 所有 32 个地址在同一段 → 1 次事务
  最差情况: 每个地址在不同段 → 32 次事务
```

### 事务大小

| GPU 架构    | L1 Cache 策略          | 事务大小     | 最大事务数/warp |
|-------------|------------------------|-------------|-----------------|
| Fermi       | L1 + Shared (可配置)    | 128B        | 32              |
| Kepler      | L1 + Shared (可配置)    | 128B / 32B  | 32 / 128        |
| Maxwell     | Unified L1/Texture     | 32B         | 128             |
| Pascal      | Unified L1/Shared      | 32B         | 128             |
| Volta+      | Unified L1/Shared      | 32B         | 128             |

- 使用 `-Xptxas -dlcm=ca` 启用 L1 缓存读取，事务 128B
- 使用 `-Xptxas -dlcm=cg` 禁用 L1 缓存，事务 32B (更少浪费)

### 合并规则 (C语言)

```
假设 32 threads / warp, 数据类型 T (sizeof(T) = k bytes)

Perfect Coalescing (stride=1):
  T *base;  // base aligned to 128 bytes
  Thread i accesses: base[i]
  Addresses: base, base+k, base+2k, ..., base+31k
  Condition: 32*k ≤ 128
  Transactions: 1
  
  Example: T = float (4B), k=4
    32*4 = 128 ⇒ 1 transaction of 128B ✓
    
  Example: T = double (8B), k=8
    32*8 = 256 ⇒ 2 transactions of 128B each

Strided Access (stride=s):
  Thread i accesses: base[i * s]  (s > 1)
  Transactions: min(32, ceil(32 * k * s / 128))
  
  Example: s=2, T=float (4B)
    Thread 0: base[0], Thread 1: base[2], ..., Thread 31: base[62]
    Total bytes accessed: 32 * 2 * 4 = 256
    Transactions: ceil(256/128) = 2
  
  Example: s=32, T=float (4B)
    Thread 0: base[0], Thread 1: base[32], ...
    Each thread accesses a different 128B segment
    Transactions: 32

Random / AoS: (worst case)
  Thread i accesses: base[permutation[i]]
  Transactions: 32 (full uncoalesced)
```

## 地址对齐要求

### 硬件对齐约束

```
  Base address alignment requirements:
  
  Data Type  | Size | Required Alignment
  -----------+------+-------------------
  char       | 1B   | 1B  (any)
  short      | 2B   | 2B
  int/float  | 4B   | 4B
  double     | 8B   | 8B
  float4     | 16B  | 16B (128-bit vectors)
  double2    | 16B  | 16B
  
  Using vector types (float4, int4) improves coalescing:
    - 4 elements per thread × 32 threads = 128 elements
    - With float: 128 * 4B = 512B → 4 transactions of 128B
    - With float4: 32 * 16B = 512B → 4 transactions of 128B
    But: float4 can use 128-bit loads, reducing instruction count
```

### 实际对齐检查

```
  __align__(16) float data[N];  // 16-byte aligned
  
  // CUDA malloc 默认对齐 256 bytes
  float *d_data;
  cudaMalloc(&d_data, N * sizeof(float));
  // d_data is 256-byte aligned
```

## Stride Access Patterns

### 常见 Pattern 与事务数

```
  +--------------------+------------------+----------------------+
  | Access Pattern     | Code             | Transactions (float) |
  +--------------------+------------------+----------------------+
  | Sequential (1)     | data[i]          | 1                    |
  | Sequential (2)     | data[i*2]        | 2                    |
  | Sequential (4)     | data[i*4]        | 4                    |
  | Column of M×N      | data[i*M]        | min(32, N)           |
  | Diagonal           | data[i*(M+1)]    | min(32, M+1)         |
  | Random             | data[rand[i]]    | 32                   |
  | Gather (indirect)  | data[idx[i]]     | 32 (worst case)      |
  +--------------------+------------------+----------------------+
```

## SoA vs AoS — 内存布局优化

### 数据结构对比

```
  // Array of Structs (AoS) - BAD for GPU
  typedef struct {
    float x, y, z;  // 12 bytes
    int id;          // 4 bytes
  } Particle_AoS;    // 16 bytes total
  
  Particle_AoS particles[N];
  
  // Thread i reads: particles[i].x
  // Layout: [x0 y0 z0 id0] [x1 y1 z1 id1] [x2 y2 z2 id2] ...
  // Thread 0: byte 0 → segment 0
  // Thread 1: byte 16 → segment 0 (ok first 8 threads)
  // Thread 8: byte 128 → segment 1
  // Thread 16: byte 256 → segment 2
  // Thread 24: byte 384 → segment 3
  // Result: 4 transactions of 128B each
  // (due to 16B per element, 8 elements per 128B segment)
```

```
  // Struct of Arrays (SoA) - GOOD for GPU
  typedef struct {
    float *x, *y, *z;
    int *id;
  } Particles_SoA;
  
  Particles_SoA p;
  p.x = malloc(N * sizeof(float));
  p.y = malloc(N * sizeof(float));
  p.z = malloc(N * sizeof(float));
  p.id = malloc(N * sizeof(int));
  
  // Thread i reads: p.x[i]
  // Layout: [x0 x1 x2 ... x31 x32 ...]
  // All 32 x-values in one 128B segment
  // Result: 1 transaction of 128B
```

### AoS 到 SoA 转换

```
  原始 AoS 代码:
    __global__ void kernel(Particle *particles) {
        int i = blockIdx.x * blockDim.x + threadIdx.x;
        float x = particles[i].x;  // stride=16B
        float y = particles[i].y;  // stride=16B
        float z = particles[i].z;  // stride=16B
    }
  
  优化 SoA 代码:
    __global__ void kernel(float *x, float *y, float *z) {
        int i = blockIdx.x * blockDim.x + threadIdx.x;
        float vx = x[i];  // stride=4B, coalesced!
        float vy = y[i];  // stride=4B, coalesced!
        float vz = z[i];  // stride=4B, coalesced!
    }
```

### 性能数据

```
  Kernel: Simple particle update (reading x, y, z)
  
  AoS version:
    Transactions: 12 (3 reads × 4 transactions each)
    Bandwidth utilization: 25%
    Time: ~3x slower
  
  SoA version:
    Transactions: 3 (3 reads × 1 transaction each)
    Bandwidth utilization: 100%
    Time: baseline
```

## Shared Memory Bank Conflicts

### Bank 结构

```
  32 banks, 4 bytes wide
  Address mapping: bank = (word_address) % 32
  
  ┌───────┬───────┬───────┬─────┬───────┐
  │ Bank0 │ Bank1 │ Bank2 │ ... │Bank31 │
  │ 4B    │ 4B    │ 4B    │     │ 4B    │
  └───────┴───────┴───────┴─────┴───────┘
  
  If N threads access the same bank → serialized into N requests
  If each thread accesses a different bank → all in parallel (1 cycle)
```

### 常见冲突 Pattern

```
  // 无冲突: stride=1, 每个thread不同bank
  data[threadIdx.x] = ...;  // bank = tid % 32 → all different
  
  // 2-way 冲突: stride=2
  data[threadIdx.x * 2] = ...;  
  // T0→B0, T1→B2, ..., T16→B0, T17→B2
  // Bank 0: threads 0 and 16 → 2-way conflict
  
  // 32-way 冲突: stride=32 (worst case)
  data[threadIdx.x * 32] = ...;
  // All 32 threads → Bank 0 → 32-way conflict
  
  // 同一地址: broadcast (无冲突 - 硬件优化)
  data[0] = ...;  // 所有线程读同一地址 → broadcast
```

### 解决方案

```
  // 1. Padding (添加空列)
  #define PAD 1
  __shared__ float smem[BLOCK_DIM][BLOCK_DIM + PAD];
  // 索引: smem[threadIdx.y][threadIdx.x]
  // 添加 PAD=1 列改变了 bank mapping
  
  // 2. 列访问转行访问
  // 原始 (列访问, stride=BLOCK_DIM, 冲突):
  float val = smem[threadIdx.x][blockIdx.x];  // 列方向
  // 优化: 转置后按行读取
  
  // 3. 使用更小的数据类型
  // 32-bit 访问 → 32 banks
  // 64-bit 访问 → 也可映射到 32 banks (2个连续bank)
```

## 内存访问优化策略

### 检查清单

```
  1. □ 确认数据对齐 (base address alignment)
  2. □ 使用 SoA 而非 AoS
  3. □ 使用向量类型 (float4, int4) 减少指令数
  4. □ 检查 stride 模式，最小化事务数
  5. □ 使用共享内存缓存非合并的全局内存访问
  6. □ 避免共享内存 bank conflicts (padding, 转置)
  7. □ 使用 profiling 工具验证 (ncu, nvprof)
```

### Profiler 指导

```
  NVIDIA Nsight Compute (ncu) 指标:
  
  l1tex__t_sectors_pipe_lsu_mem_global_op_ld.sum  → 总读取事务
  l1tex__t_sectors_pipe_lsu_mem_global_op_st.sum  → 总写入事务
  smsp__warps_launched.sum                        → 启动的 warp 数
  
  Sector = 32 bytes on modern GPUs
  
  Ideal: sectors per request → 32 / 32 = 1 (fully coalesced)
  Poor:  sectors per request → > 4
  
  Analysis:
    If sectors/warp >> 4, check for:
    - Strided access
    - AoS layout
    - Misaligned base address
    - Indirect/gather access
```

---

## Summary

内存合并是 GPU 性能的基础：

1. **合并访问 = 1次事务** vs **非合并 = 32次事务**
2. **SoA 布局**天然合并，**AoS 布局**天然跨步
3. **共享内存 bank conflict** 将并行访问串行化
4. **向量类型 (float4)** 减少 loads/stores 数量
5. **Profile, Profile, Profile** — 用 ncu/nvprof 验证假设

记住：GPU 的瓶颈几乎总是内存，而不是计算！

## References

- "CUDA C Best Practices Guide" - Memory Optimizations, NVIDIA
- "Understanding Coalescing in CUDA" - Mark Harris, NVIDIA
- "Programming Massively Parallel Processors" - Kirk & Hwu, Chapter 5
- Stanford CS149: Lecture 8 - GPU Memory Hierarchy
- "Dissecting the NVIDIA GPU Memory Hierarchy" - Microbenchmarking Study
