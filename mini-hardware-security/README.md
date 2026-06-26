# mini-hardware-security — 硬件安全 (Hardware Security Module)

纯 C99 实现的微型硬件安全模块（HSM），覆盖密码学原语、安全启动、TPM、TEE 可信执行环境、PUF 物理不可克隆函数、内存加密引擎、侧信道防御等核心硬件安全技术。

无需第三方依赖，仅依赖 C 标准库和数学库。适合学习硬件安全基础、安全芯片固件开发和嵌入式安全集成。

## Module Status: COMPLETE ✅

- **L1-L6**: Complete (全部核心定义、概念、工程结构、定理验证、算法实现、经典问题)
- **L7**: Complete (3+ 应用：HSM 加解密、安全启动演示、硬件钱包 TEE)
- **L8**: Partial (3/5 进阶主题已实现：侧信道防御、RSA 盲化、缓存分区)
- **L9**: Partial (后量子密码就绪已文档化，PQC 原语待完善)

---

## 九层知识覆盖摘要

| Level | 名称 | 状态 | 关键内容 |
|-------|------|------|----------|
| **L1** | Definitions | ✅ Complete | AES/SHA/HMAC/RSA/ECC/PUF/TPM/TEE 等 40+ struct/typedef |
| **L2** | Core Concepts | ✅ Complete | 安全启动链, 信任根, TEE 隔离, PUF 密钥生成, 内存加密 |
| **L3** | Engineering Structures | ✅ Complete | TPM 命令流水线, Enclave 生命周期, HRoT 安全存储, Merkle Tree 完整性 |
| **L4** | Standards/Theorems | ✅ Complete | FIPS 197 AES, FIPS 180-4 SHA-256, RFC 2104 HMAC, RFC 5869 HKDF, TCG TPM 2.0, IEEE 1619 XTS |
| **L5** | Algorithms/Methods | ✅ Complete | AES-256, SHA-256, HMAC, HKDF, ChaCha20 DRBG, ECDH, ECDSA, RSA-CRT, ISW Masking, Fuzzy Extractor |
| **L6** | Canonical Problems | ✅ Complete | 安全启动验证, TPM 远程证明, 飞地数据封装, 内存加密引擎 |
| **L7** | Applications | ✅ Complete | HSM 支付加密, 安全启动演示, 硬件钱包 (TEE+内存加密) |
| **L8** | Advanced Topics | ⚡ Partial | 侧信道防御(CT/掩码/盲化), DPA 检测, 缓存分区 ✅; 形式化验证, 差分故障分析 📋 |
| **L9** | Industry Frontiers | ⚡ Partial | 后量子密码就绪 (HKDF), 机密计算架构 📋; PQC 原语, 全同态加密 📋 |

---

## 核心定义列表 (L1)

### 密码学原语
- `MiniHwSecAesCtx` — AES-256 加密上下文（60 轮密钥）
- `MiniHwSecSha256Ctx` — SHA-256 哈希上下文
- `MiniHwSecEcPoint/PrivKey/PubKey` — P-256 椭圆曲线密钥
- `MiniHwSecRsaPubKey/PrivKey` — RSA-2048 密钥对

### 安全启动与 TPM
- `MiniHwSecBootChain` — 安全启动链（≤6 阶段）
- `MiniHwSecBootImage` — 启动镜像清单
- `MiniHwSecTPM` — TPM 2.0 模拟器
- `MiniHwSecPCR` — 平台配置寄存器
- `MiniHwSecTPMQuote` — 远程证明报价
- `MiniHwSecEventLog` — 启动事件日志

### 可信执行环境 (TEE)
- `MiniHwSecTEEManager` — TEE 平台管理器
- `MiniHwSecTEEEnclave` — 安全飞地 (16 个并发)
- `MiniHwSecTEEAttestReport` — 本地/远程证明报告
- `MiniHwSecTEESealedData` — 密封数据

### 硬件信任根 (HRoT)
- `MiniHwSecHRoT` — 硬件信任根
- `MiniHwSecSRAMPuf` — SRAM 物理不可克隆函数
- `MiniHwSecRingOscPuf` — 环形振荡器 PUF
- `MiniHwSecPufEnrollment` — PUF 注册数据

