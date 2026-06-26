# mini-warp-scheduler — Warp 调度器深度解析

## Overview 概述

Warp Scheduler 是 GPU Streaming Multiprocessor (SM) 的核心调度单元。它的职责是在每个时钟周期从就绪的 warp 中选择一个发射指令，实现零开销线程切换 (zero-overhead thread switching)，并通过大量 warp 的并发来隐藏内存访问延迟。

本模块 `mini-warp-scheduler` 实现了三种调度策略 (Round-Robin, Greedy, Age-Based) 的模拟器，并演示了 latency hiding 的数学原理。

## Theory 理论基础

### 1. 零开销线程切换

GPU 与 CPU 在线程切换上的关键区别：

```
CPU Context Switch:
  - Save registers to stack            (~10-30 cycles)
  - Load new thread's register state   (~10-30 cycles)
  - Switch page tables / TLB flush     (~50-100 cycles)
  Total: ~100-200 cycles

GPU Warp Switch (zero-overhead):
  - Warp scheduler selects new warp    (0 cycles)
  - Register file has ALL warp states  (no save/restore needed)
  - Issue slot immediately available   (new instruction each cycle)
  Total: 0 cycles overhead
```

### 2. 延迟隐藏模型

GPU 通过大量并发 warp 隐藏内存延迟：

```
                    Memory Latency Hiding
  ┌────────────────────────────────────────────────┐
  │                                                │
  │  Warp 0: [Issue][   Memory Stall (400 cycles)  ][Issue]...
  │  Warp 1: [   Memory Stall   ][Issue][Issue]...
  │  Warp 2: [Issue][Issue][Issue][Issue][Issue]...
  │  Warp 3: [Issue][Issue][Issue][Issue][Issue]...
  │  ...                                           │
  │  Warp N: [Issue][Issue][Issue][Issue][Issue]...
  │                                                │
  │  SM Issue Slot: ALWAYS BUSY if enough warps    │
  └────────────────────────────────────────────────┘
```

### 3. 延迟隐藏公式

至少需要的 warp 数量：

```
Required_Warps = Memory_Latency / Warp_Issue_Rate

例如：
  Memory latency = 400 cycles
  Each warp can issue 1 instruction every 4 cycles (pipeline)
  Minimum warps = 400 / 4 = 100 warps

对于 NVIDIA GPU (最多 64 warps/SM)，通常需要 2-3 条独立的
长延迟指令流水来填充，再加上缓存命中率的帮助。
```

精确的 Little's Law 模型：

```
N = λ × L

N = active warps needed
λ = instruction issue rate (instructions/cycle)
L = average memory latency (cycles)

Example: λ = 1 inst/cycle, L = 200 cycles => N = 200 warps
But: λ typically 0.25-0.5 effective => 50-100 warps sufficient
```

### 4. 调度策略

#### a. Round-Robin (轮询)

```
  调度顺序: W0 -> W1 -> W2 -> ... -> Wn-1 -> W0 -> ...
  
  特点:
  - 公平性最好
  - 实现最简单
  - 平均延迟最低
  - NVIDIA 实际使用变体 (Loose Round-Robin)
```

#### b. Greedy-then-Oldest (GTO)

NVIDIA 实际使用的策略：

```
  阶段1 (Greedy): 持续发射同一 warp 直到 stall
  阶段2 (Oldest): 从 stalled warps 中选"最老"的
  
  特点:
  - 更好的 cache 局部性 (same warp = same data)
  - 减少寄存器文件端口竞争
  - AMD 使用类似策略
```

#### c. Two-Level Scheduling

```
  Level 1: 在每个 warp 内部调度（指令级并行性 ILP）
  Level 2: 在 warp 之间调度（线程级并行性 TLP）
  
  特点:
  - 两个层面独立决策
  - Level 1 处理数据依赖
  - Level 2 处理延迟隐藏
```

### 5. Stall Reason 类型

| Stall Type       | 原因                       | 典型延迟     |
|-------------------|----------------------------|-------------|
| STALL_NONE        | 无阻塞，可以发射            | 0 cycles    |
| STALL_MEMORY      | 等待内存加载完成            | 200-800     |
| STALL_SCOREBOARD  | 等待前一条指令写回寄存器     | 4-20        |
| STALL_BARRIER     | 等待 __syncthreads() 完成  | 可变         |

### 6. 性能指标

```
Warp Issue Rate = Total_Instructions / Total_Cycles
Warp Utilization = Active_Warps / Max_Warps
Stall Rate = Stalled_Cycles / Total_Cycles
IPC (Instructions Per Cycle) = Issued_Count / Total_Cycles
```

## Implementation Steps 实现步骤

### Step 1: 创建 WarpScheduler

```c
WarpScheduler warp_sched_create(int max_warps)
{
    WarpScheduler ws;
    ws.max_warps = max_warps;
    ws.num_warps = 0;
    ws.active_warp_count = 0;
    ws.current_warp = 0;
    ws.scheduling_policy = SCHED_ROUND_ROBIN;
    return ws;
}
```

### Step 2: 添加 Warp

```c
int warp_sched_add_warp(WarpScheduler *ws, Warp *w)
{
    if (ws->num_warps >= ws->max_warps) return -1;
    ws->warps[ws->num_warps] = *w;
    ws->warps[ws->num_warps].stall_cycles = 0;
    ws->active_warp_count++;
    ws->num_warps++;
    return ws->num_warps - 1;
}
```

### Step 3: Round-Robin 选择

```c
int warp_sched_select(WarpScheduler *ws)
{
    for (int i = 0; i < ws->num_warps; i++) {
        int idx = (ws->current_warp + i) % ws->num_warps;
        if (ws->warps[idx].stall_cycles == 0) {
            ws->current_warp = (idx + 1) % ws->num_warps;
            return idx;
        }
    }
    return -1;  // 所有 warp 都 stalled
}
```

