# SIMT Model Deep Dive — SIMT 执行模型深度解析

## Introduction

SIMT (Single Instruction, Multiple Thread) 是 NVIDIA GPU 的核心执行模型。它是对传统 SIMD (Single Instruction, Multiple Data) 模型的扩展，允许程序员以标量形式编写代码，而硬件自动对多个线程应用向量化执行。

## SIMD vs SIMT 对比

### 架构层面

| 特性           | SIMD (SSE/AVX)         | SIMT (NVIDIA GPU)              |
|----------------|------------------------|--------------------------------|
| 编程模型       | 显式向量指令           | 标量代码，硬件向量化            |
| 线程管理       | 程序员管理             | 硬件自动 (warp)                |
| 分支处理       | 掩码 (mask)            | Active mask + Reconvergence    |
| 线程间通信     | shuffle/shift          | Warp shuffle, shared memory    |
| 向量宽度       | 128/256/512 bits       | 32 threads (warp)              |
| 寄存器文件     | 向量寄存器             | Per-thread 标量寄存器           |

### 执行层面

```
SIMD (AVX-512 example):
  __m512 a = _mm512_load_ps(ptr);
  __m512 b = _mm512_load_ps(ptr2);
  __m512 c = _mm512_add_ps(a, b);  // 一条指令, 16 floats

SIMT (CUDA example):
  float a = ptr[threadIdx.x];      // 标量代码
  float b = ptr2[threadIdx.x];     // 每个线程写标量
  float c = a + b;                 // 硬件自动打包成 SIMD 操作
```

## NVIDIA Warp 架构

### Warp 定义

```
Warp = 32 threads executing in lockstep

Thread Mapping (1D block):
  warp_id = threadIdx.x / 32
  lane_id = threadIdx.x % 32

Thread Mapping (2D block):
  thread_id_linear = threadIdx.y * blockDim.x + threadIdx.x
  warp_id = thread_id_linear / 32
  lane_id = thread_id_linear % 32
```

### Warp 内部结构

```
            ┌───────────────────────────────────┐
            │         Warp (32 lanes)            │
            │                                   │
            │  PC: 0x1004 (shared)              │
            │  Active Mask: 0xFFFFFFFF           │
            │                                   │
            │  ┌─────┐ ┌─────┐       ┌─────┐   │
            │  │Lane0│ │Lane1│  ...  │Lane31│   │
            │  │ RF  │ │ RF  │       │ RF  │   │
            │  └─────┘ └─────┘       └─────┘   │
            │                                   │
            │  Single Instruction Decoder       │
            │  (broadcast to all active lanes)  │
            └───────────────────────────────────┘
```

## AMD Wavefront 架构

### 关键差异

| 特性              | NVIDIA Warp          | AMD Wavefront        |
|-------------------|----------------------|----------------------|
| 线程数            | 32                   | 64 (GCN) / 32 (RDNA) |
| 执行模式          | Per-warp PC          | Per-wavefront PC     |
| 分支粒度          | 32 threads           | 64 threads           |
| 调度粒度          | 1 warp               | 1 wavefront          |
| Thread Group      | Warp                 | Wavefront            |
| Thread Block      | Cooperative Thread Array (CTA) | Work-Group    |

### RDNA 改进

AMD RDNA 架构将 wavefront 从 64 改为 32 (wave32)，更接近 NVIDIA 的 warp 模型，提供更好的 occupancy 和 divergence 处理。

## Thread Divergence 线程发散

### 问题描述

```
__global__ void divergent_kernel(int *data) {
    int tid = threadIdx.x;
    if (tid < 16) {
        data[tid] = compute_path_A(data[tid]);  // Path A
    } else {
        data[tid] = compute_path_B(data[tid]);  // Path B
    }
}
```

### 执行时间线

```
  Time ──────────────────────────────────────────►
  
  Warp execution:
  ┌──────────────┬──────────────┐
  │ Path A       │ Path B       │
  │ (lanes 0-15) │ (lanes 16-31)│
  │ active:16    │ active:16    │
  │ inactive:16  │ inactive:16  │
  └──────────────┴──────────────┘
  
  Total execution time = Time(A) + Time(B)
  (not min(Time(A), Time(B)) as in independent threads)
```

### 对性能的影响

```
SIMD Efficiency = Active_Lanes / Total_Lanes

Branch with probability p:
  E[lane_active_time] = p * T_A + (1-p) * T_B
  Total_time = T_A + T_B
  
  Efficiency = (p * T_A + (1-p) * T_B) / (T_A + T_B)
  
  Example: T_A = T_B, p = 0.5
    Efficiency = (0.5 + 0.5) / 2 = 0.5 (50%)
    
  Worst case: p = 0.5, T_A = T_B
    Only 50% of compute units are working at any time
```

## Reconvergence Mechanisms

### 1. Stack-Based Reconvergence

```
SIMT Stack (per warp):

  Push when entering branch:
    ┌─────────────┐
    │ Active Mask │  ← TOS (Top of Stack)
    │ Reconverge  │
    │ PC          │
    ├─────────────┤
    │ ...         │  ← previous entries
    └─────────────┘

  Pop when reaching reconvergence point:
    PC = reconverge_pc
    Active Mask = stored_mask
```

#### Stack 操作