### 内存加密
- `MiniHwSecMemEngine` — 内存加密引擎
- `MiniHwSecMemIntegrityTree` — Merkle 完整性树
- `MiniHwSecMemReplayCounters` — 重放保护计数器

### 侧信道防御
- `MiniHwSecSCDetector` — 侧信道攻击检测器
- `MiniHwSecMaskedByte` — 布尔掩码字节 (2-8 shares)
- `MiniHwSecMaskedAESState` — 掩码 AES 状态
- `MiniHwSecCacheMonitor` — 缓存攻击监控

---

## 核心定理列表 (L4)

### AES 安全性 (FIPS 197)
- Shannon 混淆与扩散：SubBytes 混淆 + MixColumns 扩散
- 已知最佳攻击：Biclique 密码分析，复杂度 2^254.4

### SHA-256 碰撞抵抗 (FIPS 180-4)
- 生日界：寻找碰撞需要 O(2^128) 操作
- 原像抵抗：256 位安全性

### HMAC 安全性 (Bellare-Canetti-Krawczyk 1996)
- HMAC 是 PRF，如果压缩函数是 PRF
- 提供 256 位 EF-CMA 安全性

### PUF 不可克隆性 (Guajardo 2007)
- H_infty(R | P) ≥ m - |P| - 2log(1/ε)
- 设备间 Hamming 距离 ≈ 50%, 设备内可靠性 ≥ 85%

### 内存加密 (IEEE 1619)
- AES-XTS 是强伪随机置换 (SPRP)
- 生日界优势 ≤ q²/2^(n+1)

### DPA 检测 (Mangard 2007)
- 相关系数 ρ 对正确密钥收敛于 ρ_true
- 检测概率 = Φ((ρ_true - ρ_thresh) × √(N-3))

---

## 核心算法列表 (L5)

| 算法 | 复杂度 | 参考标准 |
|------|--------|----------|
| AES-256 加密/解密 | O(1) 固定轮数 | FIPS 197 |
| AES-256 密钥扩展 | O(1) 60 轮 | FIPS 197 §5.2 |
| SHA-256 压缩 | O(n) 每 512 位块 | FIPS 180-4 §6.2 |
| HMAC-SHA256 | O(n) | RFC 2104 |
| HKDF-SHA256 派生 | O(k) k = OKM 长度 | RFC 5869 |
| ChaCha20 DRBG | O(n) 每 64 字节 | NIST SP 800-90A |
| GHASH GF(2^128) 乘法 | O(m+n) | NIST SP 800-38D |
| ISW 布尔掩码 (AND) | O(n²) n=shares | CRYPTO 2003 |
| Fuzzy Extractor (BCH) | O(n) | Dodis et al. 2004 |
| 常数时间比较 | Θ(len) | 侧信道防御 |
| AES-XTS 内存加密 | O(n) | IEEE 1619 |

---

## 模块总览与命名约定

**类型前缀**: `MiniHwSec`  
**宏前缀**: `MINI_HWSEC_`  
**包含保护**: `#ifndef MINI_HWSEC_<MODULE>_H`

---

## 目录结构

```
mini-hardware-security/
├── Makefile              # make test 一键通过 (27 测试)
├── README.md             # 本文件 - 知识覆盖报告
├── include/
│   ├── hw_crypto.h       # 密码学原语: AES-256, SHA-256, HMAC, ECDSA, RSA
│   ├── secure_boot.h     # 安全启动链, TPM 2.0, 事件日志
│   ├── side_channel.h    # 侧信道防御: 常数时间操作, 掩码, DPA检测
│   ├── tee_enclave.h     # 可信执行环境: Enclave 生命周期, 证明, 密封
│   ├── hrot_puf.h        # 硬件信任根: SRAM PUF, 安全计数器, 密钥槽
│   └── memory_crypto.h   # 内存加密引擎: XTS, Merkle Tree, 重放保护
├── src/
│   ├── hw_crypto.c       # AES/SHA/HMAC/HKDF/ChaCha20/ECDSA/RSA 实现
│   ├── secure_boot.c     # 安全启动执行, TPM 模拟, 证书链验证
│   ├── side_channel.c    # 常数时间操作, 掩码, DPA检测, 传感器监控
│   ├── tee_enclave.c     # Enclave 生命周期, 本地/远程证明, 数据密封
│   ├── hrot_puf.c        # SRAM PUF 注册/重建, BCH纠错, 单调计数器
│   └── memory_crypto.c   # AES-GCM 内存加解密, Merkle Tree 完整性
├── tests/
│   └── test_all.c        # 27 个全量测试 (L1-L9 覆盖)
├── examples/
│   ├── demo_aes_hsm.c    # 硬件安全模块(HSM)加密演示
│   ├── demo_secure_boot.c # 安全启动+TPM证明演示
│   └── demo_tee_memory.c  # TEE+内存加密集成演示
├── benches/
├── demos/
└── docs/
```

