# mini-gpu-mem-model — GPU 内存模型深度解析

## Overview 概述

GPU 内存层次结构是决定程序性能的关键因素。从大容量但高延迟的全局内存 (Global Memory / HBM/GDDR) 到低延迟的共享内存 (Shared Memory) 和寄存器 (Register File)，理解各级存储的特性和访问模式是 GPU 优化的核心技能。

本模块 `mini-gpu-mem-model` 模拟了 GPU 内存子系统，重点演示内存访问合并 (coalescing)、共享内存 bank conflict 以及不同地址空间的特性。

## Theory 理论基础

### 1. GPU 内存层次结构

```
                GPU Memory Hierarchy
  ┌─────────────────────────────────────────────────┐
  │                                                 │
  │   Per Thread:                                   │
  │   ┌──────────┐                                  │
  │   │ Registers │  ~256 KB/SM, 最快, 1 cycle      │
  │   └──────────┘                                  │
  │   ┌──────────┐                                  │
  │   │  Local    │  溢出到 L1/Global, 慢            │
  │   └──────────┘                                  │
  │                                                 │
  │   Per Block / SM:                               │
  │   ┌──────────┐                                  │
  │   │  Shared   │  ~48-164 KB/SM, ~20 cycles      │
  │   │  Memory   │  32 banks, 可配 L1 大小          │
  │   └──────────┘                                  │
  │   ┌──────────┐                                  │
  │   │ L1 Cache  │  ~128 KB/SM, ~30 cycles         │
  │   └──────────┘                                  │
  │                                                 │
  │   Per Device:                                   │
  │   ┌──────────┐                                  │
  │   │ L2 Cache  │  ~4-40 MB, ~200 cycles          │
  │   └──────────┘                                  │
  │   ┌──────────┐                                  │
  │   │  Global   │  HBM2e/HBM3/GDDR6(X)            │
  │   │  Memory   │  ~80 GB (A100), ~400 cycles     │
  │   └──────────┘                                  │
  │   ┌──────────┐                                  │
  │   │ Constant  │  只读, 64 KB, 有专用cache        │
  │   └──────────┘                                  │
  │   ┌──────────┐                                  │
  │   │  Texture  │  只读, 2D空间局部性优化           │
  │   └──────────┘                                  │
  │                                                 │
  └─────────────────────────────────────────────────┘
```

### 2. 内存访问合并 (Memory Coalescing)

GPU 以 warp 为单位访问全局内存。一个 warp(32 threads) 的 32 次内存访问可以被合并成少量 DRAM 事务。

#### 合并规则 (NVIDIA)

```
规则 1: 128 字节对齐的段
  - 地址必须在 128 字节对齐的段内
  - 每个段 = 1 次 32 字节事务 (L1 cache-line) 或 128 字节 (no L1)

规则 2: 连续地址
  - thread[i] 访问 base + i * sizeof(T)
  - 完美合并: 1 次 128 字节事务

规则 3: 跨步访问可能部分合并
  - stride = 1: 完美合并 (100%)
  - stride = 2: 2 次事务 (2 × 128 字节)
  - stride = N: min(N, 32) 次事务
```

#### 合并示意图

```
Coalesced Access (stride=1):
  Threads:    0  1  2  3  4 ... 31
  Addresses:  0  4  8 12 16 ... 124
  ┌──────────────────────────────────┐
  │ 128-byte aligned segment (1 trans)│
  └──────────────────────────────────┘
  DRAM Transactions: 1

Uncoalesced Access (stride=128):
  Threads:    0         1         2    ... 31
  Addresses:  0       128       256   ... 3968
  ┌──────┐ ┌──────┐ ┌──────┐     ┌──────┐
  │Seg 0 │ │Seg 1 │ │Seg 2 │ ... │Seg31 │
  └──────┘ └──────┘ └──────┘     └──────┘
  DRAM Transactions: 32 (worst case)
```

### 3. 共享内存 Bank Conflict

共享内存被划分为 32 个 bank (每个 bank 4 字节宽)：

```
    Bank 0  Bank 1  Bank 2  ...  Bank 31
    ┌─────┐ ┌─────┐ ┌─────┐     ┌─────┐
    │ 0x00│ │ 0x04│ │ 0x08│ ... │ 0x7C│  (address offset from smem base)
    │ 0x80│ │ 0x84│ │ 0x88│ ... │ 0xFC│
    │ 0x100││ 0x104││ 0x108│... │ 0x17C│
    │ ... │ │ ... │ │ ... │     │ ... │
    └─────┘ └─────┘ └─────┘     └─────┘
    
    Bank mapping: bank = (address / 4) % 32
```

#### Bank Conflict 类型

```
No Conflict (stride=1, 每个 bank 1 次访问):
  Thread 0 -> Bank 0, Thread 1 -> Bank 1, ..., Thread 31 -> Bank 31
  1 request per bank => 1 cycle

2-way Conflict (stride=2):
  Thread 0 -> Bank 0, Thread 1 -> Bank 2, ..., Thread 16 -> Bank 0
  Bank 0 gets 2 requests => 2 cycles (serialized)

32-way Conflict (stride=32):
  All 32 threads -> Bank 0
  32 requests to same bank => 32 cycles (worst case)
```

