# mini-network-hardware — 网络硬件 (C 语言实现)

> 参考 Stanford CS144, MIT 6.829, UC Berkeley EE 122, CMU 15-441,
> ETH 263-0005, Cambridge Part II Computer Networking
> — 计算机网络课程中的硬件层、链路层与传输加速技术

## Module Status: COMPLETE ✅

- L1-L6: Complete
- L7: Complete (5 applications: PAUSE/PFC, Token Bucket, TSN, DCB, ETS)
- L8: Partial (PAM4 SerDes, 8b/10b line coding, PTP clock synchronization)
- L9: Partial (400G/800G PHY, TSN in 5G fronthaul - documented)

**Code Line Count**: include/ + src/ = 3025 lines ✅ (threshold: ≥3000)

## 九层知识覆盖 / Nine-Level Knowledge Coverage

| Level | 名称 | 状态 | 实现模块 |
|-------|------|------|---------|
| **L1** | Definitions | ✅ Complete | 10 modules: structs/typedefs/enums/API for NIC, MAC, RDMA, Offload, Switch, PCIe, SerDes, Flow, Timestamp, QoS |
| **L2** | Core Concepts | ✅ Complete | DC balance (8b/10b), token bucket, hardware timestamping, PFC, ETS, self-synchronous scrambler |
| **L3** | Engineering Structures | ✅ Complete | Descriptor rings, crossbar fabric, 8b/10b pipeline, CQ, TLP, PTP clock sync pipeline |
| **L4** | Standards/Theorems | ✅ Complete | Shannon-Hartley (C=B·log₂(1+S/N)), Nyquist, Little's Law (L=λW), CRC32, PTP offset formula, 802.3x/802.1Qbb/802.1Qaz |
| **L5** | Algorithms/Methods | ✅ Complete | 8b/10b encode/decode, Token Bucket, DRR scheduling, PRBS scrambler, PTP delay measurement, BMCA |
| **L6** | Canonical Problems | ✅ Complete | 8 examples: MAC framing, RDMA transport, switch fabric, checksum offload, SerDes coding, flow control |
| **L7** | Applications | ✅ Complete (5) | PFC lossless Ethernet, Token Bucket policing, TSN time-aware shaper, DCB, ETS bandwidth allocation |
| **L8** | Advanced Topics | ✅ Partial (3) | PAM4 modulation, 8b/10b DC-balanced coding, PTP HW timestamping |
| **L9** | Industry Frontiers | ✅ Partial | 400G/800G SerDes, TSN for 5G - documented in code comments |

## 模块-课程映射表 / Module-Course Mapping

| 模块 / Module               | Stanford CS144                    | MIT 6.829                      | UC Berkeley EE 122           |
|-----------------------------|-----------------------------------|--------------------------------|------------------------------|
| **nic_arch** (NIC 架构)      | Lecture 3: Packet Switching, NIC  | —                              | Lecture 8: Link Layer, NIC   |
| **mac** (MAC/PHY 层)         | Lecture 19: Ethernet, MAC, CRC    | Lecture 3: Link Layer          | Lecture 9: Ethernet          |
| **switch_fabric** (交换架构)  | Lecture 20: Bridging, Switching   | Lecture 7: Switch Design       | Lecture 10: Switching        |
| **offload** (硬件卸载)        | Lecture 22: TSO/LRO, Checksum     | Lecture 21: Hardware Offload   | Lecture 19: TCP Offloading   |
| **rdma** (远程直接内存访问)    | —                                 | Lecture 19: RDMA, RoCE         | Lecture 20: RDMA, Kernel Bypass |
| **pcie** (PCIe 接口)         | Lecture 1 (context): Host-NIC     | —                              | —                            |
| **serdes** (SerDes/线路编码)  | —                                 | —                              | EE 122: Physical Layer       |
| **flow** (流量控制)           | Lecture 19: Ethernet PAUSE        | Lecture 3: Flow Control        | Lecture 8: Link Layer        |
| **timestamp** (硬件时间戳)    | —                                 | —                              | EE 122: Timing               |
| **qos** (QoS/DCB)            | —                                 | Lecture 19: QoS                | Lecture 18: QoS              |

## 目录树 / Directory Tree