## 构建与测试

```bash
make          # 编译目标文件到 bin/
make test     # 编译并运行全部测试 (27 tests, 0 failures)
make examples # 编译示例程序
make clean    # 清理构建产物
```

要求：GCC ≥ 5.x, Make, 标准 C 数学库 (-lm)

---

## 运行示例

```bash
./bin/demo_aes_hsm       # HSM 加密演示：AES-GCM 支付加密 + CTR 磁盘加密
./bin/demo_secure_boot    # 安全启动演示：TPM PCR 测量 + 远程证明报价
./bin/demo_tee_memory     # 硬件钱包演示：TEE + 内存加密 + 侧信道防御
```

---

## 九校课程映射

| 学校 | 课程 | 对应章节 |
|------|------|----------|
| **MIT** | 6.858 Computer Security | Symmetric Crypto, Trusted Boot, TEE |
| **Stanford** | CS255 Introduction to Cryptography | AES, HMAC, ECDSA |
| **CMU** | 18-732 Secure Software Engineering | Constant-time programming, Side-channels |
| **Berkeley** | CS 161 Computer Security | Secure boot, TPM, Trusted Computing |
| **Cambridge** | Part II Security | Side-channel analysis, Trusted Execution |
| **清华** | 密码学与网络安全 | SM2/SM3/SM4 对标, PUF 硬件安全 |
| **ETH** | 263-4640 Hardware Security | PUF, Fault injection, Tamper detection |
| **UT Austin** | ECE 382V VLSI | Hardware root of trust, Secure RTL |
| **Georgia Tech** | CS 6262 Network Security | TPM attestation, Secure channels |

## L7 应用示例

- **HSM 支付加密** (`demo_aes_hsm.c`): AES-256-GCM 支付记录保护, CTR 磁盘加密
- **安全启动演示** (`demo_secure_boot.c`): 三阶段启动链, TPM PCR 测量, 远程证明报价, 事件日志审计
- **硬件钱包** (`demo_tee_memory.c`): PUF 密钥派生, TEE Enclave 保护私钥, 内存加密, 侧信道攻击监控

## L8 进阶主题

- ✅ 常数时间编程（侧信道防御）
- ✅ 布尔掩码 (ISW 方案, O(n²) AND 门)
- ✅ RSA 盲化 (Kocher 定时攻击防御)
- ✅ DPA 统计检测 (Pearson 相关性分析)
- ✅ 缓存分区 (CAT 模型, Prime+Probe 检测)
- 📋 形式化验证 (TLA+/Coq) - 仅文档
- 📋 差分故障分析 (DFA) - 仅文档

## L9 工业前沿

- ✅ 后量子密码就绪 (HKDF 密钥派生框架)
- ✅ 机密计算架构 (TEE + 内存加密 + 远程证明)
- 📋 全同态加密 (FHE) - 仅文档
- 📋 后量子 NIST 候选算法 (Kyber/Dilithium) - 仅文档

---

## 跨模块集成

本模块提供硬件安全审计接口，可用于：

- **网络模块(5) 安全审计**: TPM 远程证明报价验证网络节点完整性
- **后端模块(8) 入口安全**: TEE Enclave 保护后端 API 密钥, 内存加密防止 DRAM 嗅探
- **数据引擎(7) 向量存储**: PUF 派生密钥加密向量数据库
- **AI 模块(14) 模型保护**: TEE 保护 ML 推理权重, 侧信道防御防止模型提取

跨模块数据流：
```
HRoT (PUF Key) → Secure Boot → TPM Quote → TEE Enclave → Sealed Storage
                                         ↓
                                   Memory Encryption (MEE)
                                         ↓
                                   Network Attestation → Backend Auth
```

## 许可证

MIT License — 自由使用、修改与分发。
