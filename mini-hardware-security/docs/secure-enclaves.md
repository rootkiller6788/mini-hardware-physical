# 安全飞地 (Secure Enclaves) — 可信执行环境综述

> 深入分析 Intel SGX、AMD SEV-SNP、ARM TrustZone、RISC-V Keystone 的安全模型与攻击面

---

## 1. 可信执行环境 (TEE) 概述

可信执行环境 (Trusted Execution Environment, TEE) 是在计算设备中提供一个
与操作系统/虚拟机监控器隔离开的安全区域，用于保护敏感数据和代码。

### TEE 核心安全属性

| 属性 | 描述 |
|-----|------|
| 内存隔离 | OS/Hypervisor 无法访问 TEE 内存 |
| 内存加密 | 外部总线观察者无法窃听 |
| 完整性保护 | 无法篡改 TEE 内存内容 |
| 认证 (Attestation) | 证明 TEE 身份的加密证据 |
| 密封 (Sealing) | 持久存储加密数据 |
| 安全启动 | 证明 TEE 代码未被篡改 |

---

## 2. Intel SGX 架构详解

### 2.1 保护域 (Protection Domains)

```
一般进程 (Ring-3)           SGX 飞地 (Ring-3)           内核 (Ring-0)
┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐
│ App              │    │ Enclave Code      │    │ OS/VMM            │
│ - malloc()       │    │ - EEENTER 进入    │    │ - ECREATE/EADD    │
│ - 网络 I/O       │    │ - 飞地内部计算    │    │ - 内存管理        │
│ - 文件 I/O       │    │ - EEXIT 退出      │    │ - EPC 驱逐        │
│ - printf()       │    │ - EREPORT 认证    │    │ - 创建 EPC        │
└──────────────────┘    └──────────────────┘    └──────────────────┘
    │                        │                        │
    │                        │   EPC                  │
    └────────────────────────┼────────────────────────┘
                             │
                    ┌────────▼────────┐
                    │  Hardware MEE    │
                    │  (内存加密引擎)  │
                    │  - AES-GCM 加密  │
                    │  - 完整性树      │
                    │  - 版本计数器    │
                    └─────────────────┘
```

### 2.2 EPC (Enclave Page Cache)

| 参数 | 值 |
|-----|---|
| EPC 大小 (SGXv1) | 128 MB (94 MB 可用) |
| EPC 大小 (SGXv2) | 1 TB |
| 页面大小 | 4 KB |
| 加密算法 | AES-GCM (128 位密钥) |
| 元数据每页 | 16 字节版本数组 + 完整性树 |
| 驱逐 | OS 驱逐到普通内存 (加密后) |

### 2.3 MEE (Memory Encryption Engine)

```
内存请求 -> 地址检查
               |
              EPC 页面?
               |
        ┌──────┴──────┐
        │              │
      是              否
        │              │
       ▼              ▼
    加密/解密       直通 (无加密)
    (AES-GCM)
        │
      完整性验证
    (Merkle Tree)
```

---

## 3. AMD SEV-SNP 架构

### 3.1 安全模型

| 组件 | 是否可信 |
|-----|---------|
| AMD CPU / AMD-SP | 是 |
| Hypervisor | 否 |
| 其他 VM | 否 |
| 固件 | 否 (受 PSP 限制) |

### 3.2 内存加密 (SME)

```
  Virtual Address
        │
        v
   页表 (嵌套分页)
        │
        v
    系统物理地址
        │
        ├──> C-bit = 1? (加密页面?)
        │
    ┌───┴───┐
    │       │
   是       否
    │       │
    v       v
  AES-128  直通 (不加密)
  加密
    │
    v
   内存控制器
```

### 3.3 SEV 密钥管理

| 密钥类型 | 用途 |
|---------|------|
| CEK (Chip Endorsement Key) | 芯片制造时植入的根密钥 |
| PEK (Platform Endorsement Key) | 平台特定密钥 |
| PDH (Platform Diffie-Hellman) | 安全密钥交换 |
| OCA (Owner Certificate Authority) | 客户管理的根密钥 |

---

## 4. ARM TrustZone 架构

### 4.1 安全状态

```
            非安全世界          安全世界
           (NS 位 = 1)        (NS 位 = 0)
                │                  │
    ┌───────────┤         ┌────────┤
    │           │         │        │
    ▼           ▼         ▼        ▼
  EL0 (App)  EL1 (OS)  S-EL0     S-EL1
                          (TA)   (Trusty OS)
    │           │         │        │
    └───────────┴─────────┴────────┘
                    │
              ┌─────┴─────┐
              │  EL2 (Hyper)│  (可选)
              └─────┬─────┘
                    │
              ┌─────┴─────┐
              │  EL3 (Secure)│  Monitor
              └───────────┘
```

### 4.2 TrustZone 地址空间控制器 (TZASC)

TZASC 按内存区域定义安全属性。只有在安全世界才能访问标记为 "安全" 的内存区域。

---

## 5. RISC-V Keystone

### 5.1 PMP 基础安全模型

RISC-V 通过物理内存保护 (PMP) 实现隔离：

```
PMP 条目 (每条):
  - 地址范围 (基地址 + 大小)
  - R/W/X 权限
  - 锁定位 (L): 设置后不可修改
  - 地址匹配模式
```

Keystone 利用 PMP + 监控器模式 (M-mode) 实现飞地。

---

## 6. 认证 (Attestation)

### 6.1 Intel SGX 远程认证流程