```
mini-network-hardware/
├── include/
│   ├── nic_arch.h          # NIC 架构：描述符环、DMA、中断
│   ├── mac.h               # MAC/PHY 层：以太帧、CRC32
│   ├── rdma.h              # RDMA：队列对、内存注册、单边操作
│   ├── offload.h           # 硬件卸载：校验和、TSO/LRO/GRO
│   ├── switch_fabric.h     # 交换架构：Crossbar、MAC 学习
│   ├── pcie.h              # PCIe 接口：链路、TLP 包
│   ├── serdes.h            # SerDes/线路编码：8b/10b, 64b/66b, PAM4
│   ├── flow.h              # 流量控制：PAUSE, PFC, Token/Leaky Bucket
│   ├── timestamp.h         # 硬件时间戳：PTP, TSN gate control
│   └── qos.h               # QoS/DCB: SP/WRR/DRR, ETS, DSCP
├── src/
│   ├── nic_arch.c          # NIC 架构实现
│   ├── mac.c               # MAC 层实现
│   ├── rdma.c              # RDMA 模拟实现
│   ├── offload.c           # 卸载引擎实现
│   ├── switch_fabric.c     # 交换架构实现
│   ├── pcie.c              # PCIe 接口实现
│   ├── serdes.c            # SerDes/线路编码实现 (8b/10b, 64b/66b, PAM4, Shannon)
│   ├── flow.c              # 流量控制实现 (PAUSE, PFC, Token Bucket, Little's Law)
│   ├── timestamp.c         # 硬件时间戳实现 (PTP, HW TS, TSN)
│   └── qos.c               # QoS实现 (SP/WRR/DRR, ETS, DCB, DSCP)
├── examples/
│   ├── mac_demo.c           # MAC: 地址解析、帧构建、CRC 验证
│   ├── rdma_demo.c          # RDMA: 内存注册、单边写、CQ 轮询
│   ├── switch_sim_demo.c    # 交换: 4端口交换、MAC 学习、泛洪
│   ├── checksum_offload_demo.c  # 卸载: IP 校验和、TSO 分段
│   ├── serdes_demo.c        # SerDes: 8b/10b, 64b/66b, PAM4, Shannon分析
│   └── flow_demo.c          # 流量: PAUSE, PFC, Token/Leaky Bucket, Little
├── tests/
│   └── test_runner.c       # 58 个 assert-based 测试，覆盖所有模块
├── demos/
│   ├── mini-nic-sim/README.md         # NIC 架构深入解析
│   ├── mini-rdma-transport/README.md  # RDMA 内部机制
│   ├── mini-switch-fabric/README.md   # 交换架构深入
│   └── mini-hardware-offload/README.md # 硬件卸载综述
├── docs/
│   ├── course-alignment.md    # 模块-课程详细对照
│   ├── nic-architecture.md    # NIC 内部架构文档
│   ├── rdma-internals.md      # RDMA 深入文档
│   └── hardware-offloading.md # 硬件卸载综述文档
├── benches/
├── Makefile
└── README.md
```

## 构建 / Build

```bash
make all          # 编译所有模块和演示程序
make test         # 运行所有演示 + 58 个 assert 测试
make mac_demo     # 构建 MAC 演示
make rdma_demo    # 构建 RDMA 演示
make switch_sim_demo     # 构建交换仿真演示
make checksum_offload_demo  # 构建校验和/TSO 演示
make serdes_demo  # 构建 SerDes/线路编码演示
make flow_demo    # 构建流量控制演示
make network_test # 构建并运行 assert 测试套件
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
| **SerDes/线路编码** / Line Coding   | 8b/10b DC平衡编码、64b/66b scrambling、PAM4 Gray调制   | `serdes.c`         |
| **流量控制** / Flow Control        | 802.3x PAUSE帧、PFC优先级流控、Token/Leaky Bucket      | `flow.c`           |
| **硬件时间戳** / HW Timestamping   | IEEE 1588 PTP时钟同步、HW时间戳捕获、TSN Gate Control   | `timestamp.c`      |
| **QoS/DCB** / Quality of Service   | SP/WRR/DRR调度、ETS带宽分配、DSCP分类、DCB              | `qos.c`            |

## 核心定理 / Key Theorems (L4)

| 定理 | 公式 | 实现 |
|------|------|------|
| **Shannon-Hartley** | C = B · log₂(1 + S/N) | `serdes.c:shannon_capacity()` |
| **Nyquist Criterion** | R_max = 2B · log₂(M) | `serdes.c:nyquist_bitrate()` |
| **Little's Law** | L = λ · W | `flow.c:littles_law_queue_length()` |
| **PTP Offset** | offset = ((t₂-t₁) - (t₄-t₃))/2 | `timestamp.c:ptp_calculate_offset()` |
| **PTP Delay** | delay = ((t₂-t₁) + (t₄-t₃))/2 | `timestamp.c:ptp_calculate_offset()` |
| **CRC32** | Polynomial: 0xEDB88320 | `mac.c:mac_crc32()` |
| **Bandwidth-Delay Product** | Buffer ≥ RTT · C | `flow.c:bandwidth_delay_product()` |
| **DRR Fairness** | Within one quantum of ideal | `qos.c:drr_schedule_next()` |

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
