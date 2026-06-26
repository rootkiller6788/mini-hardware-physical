# 课程对齐 (Course Alignment) — mini-hardware-security

> 本模块与 MIT 6.5950 Hardware Security、UC Berkeley CS261、Stanford CS356 的对齐映射

---

## MIT 6.5950: Hardware Security (Spring 2024)

| MIT 6.5950 章节 | 内容 | 本模块组件 | 文件 |
|----------------|------|-----------|------|
| Ch 1-2: Introduction, Physical Attacks | 硬件安全概述、物理攻击向量 | 故障注入 | `fault_injection.h/c` |
| Ch 3: Invasive Attacks | 芯片逆向工程、探针攻击 | 故障注入 (激光) | `fault_injection.h/c` |
| Ch 4: Side Channel Basics | 侧信道基础、功耗模型 | 侧信道模拟器 | `side_channel.h/c` |
| Ch 5: Timing Attacks | 时序攻击、缓存时序 | 缓存攻击 | `cache_attack.h/c` |
| Ch 6: Simple Power Analysis | SPA 基础、功耗轨迹 | 简单功耗分析 | `side_channel.h/c` |
| Ch 7: Differential Power Analysis | DPA 原理与实现 | DPA 分析 | `side_channel.h/c` |
| Ch 8: Speculative Execution Attacks | Spectre, Meltdown | 推测执行攻击 | `spec_exec_atk.h/c` |
| Ch 9: Fault Injection Attacks | 故障注入、CRT 攻击 | 故障注入 | `fault_injection.h/c` |
| Ch 10: Countermeasures Overview | 对抗措施综述 | 常数时间代码示例 | demo READMEs |
| Ch 11: Cache Side-Channel Defense | 缓存分区、CAT、预取防御 | 缓存攻击对抗 | `cache_attack.h/c` |
| Ch 12: Physical Unclonable Functions | PUF 原理与应用 | PUF 模块 | `puf.h/c` |
| Ch 14: TEE and SGX | 可信执行环境 | 安全飞地 | `secure_enclave.h/c` |
| Ch 15: SGX Side Channels | SGX 侧信道 (Foreshadow, SGAxe) | 飞地安全分析 | demo README |
| Ch 16: Remote Attestation | 远程认证 | 飞地认证 | `secure_enclave.h/c` |

---

## UC Berkeley CS261: Security in Computer Systems (Spring 2024)

| Berkeley CS261 主题 | 内容 | 本模块组件 |
|-------------------|------|-----------|
| Threat Models | 威胁建模 | `side_channel.h`, 文档 |
| Information Flow | 信息流分析 | 侧信道信息泄露模型 |
| H/W Security | 硬件安全原语 | PUF, 安全飞地 |
| Intel SGX | SGX 架构详解 | `secure_enclave.h/c`, demo README |
| Cache Attacks | 缓存攻击全分析 | `cache_attack.h/c`, demo README |
| Speculative Execution | 推测执行安全 | `spec_exec_atk.h/c`, demo README |
| Foreshadow | SGX 熔断攻击 | demo README (secure enclave) |
| Cryptographic Engineering | 密码学工程 | `fault_injection.h/c` (RSA-CRT) |

---

## Stanford CS356: Topics in Computer and Network Security (Spring 2024)

| Stanford CS356 主题 | 内容 | 本模块组件 |
|-------------------|------|-----------|
| Side Channels | 侧信道综述 | `side_channel.h/c` |
| Timing & Cache Attacks | 时序和缓存攻击 | `cache_attack.h/c` |
| Trusted Computing | 可信计算 | `secure_enclave.h/c` |
| Hardware Roots of Trust | 硬件信任根 | PUF, 认证 |
| Embedded Security | 嵌入式安全 | 全部模块 |
| Cryptographic Hardware | 密码硬件 | `fault_injection.h/c` |

---

## 模块功能矩阵

| 模块 | 攻击类型 | 安全原语 | 认证 | 课程映射 |
|-----|---------|---------|------|---------|
| `side_channel` | 时序、功耗 | DPA/SPA | - | MIT 6.5950 Ch4-7 |
| `cache_attack` | Flush+Reload, Prime+Probe | - | - | MIT 6.5950 Ch5 |
| `spec_exec_atk` | Spectre v1/v2/v4, Meltdown | - | - | MIT 6.5950 Ch8 |
| `secure_enclave` | 飞地侧信道 | SGX/TEE | 本地/远程认证 | MIT 6.5950 Ch14-16 |
| `puf` | 建模攻击 | SRAM/Arbiter PUF | CRP 认证 | MIT 6.5950 Ch12 |
| `fault_injection` | 时钟/电压/激光/RSA-CRT | - | - | MIT 6.5950 Ch3,9 |

---

## 关键技术术语对齐

| 英文术语 | 中文术语 | 本模块使用 |
|---------|---------|-----------|
| Side-Channel Attack | 侧信道攻击 | 侧信道攻击模拟器 |
| Cache Timing | 缓存时序 | 缓存攻击 |
| Speculative Execution | 推测执行 | 推测执行攻击 |
| Branch Target Buffer | 分支目标缓存 | BTB 条目 |
| Secure Enclave | 安全飞地 | Intel SGX 模拟 |
| Physical Unclonable Function | 物理不可克隆函数 | PUF |
| Fuzzy Extractor | 模糊提取器 | 密钥注册/重建 |
| Fault Injection | 故障注入 | RSA-CRT Bellcore 攻击 |
| Attestation | 认证 | 本地/远程认证 |
| Sealing | 密封 | EGETKEY 密封 |
| Challenge-Response Pair | 挑战-响应对 | CRP 认证 |
| Hamming Distance | 汉明距离 | PUF 认证度量 |
| Cache Line | 缓存行 | 缓存攻击粒度 |
| Enclave Page Cache | 飞地页面缓存 | EPC |
| Store-to-Load Forwarding | 存储转发 | 推测存储绕过 |
| Constant-Time Programming | 常数时间编程 | 防御技术 |