### 4. Bank Conflict 解决方案

```
方法 1: Padding
  #define SMEM_PADDING 1
  __shared__ float smem[BLOCK_SIZE][BLOCK_SIZE + SMEM_PADDING];
  // 额外的一列改变映射，避免冲突

方法 2: Swizzling
  对地址进行异或哈希，打散 bank 映射
  bank = ((address >> 2) ^ (address >> 7)) & 31

方法 3: Array-of-Structs to Struct-of-Arrays
  // Bad (stride=3, conflict):
  struct { float x, y, z; } arr[32]; 
  // Good (stride=1, no conflict):
  float x[32], y[32], z[32];
```

### 5. PTX 内存模型

```
PTX Address Spaces:
  .reg      - 寄存器 (最快)
  .sreg     - 特殊寄存器 (threadId, blockId, etc.)
  .const    - 常量内存 (read-only, cached)
  .global   - 全局内存 (全部线程可见)
  .local    - 局部内存 (线程私有, 寄存器溢出)
  .param    - 参数内存 (kernel 参数)
  .shared   - 共享内存 (block 内线程可见)
  .tex      - 纹理内存 (read-only, 2D cached)
```

### 6. 性能公式

```
Effective Bandwidth = Bytes / Time

Bytes = coalesced_transactions * bytes_per_transaction
       + uncoalesced_transactions * bytes_per_transaction

Time = max(compute_time, memory_time)

Memory Time = (Bytes / Bandwidth) * (1 / utilization)
```

## Implementation Steps 实现步骤

### Step 1: 初始化 GPU 内存

```c
GPUMemory gpu_mem_init(uint32_t size_gb)
{
    GPUMemory m;
    m.global_mem_bytes = size_gb * 1024ULL * 1024ULL * 1024ULL;
    m.global_mem = malloc(m.global_mem_bytes);
    
    m.l1_lines = 128 * 1024 / CACHE_LINE_BYTES;
    m.l1_cache = malloc(m.l1_lines * CACHE_LINE_BYTES);
    
    m.l2_lines = L2_CACHE_KB * 1024 / CACHE_LINE_BYTES;
    m.l2_cache = malloc(m.l2_lines * CACHE_LINE_BYTES);
    
    m.bandwidth_gbps = 900.0;  // HBM2 典型值
    m.num_channels = 8;
    
    return m;
}
```

### Step 2: 检查合并性

```c
bool mem_is_coalesced(const GPUMemory *m, 
                      const uint32_t *addresses, int n_addresses)
{
    if (n_addresses <= 1) return true;
    
    uint32_t base_line = addresses[0] / CACHE_LINE_BYTES;
    
    for (int i = 1; i < n_addresses; i++) {
        uint32_t line = addresses[i] / CACHE_LINE_BYTES;
        if (line != base_line) return false;
    }
    
    // 检查是否连续
    for (int i = 1; i < n_addresses; i++) {
        if (addresses[i] != addresses[0] + (uint32_t)i * 4) {
            return false;
        }
    }
    
    return true;
}
```

### Step 3: 检测 Bank Conflict

```c
int mem_bank_conflict_count(const uint32_t *addresses, int n)
{
    int bank_access[SHARED_BANKS] = {0};
    int max_conflict = 0;
    
    for (int i = 0; i < n; i++) {
        int bank = (addresses[i] / 4) % SHARED_BANKS;
        bank_access[bank]++;
        if (bank_access[bank] > max_conflict)
            max_conflict = bank_access[bank];
    }
    
    return max_conflict;
}
```

### Step 4: SoA vs AoS 分析

```c
// Struct-of-Arrays (GPU 友好)
// Layout: x[0..N], y[0..N], z[0..N]
// Thread i reads: x[i], which is coalesced (stride=1)

// Array-of-Structs (GPU 不友好)
// Layout: {x0,y0,z0}, {x1,y1,z1}, ...
// Thread i reads: struct[i].x, stride=3 per word
// 12 bytes apart => 3 transactions
```

### Step 5: 完整演示

```c
int main() {
    GPUMemory gmem = gpu_mem_init(4);
    
    // 合并访问演示
    uint32_t coal_addrs[16];
    for (int i = 0; i < 16; i++)
        coal_addrs[i] = i * 4;
    
    printf("Coalesced: %s\n", 
           mem_is_coalesced(&gmem, coal_addrs, 16) ? "YES" : "NO");
    int trans = mem_coalesced_access(&gmem, coal_addrs, 16);
    printf("Transactions: %d\n", trans);  // 1
    
    // 非合并访问演示
    uint32_t strided_addrs[16];
    for (int i = 0; i < 16; i++)
        strided_addrs[i] = i * 128;
    
    printf("Coalesced: %s\n",
           mem_is_coalesced(&gmem, strided_addrs, 16) ? "YES" : "NO");
    trans = mem_uncoalesced_access(&gmem, strided_addrs, 16);
    printf("Transactions: %d\n", trans);  // 16
    
    // Bank conflict 分析
    uint32_t same_bank[32];
    for (int i = 0; i < 32; i++)
        same_bank[i] = i * 128;  // all map to bank 0
    printf("Max conflict: %d\n", 
           mem_bank_conflict_count(same_bank, 32));  // 32
    
    gpu_mem_free(&gmem);
    return 0;
}
```

