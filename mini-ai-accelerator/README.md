# mini-ai-accelerator — AI 加速器 (C 语言实现)

> 参考 Google TPU ISCA 2017, MIT 6.5930, Stanford CS217

## Module Status: COMPLETE ✅

- **L1 (Definitions)**: Complete — 9 header files with struct/typedef/enum/API
- **L2 (Core Concepts)**: Complete — 9 sub-modules covering all AI accelerator fundamentals
- **L3 (Engineering Structures)**: Complete — 5 dataflows, double-buffering, CSR/BCSR/ELLPACK, pipeline
- **L4 (Standards/Theorems)**: Complete — Roofline, Amdahl, Gustafson, MAC utilization, memory energy
- **L5 (Algorithms/Methods)**: Complete — Winograd F(2,3), Flash Attention, FP16/BF16/FP8, KL divergence, pruning
- **L6 (Canonical Problems)**: Complete — 7 end-to-end demos in examples/
- **L7 (Applications)**: Complete (3+) — Multi-Head Attention, Multi-Core TPU, DMA, KV Cache
- **L8 (Advanced Topics)**: Complete (4+) — Flash Attention tiling, KV cache, block-sparse, structured sparse MMA
- **L9 (Industry Frontiers)**: Partial — Documented (TPUv4, FP8 E4M3, Flash Attention)

**include/ + src/: 5,142 lines** (threshold: 3,000)

## 模块概述 (Module Overview)

`mini-ai-accelerator` 是一个 AI 硬件加速器的 C 语言教学实现,涵盖从脉动阵列 (Systolic Array) 到 TPU 指令集、量化、稀疏加速、数据流分析和 MMA 引擎的核心概念。所有代码仅依赖 C99 标准库 + libm,适合嵌入式系统和教学使用。

## 模块与课程映射 (Module-Course Mapping)

| 模块 | 类型 | 核心概念 | 参考课程 |
|------|------|---------|---------|
| `systolic_array` | Compute Fabric | 2D systolic array, weight stationary dataflow, cycle-level simulation | MIT 6.5930, TPU ISCA 2017 |
| `tpu_isa` | Instruction Set | TPU CISC ISA (8 opcodes), unified buffer, accumulator pipeline | TPU ISCA 2017 |
| `quantization` | Precision Reduction | INT8 per-tensor/per-channel, affine quantization, MSE error | Stanford CS217 |
| `sparse_accel` | Sparsity | 2:4 structured pruning, CSR format, sparse-dense matmul | Stanford CS217 |
| `dataflow` | Data Movement | Weight/Output/Input/Row stationary energy models | MIT 6.5930 (Eyeriss) |
| `mma` | Matrix Engine | Tile MMA, im2col, Winograd F(2,3), depthwise conv, tile size opt | MIT 6.5930 |
| `accelerator_roofline` | Performance | Roofline model, Amdahl's Law, MAC utilization, memory hierarchy energy | Berkeley CS267 |
| `attention_accel` | Transformer | Multi-head attention, Flash Attention, KV cache, causal masking, tiling | Berkeley CS294 |
| `tensor_core` | Microarchitecture | FP16/BF16/FP8, warp MMA, structured sparse MMA, fragment model | CMU 15-418 |

## 目录结构 (Directory Tree)

```
mini-ai-accelerator/
├── Makefile
├── README.md
├── include/
│   ├── systolic_array.h      # Systolic array definitions + DoubleBuffer
│   ├── tpu_isa.h              # TPU ISA + MultiCore + Pipeline + DMA
│   ├── quantization.h         # Quantization (INT4/INT8/FP16/KL/symmetric)
│   ├── sparse_accel.h         # Sparse (CSR/BCSR/ELLPACK/BlockSparse)
│   ├── dataflow.h             # Dataflow + Utilization + Eyeriss
│   ├── mma.h                  # MMA + Winograd + Depthwise
│   ├── accelerator_roofline.h # Roofline model + Amdahl's Law
│   ├── attention_accel.h      # Multi-head attention + Flash Attention
│   └── tensor_core.h          # Tensor Core + FP16/BF16/FP8 MMA
├── src/
│   ├── systolic_array.c       # (334行) Systolic array + OS + DoubleBuffer + Utilization
│   ├── tpu_isa.c              # (435行) TPU ISA + MultiCore + Pipeline + DMA
│   ├── quantization.c         # (510行) INT4/FP16/KL/PerAxis/Dynamic/Symmetric
│   ├── sparse_accel.c         # (590行) CSR/BCSR/ELLPACK/BlockSparse/IterPrune
│   ├── dataflow.c             # (317行) 4 dataflows + energy breakdown + Eyeriss
│   ├── mma.c                  # (472行) Tile MMA + Winograd F(2,3) + Depthwise
│   ├── accelerator_roofline.c # (379行) Roofline + Amdahl + memory hierarchy
│   ├── attention_accel.c      # (709行) Attention + Flash Attention + KV Cache
│   └── tensor_core.c          # (628行) FP16/BF16/FP8 + MMA + Sparse MMA
├── examples/
│   ├── systolic_mm_demo.c     # 4x4 systolic array matmul demo
│   ├── quantize_demo.c        # INT8 quantization error analysis
│   ├── sparse_dot_demo.c      # 2:4 sparsity acceleration demo
│   ├── dataflow_demo.c        # Dataflow strategy comparison
│   ├── accelerator_analysis_demo.c  # Roofline + Amdahl analysis
│   ├── attention_demo.c       # Attention + Flash Attention + KV Cache demo
│   └── tensor_core_demo.c     # Tensor Core MMA + precision demo
├── demos/
│   ├── mini-systolic-array/
│   │   └── README.md          # Deep dive: systolic array theory
│   ├── mini-tpu-sim/
│   │   └── README.md          # Deep dive: TPU architecture simulator
│   ├── mini-quantized-inference/
│   │   └── README.md          # Deep dive: quantization techniques
│   └── mini-sparse-accelerator/
│       └── README.md          # Deep dive: sparse acceleration
├── docs/
│   ├── course-alignment.md    # Full course mapping
│   ├── systolic-arrays.md     # Systolic array survey
│   ├── quantization-techniques.md  # Quantization survey
│   └── tpu-architecture.md    # TPU architecture evolution
├── tests/
└── benches/
```