```
if (cond) {
  // Push: 保存当前mask和reconverge PC
  // 执行 then 分支，mask = current & cond
  A();
  // 执行 else 分支，mask = current & !cond
} else {
  B();
}
// Pop: 恢复mask，跳到reconverge点
```

#### 局限性

- 栈深度有限 (通常 32 entries)
- 深层嵌套循环可能导致栈溢出
- 间接分支难以确定 reconverge 点

### 2. Immediate Post-Dominator (IPDOM)

现代 NVIDIA GPU 使用的方法：

```
Control Flow Graph (CFG):
      Entry
        │
        ▼
    [Branch] ────┐
        │        │
        ▼        ▼
    [Block A] [Block B]
        │        │
        └────┬───┘
             ▼
        [Block C]  ← IPDOM of Branch
             │
             ▼
           Exit

Definition: Node D is immediate post-dominator of node B
  iff every path from B to Exit goes through D,
  and D ≠ B.

Reconvergence: Hardware knows to reconverge at IPDOM(C)
  after both paths complete.
```

#### 硬件实现

```
1. 编译器标记每个分支的 IPDOM PC
2. 分支指令编码中包含 reconverge PC
3. 硬件维护 per-warp 的 reconverge table:
   - active mask for taken path
   - active mask for not-taken path
   - reconverge PC
4. 两条路径都执行完毕后，恢复 PC 到 reconverge PC
```

### 3. NVIDIA Volta - Independent Thread Scheduling

Volta 架构引入了独立线程调度：

```
Pre-Volta (Pascal and earlier):
  - Single PC per warp
  - All lanes execute same instruction
  - SIMT stack for divergence
  
Volta and later:
  - Per-thread PC (virtual)
  - Threads can diverge independently
  - __syncwarp() to resynchronize
  
  Benefits:
  - Finer-grain parallelism
  - Producer-consumer within warp
  - No reconvergence overhead for unrelated divergence
```

## Predicated Execution 谓词执行

### PTX 谓词

```
  @!P0 BRA target      ; 条件分支 (控制流)
  @P0  ADD R1, R2, R3  ; 谓词执行 (数据流)
  
  // 编译器将短分支转化为谓词:
  if (x > 0)
    y = y + 1;         // 5+ 指令 → 谓词化
  
  // 编译后:
  setp.gt.s32 P0, x, 0;
  @P0 add.s32 y, y, 1;
```

### 谓词化 vs 分支

```
  Strategy decision:
  
  Short instructions (< ~7): predicate
    - Avoid branch divergence
    - All lanes execute, but "wasted" work is small
  
  Long instructions (> ~7): branch + reconverge
    - Avoid executing long sequences in inactive lanes
    - Branch overhead is amortized
```

## Warp Shuffle Instructions

### 概述

Warp shuffle 允许同一 warp 内的线程直接交换寄存器数据，无需通过共享内存：

```
  // CUDA intrinsic:
  int __shfl_sync(unsigned mask, int var, int srcLane, int width=32);
  int __shfl_up_sync(unsigned mask, int var, unsigned delta, int width=32);
  int __shfl_down_sync(unsigned mask, int var, unsigned delta, int width=32);
  int __shfl_xor_sync(unsigned mask, int var, int laneMask, int width=32);
```

### 硬件实现

```
  Each lane reads the register file of the source lane:
  
  Lane i wants value from Lane j:
  ┌─────┐     ┌─────┐
  │ RF  │ ←── │ RF  │
  │Lane │     │Lane │
  │  i  │     │  j  │
  └─────┘     └─────┘
  
  Crossbar within the warp register file:
  ┌───┐ ┌───┐     ┌───┐
  │L0 │ │L1 │ ... │L31│
  └─┬─┘ └─┬─┘     └─┬─┘
    │     │         │
    └──┬──┴────┬────┘
       │       │
    ┌──┴───────┴──┐
    │   Crossbar  │
    └──┬───────┬──┘
       │       │
       ▼       ▼
     Output lanes
```

### 应用

```
  Reduction (warp level):
    val = __shfl_down_sync(mask, val, 16);
    val = __shfl_down_sync(mask, val, 8);
    val = __shfl_down_sync(mask, val, 4);
    val = __shfl_down_sync(mask, val, 2);
    val = __shfl_down_sync(mask, val, 1);
    // val in lane 0 now holds sum of all lanes

  Broadcast:
    leader_val = __shfl_sync(mask, val, 0);  // lane 0 broadcasts to all
```

---

## Summary

SIMT 模型的核心洞察：

1. **标量编程 + 向量执行**: 程序员写标量代码，硬件负责向量化
2. **大量线程隐藏延迟**: 通过 warp 切换隐藏内存和流水线延迟
3. **Active Mask 处理分支**: 不跳转，而是掩码执行
4. **Reconvergence 保证正确性**: 分支路径最终汇合
5. **Warp Shuffle 实现零拷贝通信**: warp 内线程直接交换数据

## References

- "NVIDIA Tesla: A Unified Graphics and Computing Architecture" (2008)
- "NVIDIA Volta: Independent Thread Scheduling" (2017)
- "Divergence Analysis and Optimizations" - Coutinho et al., PACT 2011
- AMD GCN Architecture Whitepaper
- "Dissecting the NVIDIA Volta GPU Architecture" - Jia et al., ISPASS 2018
