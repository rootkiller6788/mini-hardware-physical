# Mini Secure Enclave — 安全飞地 (Intel SGX / AMD SEV / ARM TrustZone) 详解

> 本文档深入讲解安全飞地技术 (Secure Enclaves / Trusted Execution Environments) 的原理与实现

---

## 1. 概述 (Overview)

安全飞地 (Secure Enclave) 是一种硬件级隔离技术，在 CPU 内部创建一个受保护的内存区域，
即使是操作系统、Hypervisor、或 DMA 设备也无法访问飞地内的数据。

### 主流技术对比

| 特性 | Intel SGX | AMD SEV-SNP | ARM TrustZone | RISC-V Keystone |
|-----|-----------|------------|---------------|-----------------|
| 安全模型 | 只信任CPU | 信任CPU+固件 | 安全/普通世界 | 自定义PMP |
| 内存加密 | MEE (AES-GCM) | SME (AES-128) | 无(物理隔离) | 可选 |
| 飞地大小 | 128MB-1TB | 整个VM | 动态 | 自定义 |
| 远程认证 | EPID/ECDSA | SEV-SNP attest | 无标准 | 插件式 |
| 密封 | EGETKEY | 磁盘加密密钥 | RPMB | 可选 |
| 侧信道防御 | 有限 | 更强 | 中 | 中 |

---

## 2. Intel SGX 架构

### 2.1 EPC (Enclave Page Cache)

EPC 是专门存放飞地代码和数据的物理内存区域。所有 EPC 页面在离开 CPU 封装时自动加密。

```
┌──────────────────────────────────────┐
│  System Memory (DRAM)                 │
│  ┌─────────────────┐ ┌──────────────┐│
│  │  PRM (128MB)    │ │  Normal DRAM ││
│  │  ┌─────────────┐│ │              ││
│  │  │EPC 页面(4KB)││ │              ││
│  │  │- AES-GCM加密 ││ │              ││
│  │  │- 完整性保护  ││ │              ││
│  │  │- 版本号防回放││ │              ││
│  │  └─────────────┘│ │              ││
│  └─────────────────┘ └──────────────┘│
└──────────────────────────────────────┘
```

### 2.2 SGX 指令集

| 指令 | 操作码 | 功能 | Ring |
|-----|-------|------|------|
| ECREATE | 0x00 | 创建飞地控制结构 (SECS) | Ring-0 |
| EADD | 0x01 | 向 EPC 添加页面 | Ring-0 |
| EEXTEND | 0x02 | 扩展度量值 (SHA256) | Ring-0 |
| EINIT | 0x03 | 初始化飞地 (锁定) | Ring-0 |
| EENTER | 0x04 | 进入飞地 (Ring-3 -> Ring-3) | Ring-3 |
| EEXIT | 0x05 | 退出飞地 | Ring-3 |
| EREPORT | 0x06 | 生成本地认证报告 | Ring-3 |
| EGETKEY | 0x07 | 派生加密密钥 | Ring-3 |

### 2.3 MRENCLAVE 度量

MRENCLAVE 是飞地的"加密哈希"，用于唯一标识飞地的内容。

```
ECREATE:
  MRENCLAVE = SHA256_INITIAL  // 初始值为固定常量

EADD + EEXTEND (对每个页面):
  for each 256-byte chunk in page:
    MRENCLAVE = SHA256(MRENCLAVE || chunk)

EINIT:
  MRENCLAVE 变为不可变
  外部签名应该匹配 MRENCLAVE
```

### 2.4 飞地生命周期

```
    ┌──────────┐
    │ UNINIT   │  ECREATE
    └─────┬────┘
          │ EADD (多次，添加页面)
          │ EEXTEND (扩展度量)
          v
    ┌──────────┐
    │ BUILDING │ 
    └─────┬────┘
          │ EINIT (初始化，锁定度量)
          v
    ┌──────────┐
    │  READY   │
    └─────┬────┘
          │ EENTER (进入飞地)
          v
    ┌──────────┐   EEXIT (退出)
    │ RUNNING  │<---+
    └──────────┘    |
                    |
    EENTER (再次进入)
```

---

## 3. AMD SEV-SNP 架构

### 3.1 SEV 特性

| 特性 | SEV | SEV-ES | SEV-SNP |
|-----|-----|--------|---------|
| 内存加密 | 是 | 是 | 是 |
| 寄存器加密 | 否 | 是 | 是 |
| 完整性保护 | 否 | 否 | 是 |
| 远程认证 | 是 | 是 | 是 |
| 逆向页表 | 否 | 否 | 是 (RMP) |

### 3.2 RMP (Reverse Map Table)

SEV-SNP 使用 RMP 来防止 Hypervisor 恶意重映射内存：

```
RMP 条目:
  - GUEST_物理地址: 客户机的物理地址
  - SYSTEM_物理地址: 实际的系统物理地址
  - 页面状态: ASSIGNED / UNASSIGNED
  - 页面类型: NORMAL / VMSA
  - 验证位: 页面内容是否已验证
```

---

## 4. ARM TrustZone 架构

### 4.1 安全世界 vs 普通世界

