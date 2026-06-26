# mini-hardware-security — 硬件安全 (C 语言实现)

> 参考 MIT 6.5950 Hardware Security, UC Berkeley CS261, Stanford CS356

## 模块-课程映射 (Module-Course Mapping)

| 模块 | 英文名 | 功能 | 对应课程 |
|-----|--------|------|---------|
| `side_channel` | 侧信道攻击 | 时序/功耗/EM 侧信道模拟 | MIT 6.5950 Ch4-7 |
| `cache_attack` | 缓存攻击 | Flush+Reload, Prime+Probe, Evict+Time | MIT 6.5950 Ch5, CS261 |
| `spec_exec_atk` | 推测执行攻击 | Spectre v1/v2/v4, Meltdown | MIT 6.5950 Ch8 |
| `secure_enclave` | 安全飞地 | Intel SGX, 认证, 密封 | MIT 6.5950 Ch14-16 |
| `puf` | PUF | SRAM/Arbiter PUF, 模糊提取器, 认证 | MIT 6.5950 Ch12 |
| `fault_injection` | 故障注入 | 时钟/电压/激光, RSA-CRT Bellcore | MIT 6.5950 Ch3,9 |

## 目录树 (Directory Tree)

```
mini-hardware-security/
├── include/
│   ├── side_channel.h        # 侧信道攻击 (时序/功耗/EM/声学)
│   ├── cache_attack.h        # 缓存攻击 (Flush+Reload/Prime+Probe/Evict+Time)
│   ├── spec_exec_atk.h       # 推测执行攻击 (Spectre/Meltdown)
│   ├── secure_enclave.h      # 安全飞地 (SGX/TrustZone)
│   ├── puf.h                 # PUF (SRAM/Arbiter/RO, 模糊提取器)
│   └── fault_injection.h     # 故障注入 (时钟/电压/激光/RSA-CRT)
├── src/
│   ├── side_channel.c        # 侧信道实现 (100+ 行)
│   ├── cache_attack.c        # 缓存攻击实现 (100+ 行)
│   ├── spec_exec_atk.c       # 推测执行实现 (100+ 行)
│   ├── secure_enclave.c      # 安全飞地实现 (100+ 行, SHA256)
│   ├── puf.c                 # PUF 实现 (100+ 行)
│   └── fault_injection.c     # 故障注入实现 (100+ 行)
├── examples/
│   ├── cache_timing_demo.c   # Flush+Reload 演示
│   ├── spectre_demo.c        # Spectre v1 演示
│   ├── meltdown_demo.c       # Meltdown 演示
│   └── puf_demo.c            # PUF 认证演示
├── demos/
│   ├── mini-cache-attack/    # 缓存攻击详解 (250+ 行)
│   │   └── README.md
│   ├── mini-spectre/         # Spectre 详解 (250+ 行)
│   │   └── README.md
│   ├── mini-secure-enclave/  # SGX/飞地详解 (250+ 行)
│   │   └── README.md
│   └── mini-puf-auth/        # PUF 认证详解 (250+ 行)
│       └── README.md
├── docs/
│   ├── course-alignment.md           # 课程对齐文档
│   ├── side-channel-primer.md        # 侧信道攻防入门
│   ├── speculative-execution-attacks.md # 推测执行攻击详解
│   └── secure-enclaves.md            # TEE 安全综述
├── tests/                            # 测试 (预留)
├── benches/                          # 基准测试 (预留)
├── README.md                         # 本文件
└── Makefile                          # 构建文件
```

## 构建与测试 (Build & Test)

```bash
# 编译所有库源文件
make all

# 编译并运行所有演示
make cache_timing_demo
make spectre_demo
make meltdown_demo
make puf_demo

# 运行所有演示
make test

# 清理构建产物
make clean
```

## 核心概念 (Core Concepts)

### 侧信道攻击 (Side-Channel Attacks)

- **时序攻击**: 通过测量执行时间的微小差异推断秘密数据
- **缓存攻击**: Flush+Reload (共享内存), Prime+Probe (不需要共享内存), Evict+Time
- **功耗分析**: 简单功耗分析 (SPA), 差分功耗分析 (DPA)
- **功率模型**: Hamming Weight, Hamming Distance

### 推测执行攻击 (Speculative Execution Attacks)

- **Spectre v1 (BCB)**: 边界检查绕过, 训练分支预测器, 越界访问
- **Spectre v2 (BTI)**: 分支目标注入, 劫持间接跳转
- **Spectre v4 (SSB)**: 推测存储绕过
- **Meltdown**: 异常抑制, 从用户空间读取内核内存
- **隐蔽通道**: 使用缓存状态传输秘密数据的比特

### 安全飞地 (Secure Enclaves)

- **Intel SGX**: EPC, MRENCLAVE, MEE 加密, EPID/ECDSA 认证
- **AMD SEV-SNP**: 内存加密, RMP, 安全处理器
- **ARM TrustZone**: 安全世界/普通世界隔离, EL3 监视器
- **RISC-V Keystone**: PMP 隔离, M-mode 监控

### 物理不可克隆函数 (PUF)

- **SRAM PUF**: 上电状态的随机性, 4-6 个晶体管单元
- **Arbiter PUF**: 竞争路径延迟差异
- **认证协议**: 挑战-响应对 (CRP), Hamming 距离阈值
- **模糊提取器**: 注册 (Gen) + 重建 (Rep), 纠错码

### 故障注入 (Fault Injection)

- **时钟故障**: 跳过指令, 将条件分支变 NOP
- **电压故障**: 在位级别操纵寄存器或内存
- **RSA-CRT Bellcore 攻击**: 在 CRT 签名中注入故障, 通过 GCD 恢复私钥
- **RowHammer**: 内存中由相邻行访问引起的比特翻转

## 参考资料 (References)

- MIT 6.5950: Hardware Security (Spring 2024)
- UC Berkeley CS261: Security in Computer Systems
- Stanford CS356: Topics in Computer and Network Security
- Kocher et al. (2019): Spectre Attacks: Exploiting Speculative Execution
- Lipp et al. (2018): Meltdown: Reading Kernel Memory from User Space
- Intel SGX Developer Reference (329298-001US)
- Costan & Devadas (2016): Intel SGX Explained