```
步骤 1: 智能合约挑战
  外部方  -----> 应用程序 -----> 飞地
          (nonce)
          
步骤 2: 本地认证
  应用程序飞地 <--EREPORT--> Quoting Enclave (QE)
          
步骤 3: 报价生成
  QE 使用 EPID/ECDSA 密钥签名:
    QUOTE = Sign(MRENCLAVE, nonce, 飞地公钥)
    
步骤 4: 认证验证
  QUOTE -----> Intel IAS (Intel Attestation Service)
  IAS 验证: EPID 签名是否有效
  IAS 检查: 飞地身份是否在受信列表中
  
步骤 5: 远程方验证
  远程方收到 IAS 签名的认证报告
```

### 6.2 AMD SEV 认证

```
AMD-SP (平台安全处理器) 生成认证报告:
  ATTESTATION_REPORT = Sign(MEASUREMENT || nonce || POLICY)
  
远程方直接与 AMD KDS (Key Distribution Service) 验证:
  KDS 验证 SEV 证书链 (CEK -> PEK -> VCEK)
```

---

## 7. 侧信道攻击面

### 7.1 飞地侧信道分类

| 攻击类别 | 示例 | 泄露通道 |
|---------|------|---------|
| 页表攻击 | Controlled-Channel | 页故障 + A/D 位 |
| 分支预测器 | BranchScope | BTB/PHT |
| 缓存攻击 | CacheZoom | L1/L2/L3 缓存 |
| DRAM 攻击 | DRAMA | 行缓冲区 |
| 电压控制 | Plundervolt | 计算错误 |
| SMT 攻击 | PortSmash | 执行端口争用 |
| 瞬态执行 | Foreshadow/LVI | L1 缓存 |

### 7.2 Foreshadow (L1 Terminal Fault) 攻击

```
1. 攻击者使用 TSX (事务内存) 抑制异常
2. 在事务中访问 SGX 飞地的 EPC 页面
3. 访问触发终端故障 (L1 数据未命中)
4. 在故障处理前，L1 缓存中的残留数据被推测性地读取
5. 结果：从 L1 缓存中泄露飞地数据 (包括 EPID 密钥!)
```

### 7.3 Plundervolt 攻击

```
1. 攻击者通过 MSR (模型特定寄存器) 界面控制 CPU 电压
2. 在特定时机降低电压 -> 诱导确定性计算错误
3. 利用错误破坏飞地内的加密操作
4. 例如：RSA-CRT 签名中的 Sp 被故障 -> 恢复私钥
```

### 7.4 Controlled-Channel 攻击

```
飞地内部访问外部内存 (non-EPC) 时:
  -> 触发页故障
  -> OS 获得控制权
  -> OS 观察故障地址和类型
  -> OS 推断飞地的控制流 (哪条指令、哪个函数)

信息泄露: 地址访问模式 -> 控制流序列 -> 秘密推断
```

---

## 8. 防御策略

### 8.1 飞地侧信道防御

| 防御 | 针对 | 适用平台 | 效果 |
|-----|------|---------|------|
| Oblivious RAM (ORAM) | 地址模式泄露 | SGX, Keystone | 高 (性能代价大) |
| 常数时间代码 | 时序泄露 | 所有 | 中-高 |
| DRAM 预取 | 缓存攻击 | SGX | 低 |
| 页内加密 | 冷启动攻击 | SGX | 高 |
| 硬件 TSC 过滤 | 时序攻击 | SGXv2 | 中 |
| BTI 防御 (lfence/pause) | 分支预测攻击 | 所有 | 中 |

### 8.2 Hypervisor 防御

| 防御 | 描述 |
|-----|------|
| SEV-SNP RMP | Hypervisor 无法重新映射飞地内存 |
| EPC 版本数组 | 防止回滚旧 EPC 页面 |
| BIOS 锁定 | 限制 MSR 访问 (电压、频率等) |

---

## 9. 认证可信根

### 9.1 证明链

```
芯片制造商
  │
  v
制造时的根密钥 (Fuse ROM)
  │
  v
Boot ROM (可信代码)
  │
  v
安全世界/飞地
  │
  v
远程方 (验证)
```

### 9.2 证明证书链

```
根证书 (Intel 或 AMD 根CA)
  │
  v
平台证书 (特定 CPU)
  │
  v
报价/认证报告
  │
  v
飞地身份 (MRENCLAVE)
```

---

## 10. 未来方向

### 10.1 保密计算 (Confidential Computing)

| 技术 | 提供者 | 状态 |
|-----|--------|------|
| Intel TDX | Intel | 4th Gen Xeon 可用 |
| AMD SEV-SNP | AMD | EPYC 3rd Gen 可用 |
| ARM CCA | ARM | ARMv9 可用 |
| IBM Ultravisor | IBM | POWER10 可用 |
| RISC-V CoVE | 开源社区 | 标准制定中 |

### 10.2 研究前沿

- 形式化验证飞地代码的正确性和安全性
- 飞地操作系统 (Graphene-SGX, Occlum)
- 分布式飞地计算 (跨机器飞地通信)
- PUF 与 TEE 结合的硬件根信任
- 侧信道免疫的飞地设计

---

## 参考资料

- Intel SGX Developer Guide (329298-001US)
- AMD SEV API Version 0.24
- ARM TrustZone Base System Architecture (ARM DEN 0029A)
- RISC-V Keystone: Open-Source Secure Enclave Framework
- Van Bulck et al. (2018): Foreshadow: Extracting the Keys to the Intel SGX Kingdom
- Costan & Devadas (2016): Intel SGX Explained
- Murdock et al. (2020): Plundervolt: Software-based Fault Injection Attacks against Intel SGX
- MIT 6.5950: TEE and Trusted Computing Lecture Notes