## 构建 (Build)

```bash
cd mini-ai-accelerator
make all       # 构建所有示例
make test      # 运行所有演示
make clean     # 清理构建产物
```

### 单独构建 (Individual Builds)

```bash
make systolic_mm_demo   # 脉动阵列矩阵乘法演示
make quantize_demo      # INT8 量化误差分析
make sparse_dot_demo    # 2:4 结构化稀疏演示
make dataflow_demo      # 数据流策略对比
```

## 核心能力 (Core Capabilities)

### 1. 脉动阵列 (Systolic Array)
- 可配置的 N×N 2D 脉动阵列 (最大 16×16)
- 权重静止 (Weight Stationary) 数据流
- 逐周期状态跟踪
- 矩阵乘法 C = A × B 的完整流水线执行
- 吞吐量公式: `Ops = 2 × N² × f`

### 2. TPU 指令集 (TPU ISA)
- 8 条 CISC 指令: MATMUL, VECTOR_ADD, VECTOR_MUL, ACTIVATION, LOAD_WEIGHT, LOAD_ACT, STORE, SYNC
- 24 MB 统一缓冲区 (Unified Buffer) 模拟
- 累加器缓冲区 (4096 × FP32)
- 标量和向量单元
- 确定性执行模型

### 3. 量化 (Quantization)
- INT8 仿射量化 (Asymmetric): `q = round(x/S) + Z`
- 每张量 (Per-Tensor) 和每通道 (Per-Channel) 量化参数
- 量化误差分析: MSE, 最大误差
- 模拟量化往返 (round-trip) 误差
- 支持的量化类型: INT8, INT4, FP16

### 4. 稀疏加速 (Sparse Acceleration)
- CSR (Compressed Sparse Row) 稀疏格式转换
- 稀疏-稠密矩阵乘法 (SpMM): O(nnz) 复杂度
- NVIDIA 2:4 结构化稀疏: 每组 4 个值保留最大的 2 个
- 压缩率分析: CSR 存储 vs 稠密存储
- 加速因子估算

### 5. 数据流 (Dataflow)
- 四种数据流策略: Weight Stationary, Output Stationary, Input Stationary, Row Stationary (Eyeriss)
- 能量模型: MAC=1pJ, SRAM=5pJ, DRAM=640pJ
- 周期估算
- 不同卷积层形状的对比分析

### 6. MMA 引擎 (Matrix Multiply-Accumulate)
- 8×8 瓦片级矩阵乘法
- 大型矩阵的分块 (Tiling) 乘法
- im2col + MatMul 模拟卷积
- 可配置瓦片大小和分块策略

## 硬件参数参考

| 平台 | 阵列尺寸 | 频率 | 峰值性能 | 片上内存 |
|------|---------|------|---------|---------|
| TPUv1 | 256×256 | 700 MHz | 92 TOPS (int8) | 24 MB SRAM |
| TPUv2 | 128×128 ×2 | 700 MHz | 180 TFLOPS (bf16) | 16 MB SRAM |
| TPUv3 | 128×128 ×2 | 940 MHz | 420 TFLOPS (bf16) | 32 MB SRAM |
| TPUv4 | 128×128 ×8 | 1.05 GHz | 275 TFLOPS (bf16) | 128 MB SRAM |
| Eyeriss v1 | 12×14 | 200 MHz | 0.067 TOPS | 108 KB |
| Eyeriss v2 | 16×16 ×8 | 250 MHz | 1.02 TOPS | 384 KB |

## 关键公式

```
脉动阵列吞吐量:   TOPS = 2 × N² × f × 10⁻¹²
脉动阵列延迟:     Latency = M + N + K - 2 cycles
量化误差上限:     ε_max = S / 2
量化均方误差:     MSE = S² / 12 (均匀分布)
稀疏加速比:       Speedup = Dense_MACs / nnz
数据流能量模型:   E_total = N_MAC × E_MAC + N_SRAM × E_SRAM + N_DRAM × E_DRAM
```

## 依赖

- GCC (或其他 C99 编译器)
- GNU Make
- libm (数学库)

## 参考文献

1. Jouppi, N.P. et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017
2. Kung, H.T. and Leiserson, C.E., "Systolic Arrays (for VLSI)", CMU-CS-79-103, 1978
3. Chen, Y.H. et al., "Eyeriss: A Spatial Architecture for Energy-Efficient Dataflow for CNNs", ISCA 2016
4. Jacob, B. et al., "Quantization and Training of Neural Networks for Efficient Integer-Arithmetic-Only Inference", CVPR 2018
5. Dettmers, T. et al., "LLM.int8(): 8-bit Matrix Multiplication for Transformers at Scale", NeurIPS 2022
6. Frankle, J. and Carbin, M., "The Lottery Ticket Hypothesis", ICLR 2019
7. MIT 6.5930: Hardware Architecture for Deep Learning, Fall 2023
8. Stanford CS217: Hardware Accelerators for Machine Learning