```
普通世界 (Normal World)          安全世界 (Secure World)
┌─────────────────┐             ┌─────────────────┐
│  Rich OS (Linux)│             │  Trusted OS     │
│  ┌─────────────┐│             │  (Trusty, TEE)  │
│  │ App  App    ││             │  ┌─────────────┐│
│  │ App  App    ││             │  │TA  TA  TA   ││
│  └─────────────┘│             │  │(Trusted Apps)││
└────────┬────────┘             │  └─────────────┘│
         │                       └────────┬────────┘
         │                                │
    ┌────┴───────────────┐          ┌─────┴──────────────┐
    │  EL0 (User)        │          │  S-EL0 (Secure User)│
    ├────────────────────┤          ├────────────────────┤
    │  EL1 (Kernel)      │          │  S-EL1 (Secure OS)  │
    ├────────────────────┤          ├────────────────────┤
    │  EL2 (Hypervisor)  │          │  S-EL2 (Secure Part)│
    ├────────────────────┤          ├────────────────────┤
    │  EL3 (Secure Monitor) ◄────────┤                    │
    └────────────────────┘          └────────────────────┘
```

### 4.2 Monitor 模式 (EL3)

EL3 是 ARM 的"安全监视器"，控制着普通世界和安全世界之间的切换：

```
普通世界 -> Secure Monitor (SMC 指令) -> 安全世界
安全世界 -> Secure Monitor (SMC 指令) -> 普通世界
```

---

## 5. 认证 (Attestation)

### 5.1 本地认证 (Local Attestation)

两个飞地在同一平台上通过 EREPORT 互相验证：

```
飞地 A                                飞地 B
  |                                      |
  |-- EREPORT -> 本地认证报告 ------------->|
  |                                      |
  |<- EREPORT <- 本地认证报告 --------------|
  |                                      |
  MRENCLAVE_A == MRENCLAVE_A_expected?   MRENCLAVE_B == MRENCLAVE_B_expected?
```

### 5.2 远程认证 (Remote Attestation)

飞地向远程验证方证明其身份：

```
飞地 <---> QE (Quoting Enclave) <---> IAS (Intel Attestation Service) <---> 远程方
  |                                      |
EREPORT                    EPID 签名验证
  |                                      |
MRENCLAVE                  检查飞地是否在受信列表中
```

---

## 6. 侧信道攻击与飞地

### 6.1 Foreshadow (L1TF, CVE-2018-3615)

利用 L1 数据缓存来读取 SGX 飞地内容。攻击者可以在 L1 缓存未命中时，从 L1 缓存中读取到飞地的数据。

### 6.2 SGAxe (CVE-2020-0549)

利用缓存的共享性，从 CPU 核心的 L1 数据缓存中提取 SGX 飞地内的加密密钥（如 ECDSA 认证密钥）。

### 6.3 Plundervolt (CVE-2019-11157)

通过操纵 CPU 工作的电压（undervolting）来在飞地内诱导确定性错误，从而打破 RSA-CRT 或 AES 加密的安全性。

### 6.4 防御现状

| 攻击 | Intel 修复 | OS 修复 |
|-----|----------|--------|
| Foreshadow/L1TF | 微代码 (L1D flush) | 内核防御 |
| SGAxe | 微代码更新 | - |
| Plundervolt | 硬件修复 | BIOS 更新 |
| MicroScope | - | 代码审计 |

---

## 7. 密封 (Sealing)

飞地可以使用 EGETKEY 指令派生密钥来加密（密封）数据以供持久存储。

### 7.1 密封策略

| 策略 | 密钥派生 | 适用场景 |
|-----|---------|---------|
| MRENCLAVE | MRENCLAVE 哈希 | 飞地版本一致性 |
| MRSIGNER | 签名者公钥 | 跨飞地版本 |
| MISCFLAGS | 属性掩码 | 选择性共享 |

```
EGETKEY 输入:
  KeyID = SEAL_KEY
  KeyPolicy = MRENCLAVE (或 MRSIGNER)
  OwnerEpoch = 平台所有者值

输出:
  128-bit AES seal key
```

---

## 8. 本模块实现

本模块 (`secure_enclave.h/c`) 提供了：

- `enclave_create()`: 创建飞地
- `enclave_add_page()`: 添加 EPC 页面
- `enclave_measure()`: SHA256 扩展度量
- `enclave_init()`: 初始化飞地
- `enclave_enter()` / `enclave_exit()`: 进入/退出飞地
- `enclave_attest()`: 本地认证
- `enclave_seal_data()` / `enclave_unseal_data()`: 密封/解封数据
- 内置 SHA256 哈希实现
- 基于 XOR 的页面加密模型

---

## 参考资料

- Intel SGX Developer Reference (329298-001US)
- AMD SEV-SNP: Strengthening VM Isolation with Integrity Protection
- ARM TrustZone Technology for ARMv8-A
- RISC-V Keystone: Open-Source Secure Enclave Framework
- Lipp et al. (2018): Foreshadow: Extracting the Keys to the Intel SGX Kingdom
- MIT 6.5950: Hardware Security, Lecture 14-18