### Step 4: 每周期推进

```c
void warp_sched_step(WarpScheduler *ws)
{
    // 递减所有 stall 计数器
    for (int i = 0; i < ws->num_warps; i++) {
        if (ws->warps[i].stall_cycles > 0) {
            ws->warps[i].stall_cycles--;
            ws->warps[i].stalled_cycles++;
            if (ws->warps[i].stall_cycles == 0) {
                ws->warps[i].stall_reason = STALL_NONE;
                ws->active_warp_count++;
            }
        }
        ws->warps[i].age++;
    }
    
    // 发射一条指令
    int selected = warp_sched_select(ws);
    if (selected >= 0) {
        ws->warps[selected].issued_count++;
    }
}
```

### Step 5: 延迟隐藏演示

```c
int main() {
    WarpScheduler ws = warp_sched_create(8);
    
    // 添加 8 个 warp
    for (int i = 0; i < 8; i++) {
        Warp w = {.warp_id = i, .stall_cycles = 0};
        warp_sched_add_warp(&ws, &w);
    }
    
    // 模拟 20 个周期，每 4 周期某些 warp 发生 memory stall
    for (int cycle = 0; cycle < 20; cycle++) {
        if (cycle % 4 == 0) {
            // 某些 warp 触发 memory stall (8 cycles)
            ws.warps[cycle % 8].stall_cycles = 8;
        }
        
        int selected = warp_sched_select(&ws);
        printf("Cycle %d: Issued warp %d\n", cycle, selected);
        warp_sched_step(&ws);
    }
    
    return 0;
}
```

## Expected Output 预期输出

```
===== mini-gpu-arch: Warp Scheduling Simulation Demo =====

[Setup] Creating warp scheduler with 8 warps
[Setup] Memory latency: 8 cycles
[Setup] Warps needed to hide latency: 8

[DEMO 1] Initial state - all 8 warps ready
==== Warp Scheduler (8 warps, 8 active, policy=0) ====
Warp  0 | PC=0x1000 | mask=0xFFFFFFFF | stall=0(NONE) | issued=0
Warp  1 | PC=0x1010 | mask=0xFFFFFFFF | stall=0(NONE) | issued=0
...

[DEMO 2] Scheduling Trace (20 cycles)
Cycle | Selected Warp | Warp States (R=Ready, S=Stalled)
------+---------------+------------------------------------
    0 |         0     | W0:R W1:R W2:R W3:R W4:R W5:R W6:R W7:R
    1 |         1     | W0:R W1:R W2:R W3:R W4:R W5:R W6:R W7:R
    ...
    4 |         1     | W0:S W1:R W2:R W3:R W4:S W5:R W6:R W7:R
    ...

[DEMO 3] Final Statistics
  Warp 0: issued=4 stalled=8 cycles
  Warp 1: issued=6 stalled=0 cycles
  ...

[DEMO 4] Scheduling Policy Comparison
  Policy: RoundRobin    | Selection order: W1 W2 W3 W1 W2
  Policy: Greedy        | Selection order: W1 W1 W1 W2 W3
  Policy: AgeBased      | Selection order: W1 W2 W3 W1 W2

[DEMO 5] Latency Hiding Theory
  Memory latency:          8 cycles
  Warps needed to hide:    8 (one ready each cycle)
  With 8 warps and 25% stall rate, effective throughput maintained
```

## Build Instructions 构建说明

```bash
cd mini-gpu-arch
make warp_sim_demo
./bin/warp_sim_demo
```

## Key Concepts 核心概念

1. **Zero-Overhead Thread Switching**
   GPU 在寄存器文件中保存所有 warp 的完整上下文，切换 warp 只需要改变 warp 选择器，不需要任何寄存器保存/恢复操作。

2. **Latency Hiding**
   通过并发执行大量 warp，当一个 warp 等待内存时，其他 warp 可以继续执行，从而隐藏内存延迟。这是 GPU 达到高吞吐量的关键。

3. **Occupancy vs Latency Hiding**
   - 更高的 occupancy = 更多的活跃 warp = 更好的延迟隐藏
   - 但 occupancy 不是唯一因素：ILP（指令级并行）也很重要
   - 有时低 occupancy + 高 ILP 比高 occupancy + 低 ILP 更好

4. **Scheduling Policy**
   不同的调度策略影响缓存局部性、公平性和整体吞吐量。Greedy-then-Oldest 是 NVIDIA 的实际选择。

5. **Stall Sources**
   主要 stall 原因包括内存依赖、寄存器写后读冒险、barrier 同步。理解 stall 来源是性能优化的关键。

6. **Warp Divergence Impact**
   如果 warp 内线程发生分支发散，SIMD 效率降低，相当于只有部分 lane 在干活，浪费了计算资源。

7. **Dual-Issue Scheduling**
   现代 GPU (如 NVIDIA Volta+) 支持每个周期发射两条指令（如一条 INT + 一条 FP），warp 调度器需要协调。

8. **Sub-Warp Scheduling**
   Volta 引入了独立线程调度 (Independent Thread Scheduling)，每个线程可以独立 PC，不再是严格的 SIMD。但性能在数据并行时仍然最高。

## References 参考资料

- Stanford CS149: Lecture 6 - GPU Architecture, Scheduling, and Occupancy
- CMU 15-418/618: Lecture 6 - GPU Architecture and CUDA
- NVIDIA Volta Architecture Whitepaper: Independent Thread Scheduling
- "Dissecting the NVIDIA Volta GPU Architecture via Microbenchmarking" - Jia et al.
- UMich EECS 570: Lecture on GPU Warp Scheduling
