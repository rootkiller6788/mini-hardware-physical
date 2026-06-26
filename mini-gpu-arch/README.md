# mini-gpu-arch — GPU 架构 (C 语言实现)

> 参考 CMU 15-418 Parallel Architecture, Stanford CS149, UMich EECS 570

## 模块与课程对照

| 模块 (Module)      | 主题                         | CMU 15-418     | Stanford CS149 | UMich EECS 570    |
|--------------------|------------------------------|----------------|----------------|---------------------|
| `simd`             | SIMD 执行引擎                | Lecture 4      | Lecture 5      | SIMT Model          |
| `warp`             | Warp 调度与延迟隐藏            | Lecture 6      | Lecture 6      | Warp Scheduling     |
| `shader_core`      | Streaming Multiprocessor      | Lecture 6      | Lecture 5-6    | SIMT Microarch      |
| `tensor_core`      | Tensor Core 矩阵加速          | —              | Lecture 17     | Domain Accelerators |
| `memory_gpu`       | GPU 内存层次与合并访问         | Lecture 7      | Lecture 8      | Memory Hierarchy    |
| `thread_sched`     | Block/Grid 调度与 Occupancy   | Lecture 6      | Lecture 7      | Block Scheduling    |

## 目录结构

```
mini-gpu-arch/
├── include/
│   ├── simd.h              SIMD 执行模型
│   ├── warp.h              Warp 调度器
│   ├── shader_core.h       Shader Core (SM)
│   ├── tensor_core.h       Tensor Core
│   ├── memory_gpu.h        GPU 内存层次
│   └── thread_sched.h      Block 调度 & Occupancy
├── src/
│   ├── simd.c
│   ├── warp.c
│   ├── shader_core.c
│   ├── tensor_core.c
│   ├── memory_gpu.c
│   └── thread_sched.c
├── examples/
│   ├── simd_demo.c         SIMD 向量运算演示
│   ├── warp_sim_demo.c     Warp 调度模拟
│   ├── tensor_op_demo.c    Tensor Core 矩阵运算
│   ├── coalescing_demo.c   内存合并访问对比
│   └── occupancy_demo.c    Occupancy 计算器
├── demos/
│   ├── mini-simd-engine/
│   │   └── README.md       SIMD 引擎深度解析
│   ├── mini-warp-scheduler/
│   │   └── README.md       Warp 调度深度解析
│   ├── mini-tensor-core/
│   │   └── README.md       Tensor Core 深度解析
│   └── mini-gpu-mem-model/
│       └── README.md       GPU 内存模型深度解析
├── docs/
│   ├── course-alignment.md     课程对照表
│   ├── simt-model.md           SIMT 模型详解
│   ├── gpu-memory-coalescing.md 内存合并详解
│   └── tensor-core-internals.md Tensor Core 微架构
├── tests/
├── benches/
├── Makefile
└── README.md
```

## 构建与运行

```bash
cd mini-gpu-arch

# 编译所有 demo
make all

# 编译单个 demo
make simd_demo
make warp_sim_demo
make tensor_op_demo
make coalescing_demo
make occupancy_demo

# 运行全部 demo 测试
make test

# 清理
make clean
```

## 核心能力

| 能力                           | 描述                                          | 演示程序            |
|--------------------------------|-----------------------------------------------|---------------------|
| SIMD 向量执行                   | 多 lane 向量加/乘/FMA 操作, 谓词执行           | `simd_demo`         |
| Warp 调度模拟                   | Round-Robin/Greedy/Age 策略, 延迟隐藏验证      | `warp_sim_demo`     |
| 矩阵乘法加速                   | 4×4 MMA, 分块矩阵乘法, 吞吐量估算              | `tensor_op_demo`    |
| 内存合并分析                   | Coalesced/Uncoalesced 对比, Bank Conflict 检测 | `coalescing_demo`   |
| Occupancy 计算                 | 寄存器/共享内存约束下的活跃 warp 数计算         | `occupancy_demo`    |
| Shader Core 流水线              | 5 级流水: Fetch/Decode/Issue/Execute/Writeback | `shader_core.h/c`   |
| Block/Grid 调度                 | 面向 SM 的 thread block 分配策略               | `thread_sched.h/c`  |

## 技术栈

- **语言**: C99
- **依赖**: libc + libm only
- **编译器**: GCC (MinGW on Windows)
- **构建**: Make (GNU Make)

## 许可

仅供教学与学习使用。
