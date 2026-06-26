# Mini Hardware Physical（迷你硬件物理层）

**从零开始、零依赖的 C 语言实现**，涵盖计算机硬件与物理层核心概念。每个模块以教学级精度仿真或建模真实硬件行为 — 从逻辑门到 CPU 流水线、GPU SIMD 核心、AI 加速器、网络硬件和存储控制器。模块对应 MIT、Stanford、CMU 课程，将硬件理论桥接到可运行的 C 代码。

## 模块总览

| 模块 | 主题 | 参考课程 |
|--------|--------|-------------|
| [mini-digital-circuit](mini-digital-circuit/) | 逻辑门、组合/时序电路、有限状态机、RTL 基础 | MIT 6.004, MIT 6.111 |
| [mini-cpu-arch](mini-cpu-arch/) | ISA 设计、流水线、超标量、分支预测、乱序执行 | MIT 6.004, MIT 6.175, Stanford EE282 |
| [mini-computer-arch](mini-computer-arch/) | 存储器层次、缓存设计、虚拟内存、多核、一致性协议 | MIT 6.823, MIT 6.5900, CMU 18-447 |
| [mini-gpu-arch](mini-gpu-arch/) | SIMD/SIMT、Warp 调度、Shader 核心、Tensor 核心、GPU 内存模型 | CMU 15-418, Stanford CS149 |
| [mini-ai-accelerator](mini-ai-accelerator/) | 脉动阵列、TPU 指令集、量化、稀疏加速、数据流 | Google TPU ISCA 2017, MIT 6.5930 |
| [mini-hardware-security](mini-hardware-security/) | 侧信道攻击、缓存计时、Meltdown/Spectre、安全飞地、PUF | MIT 6.5950, UC Berkeley CS261 |
| [mini-network-hardware](mini-network-hardware/) | 网卡架构、MAC/PHY、RDMA、硬件卸载、交换结构 | Stanford CS144, MIT 6.829 |
| [mini-storage-hardware](mini-storage-hardware/) | SSD 控制器、FTL、磨损均衡、垃圾回收、NVMe 协议 | CMU 18-746, Stanford CS240 |

## 设计理念

- **零外部依赖** — 纯 C（C99/C11），仅使用 `libc` 和 `libm`
- **模块自包含** — 每个目录自带 `Makefile`、`include/`、`src/`、`examples/`、`demos/`、`tests/`
- **软件仿真硬件** — 对真实硬件组件进行周期精确或行为级建模
- **理论到代码的映射** — 每个模块包含 `docs/` 目录，内有课程对齐说明
- **实用演示程序** — 逻辑门仿真器、流水线仿真器、缓存仿真器、TPU 脉动阵列等

## 构建方式

每个模块相互独立。进入模块目录后运行：

```bash
cd mini-cpu-arch
make all    # 构建全部
make test   # 运行测试
```

需要 **GCC** 和 **GNU Make**。

## 项目结构

```
mini-hardware-physical/
├── mini-digital-circuit/       # 数字逻辑与电路设计
├── mini-cpu-arch/              # CPU 微架构
├── mini-computer-arch/         # 计算机体系结构与内存系统
├── mini-gpu-arch/              # GPU 架构与并行处理
├── mini-ai-accelerator/        # AI 加速器（TPU、NPU）
├── mini-hardware-security/     # 硬件安全与侧信道攻击
├── mini-network-hardware/      # 网络硬件与卸载
└── mini-storage-hardware/      # 存储硬件与 SSD 控制器
```

## 许可证

MIT