## Expected Output 预期输出

```
===== mini-gpu-arch: Memory Coalescing Demo =====

[DEMO 1] GPU Memory Subsystem Created
GPU Memory Stats:
  Global memory:  4096 MB
  GDDR channels:  8
  Bandwidth:      900.0 GB/s
  L1 cache:       1024 lines (128 bytes/line)
  L2 cache:       32768 lines
  Shared memory:  6144 KB (32 banks)
  Transactions:    0 total

[DEMO 2] Coalesced Access: Sequential addresses
  Addresses: 0, 4, 8, 12, 16, ..., 60 (16 words)
  Is coalesced? YES
  DRAM transactions: 1
  Data transferred: 16 * 4 = 64 bytes in 1 burst

[DEMO 3] Uncoalesced Access: Strided addresses (stride=128)
  Addresses: 0, 128, 256, 384, ..., 1920 (16 words)
  Is coalesced? NO
  DRAM transactions: 16 (each word needs separate transaction)
  Data transferred: 16 * 128 = 2048 bytes

[DEMO 4] Performance Comparison: Coalesced vs Uncoalesced
  +------------------+-------------+----------------+
  | Metric           | Coalesced   | Uncoalesced    |
  +------------------+-------------+----------------+
  | Words accessed   | 16          | 16             |
  | DRAM transactions| 1           | 16             |
  | Bus utilization  | 100%        | 3%             |
  | Effective BW     | 100%        | 3%             |
  +------------------+-------------+----------------+

[DEMO 5] Shared Memory Bank Conflict Analysis
  No-conflict (stride=1): max per bank = 1
  2-way conflict (stride=2): max per bank = 2
  32-way conflict (same bank): max per bank = 32

[DEMO 6] Struct-of-Arrays (SoA) vs Array-of-Structs (AoS)
  SoA layout (good for GPU): coalesced reads
  AoS layout (bad for GPU): strided reads, uncoalesced
  Rule: Use SoA for GPU to maximize coalescing

[DEMO 7] Final Memory Stats
  ...
  Coalesced:      1 (1 trans each)
  Uncoalesced:    16 (16 trans each)
```

## Build Instructions 构建说明

```bash
cd mini-gpu-arch
make coalescing_demo
./bin/coalescing_demo
```

## Key Concepts 核心概念

1. **Coalescing Rule #1**
   一个 warp 的 32 个线程应访问连续、对齐的地址。最优情况：1 次 128 字节事务。

2. **Cache Line Size**
   L1 cache line = 128 bytes on NVIDIA. 32 threads × 4 bytes = 128 bytes. This is by design.

3. **Strided Access Penalty**
   跨步越大，事务越多。stride=1 → 1 trans, stride=2 → 2 trans, ..., stride=32 → 32 trans.

4. **Bank Conflicts**
   共享内存的 32 个 bank 对应 32 个线程。如果多个线程访问同一 bank，访问将被串行化。

5. **Padding Technique**
   在共享内存数组末尾添加 padding 可以改变 bank 映射，消除 conflict。

6. **SoA > AoS**
   对于 GPU 程序，将数据布局从 AoS 改为 SoA 是最简单也最有效的优化之一。

7. **Constant Memory**
   64 KB 常量内存 + 专用 L1 cache。适合 warp 内所有线程访问相同值的场景。

8. **Texture Memory**
   提供硬件插值、边界处理。适合图像处理应用。2D 空间局部性自动获得 cache 优化。

9. **Unified Memory**
   自 CUDA 6.0 起，GPU 和 CPU 共享同一地址空间。硬件/驱动自动迁移页面。简化编程但可能引入额外延迟。

10. **HBM vs GDDR**
    - HBM2e/HBM3: 宽总线 (1024-bit), 高带宽 (~2 TB/s), 低功耗
    - GDDR6X: 窄总线 (384-bit), 中等带宽 (~1 TB/s), 高功耗
    - 数据中心 GPU 使用 HBM，消费级使用 GDDR

## References 参考资料

- NVIDIA CUDA C Programming Guide: Chapter 5 - Performance Guidelines
- "CUDA C Best Practices Guide" - Memory Optimizations
- "Dissecting the NVIDIA GPU Memory Hierarchy" - Microbenchmarking
- Stanford CS149: Lecture 8 - GPU Memory Hierarchy and Optimization
- CMU 15-418/618: Lecture 7 - Memory Hierarchy in GPUs
- AMD GCN: "Optimizing for the AMD GPU Memory System"
