# mini-network-hardware — 网络硬件 (C 语言实现)

> 参考 Stanford CS144, MIT 6.829, UC Berkeley EE 122 — 计算机网络课程中的硬件层、链路层与传输加速技术

## 模块-课程映射表 / Module-Course Mapping

| 模块 / Module               | Stanford CS144                    | MIT 6.829                      | UC Berkeley EE 122           |
|-----------------------------|-----------------------------------|--------------------------------|------------------------------|
| **nic_arch** (NIC 架构)      | Lecture 3: Packet Switching, NIC  | —                              | Lecture 8: Link Layer, NIC   |
| **mac** (MAC/PHY 层)         | Lecture 19: Ethernet, MAC, CRC    | Lecture 3: Link Layer          | Lecture 9: Ethernet          |
| **switch_fabric** (交换架构)  | Lecture 20: Bridging, Switching   | Lecture 7: Switch Design       | Lecture 10: Switching        |
| **offload** (硬件卸载)        | Lecture 22: TSO/LRO, Checksum     | Lecture 21: Hardware Offload   | Lecture 19: TCP Offloading   |
| **rdma** (远程直接内存访问)    | —                                 | Lecture 19: RDMA, RoCE         | Lecture 20: RDMA, Kernel Bypass |
| **pcie** (PCIe 接口)         | Lecture 1 (context): Host-NIC     | —                              | —                            |

## 目录树 / Directory Tree

```
mini-network-hardware/
├── include/
│   ├── nic_arch.h          # NIC 架构：描述符环、DMA、中断
│   ├── mac.h               # MAC/PHY 层：以太帧、CRC32
│   ├── rdma.h              # RDMA：队列对、内存注册、单边操作
│   ├── offload.h           # 硬件卸载：校验和、TSO/LRO
│   ├── switch_fabric.h     # 交换架构：Crossbar、MAC 学习
│   └── pcie.h              # PCIe 接口：链路、TLP 包
├── src/
│   ├── nic_arch.c          # NIC 架构实现
│   ├── mac.c               # MAC 层实现
│   ├── rdma.c              # RDMA 模拟实现
│   ├── offload.c           # 卸载引擎实现
│   ├── switch_fabric.c     # 交换架构实现
│   └── pcie.c              # PCIe 接口实现
├── examples/
│   ├── mac_demo.c           # MAC: 地址解析、帧构建、CRC 验证
│   ├── rdma_demo.c          # RDMA: 内存注册、单边写、CQ 轮询
│   ├── switch_sim_demo.c    # 交换: 4端口交换、MAC 学习、泛洪
│   └── checksum_offload_demo.c  # 卸载: IP 校验和、TSO 分段
├── demos/
│   ├── mini-nic-sim/README.md         # NIC 架构深入解析 (250+ 行)
│   ├── mini-rdma-transport/README.md  # RDMA 内部机制 (250+ 行)
│   ├── mini-switch-fabric/README.md   # 交换架构深入 (250+ 行)
│   └── mini-hardware-offload/README.md # 硬件卸载综述 (250+ 行)
├── docs/
│   ├── course-alignment.md    # 模块-课程详细对照
│   ├── nic-architecture.md    # NIC 内部架构文档
│   ├── rdma-internals.md      # RDMA 深入文档
│   └── hardware-offloading.md # 硬件卸载综述文档
├── tests/
├── benches/
├── Makefile
└── README.md
```

## 构建 / Build

```bash
make all          # 编译所有模块和演示程序
make mac_demo     # 构建 MAC 演示
make rdma_demo    # 构建 RDMA 演示
make switch_sim_demo     # 构建交换仿真演示
make checksum_offload_demo  # 构建校验和/TSO 演示
make test         # 运行所有演示程序
make clean        # 清理构建产物
```

## 核心能力 / Core Capabilities

| 能力 / Capability                  | 描述 / Description                                    | 源文件 / Source    |
|------------------------------------|-------------------------------------------------------|--------------------|
| **NIC 仿真** / NIC Simulation       | 描述符环形队列 (TX/RX)、DMA 引擎、中断处理              | `nic_arch.c`       |
| **MAC 帧处理** / MAC Frame          | 地址解析、帧构建、CRC32 计算与验证                      | `mac.c`            |
| **RDMA 传输** / RDMA Transport      | 队列对 (QP)、内存注册 (MR)、单边写、CQ 轮询             | `rdma.c`           |
| **硬件卸载** / Hardware Offload     | IP/TCP 校验和 (反码和)、TSO 分段 (MSS 切分)、GRO/LRO   | `offload.c`        |
| **交换架构** / Switch Fabric        | 4 端口 Crossbar 交换、MAC 地址自学习、泛洪转发          | `switch_fabric.c`  |
| **PCIe 接口** / PCIe Interface     | 多代 PCIe 链路建模、TLP 包构造、配置空间读写            | `pcie.c`           |

## 演示程序输出示例 / Demo Output Example

```bash
$ make mac_demo && ./bin/mac_demo
=== mini-network-hardware: MAC Layer Demo ===

[1] MAC Address Parsing
    Input string:  aa:bb:cc:dd:ee:ff
    Parsed (round-trip): aa:bb:cc:dd:ee:ff

[2] Ethernet Frame Construction
    Dst MAC:       11:22:33:44:55:66
    Src MAC:       aa:bb:cc:dd:ee:ff
    EtherType:     0x0800 (IPv4)
    ...
```

## 依赖 / Dependencies

- C99 编译器 (GCC/Clang/MSVC)
- libc (stdio, stdlib, string, stdint)
- libm (数学库)

无其他外部依赖。纯 C 语言实现，适合教学与嵌入式环境。

## 许可证 / License

MIT License — 仅供教育用途 / For educational purposes only.

---

*Generated for mini-network-hardware project · mini-everything 系列*
