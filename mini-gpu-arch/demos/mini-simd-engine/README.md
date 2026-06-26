# mini-simd-engine — SIMD 执行引擎深度解析

## Overview 概述

SIMD (Single Instruction, Multiple Data) 是 GPU 架构的核心执行模型。本模块 `mini-simd-engine` 模拟了 SIMD 单元的内部结构，包括向量 lanes、谓词执行 (predicated execution)、散集操作 (scatter/gather) 以及分歧处理 (divergence handling)。

在 GPU 中，一个 warp (NVIDIA, 32 threads) 或 wavefront (AMD, 64 threads) 就是一个 SIMD 单元。所有线程执行相同的指令流，但可以操作不同的数据。当发生分支时，不同线程走不同路径，导致 divergence。

## Theory 理论基础

### 1. SIMD 执行模型

SIMD 的基本思想是用一条指令操作多个数据元素。在 GPU 中，这表现为：

- **向量 lane**：每个 lane 对应一个线程，有独立的寄存器文件
- **PC（程序计数器）**：warp 级别共享一个 PC，所有 lane 执行同一指令
- **Active Mask**：位掩码标记当前活跃的 lane，用于谓词执行

```
   SIMD Unit (Warp = 32 lanes)
  ┌─────────────────────────────────┐
  │ PC: 0x1004                      │
  │ Active Mask: 0xFFFFFFFF (all on)│
  ├─────────────────────────────────┤
  │ Lane 0: R0 R1 R2 ... R15        │
  │ Lane 1: R0 R1 R2 ... R15        │
  │ ...                             │
  │ Lane 31: R0 R1 R2 ... R15       │
  └─────────────────────────────────┘
```

### 2. 谓词执行 (Predicated Execution)

对于条件分支，GPU 不直接跳转，而是通过修改 active mask 来实现：

```
if (threadId < 16) {
    A();  // lane 0-15 active, lane 16-31 inactive
} else {
    B();  // lane 16-31 active, lane 0-15 inactive
}
```

执行 A() 时，mask = 0x0000FFFF；执行 B() 时，mask = 0xFFFF0000。两条路径串行执行，然后汇合 (reconverge)。

### 3. Control Flow Graph 处理

```
          Entry
            |
         [Branch] condition = threadId < 16
          /     \
     True Path  False Path
     (lanes     (lanes
      0-15)      16-31)
          \     /
         [Reconverge]
            |
          Exit
```

### 4. 散集操作 (Scatter/Gather)

- **Gather**: 从非连续内存地址读取数据到向量寄存器
- **Scatter**: 将向量寄存器写入非连续内存地址

```
Gather: VLD with per-lane addresses
  Lane 0: addr[0] = base + indices[0] * stride
  Lane 1: addr[1] = base + indices[1] * stride
  ...
  
Scatter: VST with per-lane addresses
  mem[base + indices[i] * stride] = lane[i]
```

### 5. 分歧处理策略

#### a. Stack-based Reconvergence (栈式重汇合)

使用一个栈来管理 divergent 分支：

```
Push(current_mask, reconverge_pc)  // 进入分支
  Execute taken path with new_mask
  Execute not-taken path with ~new_mask
Pop(reconverge_pc)                  // 回到汇合点
```

- **优点**：硬件简单
- **缺点**：深层嵌套消耗栈空间

#### b. Immediate Post-Dominator Reconvergence (IPDOM)

现代 NVIDIA GPU 使用 IPDOM 策略：

```
1. 编译器分析控制流图，找到每个分支的 immediate post-dominator
2. 硬件记录每个 warp 应该在哪个 PC 汇合
3. 所有活跃路径执行完毕后，PC 跳到 IPDOM 点
```

- **优点**：不需要栈，支持任意嵌套
- **缺点**：需要编译器配合

### 6. 公式

SIMD 效率 (SIMD Efficiency)：

```
η_simd = (active_lanes / total_lanes) * 100%

对于完全一致的 warp: η_simd = 100%
对于完全发散的 warp: η_simd_min = 1/32 = 3.125% (worst case)
```

平均 SIMD 效率：

```
E[η_simd] = Σ(prob_branch_i * efficiency_i)
```

## Implementation Steps 实现步骤

### Step 1: SIMD 单元初始化

```c
SIMDUnit simd_unit_create(int num_lanes)
{
    SIMDUnit u;
    u.num_lanes = num_lanes;
    u.active_mask = (1U << num_lanes) - 1;  // 所有 lane 活跃
    u.warp_pc = 0;
    
    for (int i = 0; i < num_lanes; i++) {
        u.lanes[i].lane_id = i;
        for (int j = 0; j < VEC_REG_COUNT; j++) {
            u.lanes[i].reg[j] = 0;
        }
    }
    return u;
}
```

### Step 2: 设置 Active Mask

