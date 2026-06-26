# Course Alignment — 课程对照表

> 本模块 `mini-gpu-arch` 参考了以下三所学校的并行计算课程内容，并进行了系统整合。

## Module-Course Mapping Table

| 模块 (Module)                | CMU 15-418                          | Stanford CS149                          | UMich EECS 570                       |
|------------------------------|-------------------------------------|-----------------------------------------|--------------------------------------|
| `simd` (SIMD 执行引擎)        | Lecture 4: SIMD & Vector            | Lecture 5: GPU Architecture             | SIMT Model                           |
| `warp` (Warp 调度器)          | Lecture 6: GPU Architecture         | Lecture 6: Scheduling & Occupancy       | Warp Scheduling & Latency Hiding     |
| `shader_core` (Shader Core)   | Lecture 6: GPU Architecture         | Lecture 5-6: GPU & CUDA                 | SIMT Core Microarchitecture          |
| `tensor_core` (Tensor Core)   | —                                    | Lecture 17: Hardware Specialization     | Domain-Specific Accelerators         |
| `memory_gpu` (GPU 内存)        | Lecture 7: GPU Memory               | Lecture 8: Memory Optimizations         | Memory Hierarchy & Coalescing        |
| `thread_sched` (Block 调度)    | Lecture 6: Thread Hierarchy         | Lecture 7: CUDA Programming Models      | Block/Grid Scheduling                |

## CMU 15-418/618 — Parallel Computer Architecture and Programming

### 课程概述
- 卡内基梅隆大学并行计算核心课程
- 涵盖从指令级并行到大规模分布式系统
- GPU 部分重点：SIMD 模型、CUDA 编程、内存层次

### 本模块对应章节

| 章  | 主题                        | 对应实现      |
|-----|-----------------------------|---------------|
| Ch4 | SIMD & Vector Architectures | `simd.h/c`    |
| Ch5 | GPU Architecture (SIMT)     | `warp.h/c`, `shader_core.h/c` |
| Ch6 | CUDA Programming Model      | `thread_sched.h/c` |
| Ch7 | GPU Memory Hierarchy        | `memory_gpu.h/c` |

### 关键概念覆盖

1. **SIMD vs SIMT**: 课程区分传统 SIMD (SSE/AVX) 和 GPU SIMT 模型。本模块实现了 SIMT warp 模型。
2. **Latency Hiding**: 通过大量线程切换隐藏内存延迟。`warp_sim_demo.c` 演示。
3. **Memory Coalescing**: 合并内存访问。`coalescing_demo.c` 演示。
4. **Occupancy**: 线程占用率计算。`occupancy_demo.c` 演示。

## Stanford CS149 — Parallel Computing

### 课程概述
- Stanford 并行计算课程（前身为 CS193c）
- 重点涵盖 GPU 架构、CUDA/OpenCL 编程、硬件加速
- 包括 Tensor Core 等现代硬件特性

### 本模块对应章节

| Lecture | 主题                         | 对应实现      |
|---------|------------------------------|---------------|
| Lec 5   | GPU Architecture & CUDA      | `shader_core.h/c` |
| Lec 6   | Scheduling & Occupancy       | `warp.h/c` |
| Lec 7   | CUDA Programming Models       | `thread_sched.h/c` |
| Lec 8   | Memory Hierarchy             | `memory_gpu.h/c` |
| Lec 17  | Hardware Specialization      | `tensor_core.h/c` |

### 额外补充

- **Warp Scheduling Policies**: Round-Robin, Greedy-then-Oldest (GTO), Two-Level
- **Coalescing Rules**: 128-byte对齐段，详细事务分析
- **Bank Conflicts**: 32-bank共享内存，padding解决方案
- **Tensor Core Internals**: 4x4x4 systolic array, HMMA指令

## UMich EECS 570 — Parallel Computer Architecture

### 课程概述
- 密歇根大学并行计算机体系结构
- 从Cache一致性到GPU架构全覆盖
- SIMT模型和GPU内存系统是其重点

### 本模块对应章节

| 主题                     | 对应实现                        |
|--------------------------|---------------------------------|
| SIMT Model (线程模型)     | `simd.h/c`, `warp.h/c`         |
| Warp Scheduling          | `warp.h/c`                     |
| Memory Hierarchy         | `memory_gpu.h/c`               |
| Coalescing & Tiling      | `coalescing_demo.c`            |
| Occupancy & Resource Limits | `thread_sched.h/c`, `occupancy_demo.c` |

---

## 知识体系对比

| 概念                    | CMU 15-418  | Stanford CS149 | UMich EECS 570 | 本模块实现    |
|------------------------|-------------|----------------|----------------|---------------|
| SIMD Lane              | ✅           | ✅              | ✅              | `SIMDLane`    |
| Warp/Wavefront         | ✅           | ✅              | ✅              | `Warp`        |
| Active Mask            | ✅           | ✅              | ✅              | `active_mask` |
| Predicated Execution   | ✅           | ✅              | —               | `simd_mask_set` |
| Divergence & Reconvergence | ✅       | ✅              | ✅              | 文档覆盖      |
| Warp Scheduler         | ✅           | ✅              | ✅              | `WarpScheduler` |
| Round-Robin            | ✅           | ✅              | ✅              | `SCHED_ROUND_ROBIN` |
| Greedy-then-Oldest     | —            | ✅              | —               | `SCHED_GREEDY` |
| Shader Core (SM)       | ✅           | ✅              | ✅              | `ShaderCore`  |
| Register File          | ✅           | ✅              | ✅              | `register_file` |
| Shared Memory          | ✅           | ✅              | ✅              | `shared_memory` |
| L1 / L2 Cache          | ✅           | ✅              | ✅              | `l1_cache`, `l2_cache` |
| Tensor Core            | —            | ✅              | —               | `TensorCore`  |
| MMA (4×4×4)            | —            | ✅              | —               | `tensor_core_mma` |
| HMMA Instruction       | —            | ✅              | —               | 文档覆盖      |
| Memory Coalescing      | ✅           | ✅              | ✅              | `mem_is_coalesced` |
| Bank Conflicts         | ✅           | ✅              | ✅              | `mem_bank_conflict_count` |
| Occupancy Calculation  | ✅           | ✅              | —               | `block_sched_calc_occupancy` |
| Grid/Block Scheduling  | ✅           | ✅              | ✅              | `BlockScheduler` |
| SoA vs AoS             | ✅           | ✅              | ✅              | `coalescing_demo.c` |
| PTX Memory Model       | —            | ✅              | —               | 文档覆盖      |

---

## 学习路径建议

```
  初学者路径:
    1. simd_demo.c       → 理解 SIMT 执行基础
    2. warp_sim_demo.c   → 理解 warp 调度和延迟隐藏
    3. coalescing_demo.c → 理解内存访问优化
    4. occupancy_demo.c  → 理解资源分配与性能
    ──────────────────────
    5. tensor_op_demo.c  → Tensor Core 高级话题

  进阶阅读:
    1. docs/simt-model.md
    2. docs/gpu-memory-coalescing.md
    3. docs/tensor-core-internals.md
```