```c
void simd_mask_set(SIMDUnit *u, bool *mask)
{
    u->active_mask = 0;
    for (int i = 0; i < u->num_lanes; i++) {
        if (mask[i]) {
            u->active_mask |= (1U << i);
        }
    }
}
```

### Step 3: 向量执行

```c
void simd_execute(SIMDUnit *u, SIMDOp op, uint32_t *a, uint32_t *b, uint32_t *result)
{
    for (int i = 0; i < u->num_lanes; i++) {
        if (!(u->active_mask & (1U << i))) continue;  // 跳过非活跃 lane
        
        switch (op) {
        case OP_VADD:  result[i] = a[i] + b[i];      break;
        case OP_VMUL:  result[i] = a[i] * b[i];      break;
        case OP_VFMA:  result[i] = a[i] * b[i] + result[i]; break;
        case OP_VCOMP: result[i] = (a[i] > b[i]) ? 1 : 0;   break;
        }
    }
}
```

### Step 4: 完整 Demo 流程

```c
int main() {
    SIMDUnit unit = simd_unit_create(16);
    
    // 向量加法
    uint32_t a[16], b[16], result[16];
    for (int i = 0; i < 16; i++) { a[i]=i; b[i]=i*2; }
    simd_execute(&unit, OP_VADD, a, b, result);
    
    // 谓词执行：仅 lane 0-7 活跃
    bool mask[16] = {1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0};
    simd_mask_set(&unit, mask);
    simd_execute(&unit, OP_VADD, a, b, result);
    
    return 0;
}
```

## Expected Output 预期输出

```
===== mini-gpu-arch: SIMD Execution Demo =====

[DEMO 1] Created SIMD unit with 16 lanes

[DEMO 2] Vector ADD (VADD)
  Lane  0:   0 +   0 =   0
  Lane  1:   1 +   2 =   3
  ...
  Lane 15:  15 +  30 =  45

[DEMO 3] Vector MUL (VMUL)
  Lane  0:   1 *   1 =   1
  Lane  1:   2 *   2 =   4
  ...

[DEMO 4] Vector FMA - Fused Multiply-Add
  Lane  0: 2 * 3 + 0 = 6
  Lane  1: 2 * 3 + 10 = 16
  ...

[DEMO 5] Predicated Execution (Mask: lanes 0-7 only)
  Active mask: 0x00FF
  Lane  0 [ON ]: 100 + 0 = 100
  Lane  7 [ON ]: 100 + 7 = 107
  Lane  8 [OFF]: 100 + 8 = 0    <-- 未执行

[DEMO 6] Vector Compare (VCOMP: a > b ?)
  Lane  0: 0 > 8 ? 0
  Lane  9: 9 > 8 ? 1
  ...

[DEMO 7] SIMD Unit Register Dump
  Lane 0: R0=0 R1=0 R2=0 R3=0
  ...
```

## Build Instructions 构建说明

```bash
cd mini-gpu-arch
make simd_demo
./bin/simd_demo
```

## Key Concepts 核心概念

1. **SIMD vs SIMT**
   - SIMD: 程序员显式管理向量操作
   - SIMT: 程序员写标量代码，硬件自动向量化

2. **Warp/Wavefront**
   - NVIDIA: 32 threads = 1 warp
   - AMD: 64 threads = 1 wavefront (in GCN/RDNA, 32/64)

3. **Active Mask 位掩码**
   - 32-bit 掩码对应 32 个 lane
   - 1 = active (执行), 0 = inactive (跳过)
   - 硬件根据分支条件更新 mask

4. **Divergence 发散**
   - 当 warp 内不同线程走不同分支时发生
   - 严重降低 SIMD 效率
   - 尽量减少 warp 内分支 (data-parallel patterns)

5. **Predication 谓词化**
   - 将短分支转化为谓词执行的指令
   - `@P0 ADD R1, R2, R3` — 仅在 P0=true 时执行
   - 避免分支跳转开销

6. **Reconvergence 重汇合**
   - Stack-based: 简单但有限制
   - IPDOM: 现代 GPU 常用
   - SIMT stack: NVIDIA 专利技术

7. **Warp Shuffle**
   - `__shfl_sync()` — lane 间直接数据交换
   - 无需通过共享内存
   - 高效实现 reduction, scan 等操作

8. **Occupancy 占用率**
   - 高 occupancy 有利于延迟隐藏
   - 但更多寄存器/共享内存 > 更高 occupancy
   - 需要平衡

## References 参考资料

- CMU 15-418/618: Lecture 4 - SIMD and Vector Architectures
- Stanford CS149: Lecture 5 - GPU Architecture and CUDA
- NVIDIA CUDA Programming Guide: Chapter 5 - Performance Guidelines
- AMD GCN Architecture Whitepaper: Wavefront Execution Model
