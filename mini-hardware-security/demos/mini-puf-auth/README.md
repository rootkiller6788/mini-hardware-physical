# Mini PUF Auth — 物理不可克隆函数与认证协议详解

> 本文档深入讲解 PUF (Physical Unclonable Function) 的原理、实现与安全认证协议

---

## 1. 概述 (Overview)

物理不可克隆函数 (PUF) 是一种利用集成电路制造过程中的物理随机变化来产生设备唯一"指纹"的硬件原语。由于制造过程不可控的微观差异，每个芯片在物理上都是唯一的，这种唯一性可以通过 PUF 来提取和验证。

### PUF 关键属性

| 属性 | 描述 | 典型值 |
|-----|------|-------|
| 唯一性 (Uniqueness) | 不同设备对同一挑战的响应差异 | 50% 理想 |
| 可靠性 (Reliability) | 同一设备同一挑战的响应稳定性 | >95% |
| 不可克隆性 | 无法制造具有相同响应的芯片 | 物理保证 |
| 不可预测性 | 给定已知 CRP 无法预测新 CRP | 理论保证 |

---

## 2. 制造工艺变化与 PUF 物理基础

### 2.1 SRAM 单元的阈值电压差异

SRAM 单元由 6 个晶体管组成，两个交叉耦合的反相器处于亚稳态时，
微小的电压差异决定了单元上电后趋近于 0 还是 1。

```
SRAM 单元 (6T):
       VDD
        |
       ┴┴
        |            M2
  M3  |─┤─|  M4
  |   |  |  |
  X---┤  ├──Y
  |   |  |
  M1  |  |
  |   |  |
       ┘┘
        |
       GND

上电过程: 交叉耦合反相器从亚稳态趋向稳定
  → M1 驱动强于 M3 → 单元趋近于 0
  → M3 驱动强于 M1 → 单元趋近于 1
  → 差异由随机掺杂决定 (不可控)
```

### 2.2 延迟路径 (Arbiter PUF 基础)

Arbiter PUF 基于两个竞争信号的路径延迟差异。

```
Challenge bits: C0, C1, C2, ..., C127

Input ──┬──[SW0]──[SW1]──[SW2]──...──[SW127]──┬──┐
         │                                     │  │
         │                                     │  │  Arbiter
         │                                     │  │   ║
         │                                     │  │   ║
         │                                     │  │   ║
         └──[SW0]──[SW1]──[SW2]──...──[SW127]──┘──┘    Response (0/1)

每个 SW 由 challenge bit 控制走上方还是下方路径
路径延迟差异由工艺变化决定
Arbiter 判断哪个信号先到达 (0 = 上方先到, 1 = 下方先到)
```

### 2.3 环形振荡器 (Ring Oscillator PUF)

```
RO1: NOT -> NOT -> NOT -> NOT -> NOT -> ... (奇数个反相器)
RO2: NOT -> NOT -> NOT -> NOT -> NOT -> ...

频率差异由工艺变化决定
challenge bits 选择哪对 RO 比较频率
Response = f(RO1) > f(RO2) ? 1 : 0
```

---

## 3. PUF 类型详解

### 3.1 SRAM PUF

| 特性 | 值 |
|-----|---|
| 原理 | SRAM 上电状态 |
| 挑战空间 | 无 (固定挑战) |
| 响应比特数 | 取决于 SRAM 大小 |
| 可靠性 | 良好 (0.7-3% 错误率) |
| 抗老化 | 良好 |

### 3.2 Arbiter PUF

| 特性 | 值 |
|-----|---|
| 原理 | 信号路径延迟 |
| 挑战空间 | 2^n (n = 级数) |
| 响应比特数 | 1 |
| 可靠性 | 中 (1-5% 错误率) |
| 抗建模 | 弱 (易受机器学习攻击) |

### 3.3 Ring Oscillator PUF

| 特性 | 值 |
|-----|---|
| 原理 | RO 频率比较 |
| 挑战空间 | 中等 |
| 响应比特数 | 1 |
| 可靠性 | 良好 (0.5-2% 错误率) |
| 抗老化 | 中 |

---

## 4. 挑战-响应认证协议 (CRP-Based Authentication)

### 4.1 基本协议

```
                                挑战 C
  设备 (PUF)                           ──────────────────►   服务器 (数据库)
  ┌──────────┐                                               ┌──────────┐
  │ R=PUF(C) │                                               │ CRP 数据库 │
  │          │  响应 R                                       │ C1→R1    │
  │          │ ◄──────────────────                           │ C2→R2    │
  │          │                                               │ C3→R3    │
  │          │  验证: R' ≈ PUF(C) 接受在阈值内               │ ...      │
  └──────────┘                                               └──────────┘
```

### 4.2 认证阈值计算

```
Hamming 距离 (HD):
  HD(A, B) = 两个二进制字符串之间不同的比特数

认证决策:
  如果 HD(claimed_response, expected_response) < threshold:
    -> 接受 (设备认证成功)
  否则:
    -> 拒绝 (设备可能克隆)
```

---

## 5. 模糊提取器 (Fuzzy Extractor)

### 5.1 为什么需要模糊提取器

PUF 的响应不是完全可重复的（存在噪声）。模糊提取器将带有噪声的 PUF 响应转换为可重复的加密密钥。

### 5.2 模糊提取器工作流程

```
注册阶段 (Enrollment):
  Enrollment
    Response_R ← PUF(C)
    (Key_K, Helper_Data_P) ← Gen(R)
    存储: (Key_K 用于加密, Helper_Data_P 公开存储)
    销毁: Response_R

重建阶段 (Reconstruction):
  Reconstruction
    Noisy_R' ← PUF(C)  (同一挑战 C)
    Recovered_Key_K ← Rep(R', P)
    使用 Recovered_Key_K 进行解密
```

### 5.3 实现 (Code Sketch 和 Secure Sketch)

```c
// Gen 函数 (注册)
// 输入: PUF 响应 R
// 输出: 密钥 K, 辅助数据 P
void Gen(uint8_t *R, uint8_t *K, uint8_t *P, int n) {
    // 1. 生成随机密钥 K
    random_key(K);
    
    // 2. 编码 K 为码字
    uint8_t codeword[256];
    encode(K, codeword);  // BCH, Reed-Solomon, 等纠错码
    
    // 3. 计算辅助数据 P = R XOR codeword
    for (int i = 0; i < n; i++) {
        P[i] = R[i] ^ codeword[i];
    }
    
    // 4. 销毁原始 R
}

// Rep 函数 (重建)
// 输入: 噪声响应 R', 辅助数据 P
// 输出: 恢复的密钥 K
void Rep(uint8_t *R_prime, uint8_t *P, uint8_t *K, int n) {
    // 1. 恢复可能错误码字: codeword' = R' XOR P
    uint8_t codeword_prime[256];
    for (int i = 0; i < n; i++) {
        codeword_prime[i] = R_prime[i] ^ P[i];
    }
    
    // 2. 解码纠错码
    decode(codeword_prime, K);  // 错误纠正
    
    // 3. 返回恢复的密钥 K
}
```

### 5.4 Helper Data 安全分析

辅助数据 P 是公开的，但不会直接泄露密钥信息，因为：

- P = R XOR codeword
- 没有 R 的知识，P 看起来是随机的
- 纠错码的冗余信息有限
- 攻击者需要同时知道 P 和接近 R 的响应才能恢复密钥

---

## 6. 基于 PUF 的密钥生成

### 6.1 密钥生成协议

```
步骤 1: 设备上电，SRAM PUF 产生初始响应 (256 bits)
步骤 2: 模糊提取器 Gen() 生成密钥 K 和辅助数据 P
步骤 3: 辅助数据 P 存储在非易失性存储器 (NVM) 中
步骤 4: 密钥 K 被传递给加密引擎 (AES, etc.)
步骤 5: 设备断电时，密钥在内存中消失（安全）
步骤 6: 下次上电时，Rep() 从 PUF 响应和辅助数据中恢复 K
```

### 6.2 安全优势

| 特性 | 传统密钥 (EEPROM) | PUF 派生密钥 |
|-----|-----------------|------------|
| 静态存在 | 总是可读取 | 仅上电时存在 |
| 物理攻击 | 微探针可读取 | 微探针破坏 PUF 响应 |
| 克隆攻击 | 复制 EEPROM 即可 | 无法物理复制 |
| 逆向工程 | 显微镜可读 | 无法确定 PUF 响应 |

---

## 7. 攻击与防御

### 7.1 建模攻击 (Modeling Attack)

攻击者收集大量 CRP 对，训练机器学习模型预测未见的响应。

**防御方法**:
- XOR Arbiter PUF: 多个 Arbiter PUF 的 XOR 增加非线性
- 混淆逻辑: 在 Arbiter PUF 输出前添加非线性逻辑
- 限制 CRP 访问次数

### 7.2 侧信道攻击

通过功耗分析或电磁辐射获取 PUF 响应信息。

**防御方法**:
- 恒定时间评估
- 功耗平衡电路

### 7.3 失效分析攻击

使用显微镜或化学方法逆向芯片提取 PUF 信息。

**防御方法**:
- 物理防护层 (Shielding)
- 触发式自毁

---

## 8. 性能指标

### 8.1 类内汉明距离 (Intra-HD)

同一设备对同一挑战的多次测量的汉明距离分布。

```
理想值: 0% (完全重合)
典型值: 1-5%
标准: <10% 为可接受
```

### 8.2 类间汉明距离 (Inter-HD)

不同设备对同一挑战的汉明距离分布。

```
理想值: 50% (完全随机)
典型值: 40-50%
标准: >40% 为可接受
```

### 8.3 误比特率 (Bit Error Rate)

同一设备多次测量的平均比特错误率：

```
BER = (错误比特数) / (总比特数) × 100%
```

---

## 9. 本模块实现

本模块 (`puf.h/c`) 提供了：

- `puf_sram_init()`: SRAM PUF 初始化
- `puf_arbiter_challenge()`: Arbiter PUF 挑战响应
- `puf_get_response()`: 通用响应生成
- `puf_noise_simulate()`: 噪声仿真
- `puf_authenticate()`: 基于阈值的认证
- `puf_key_generate()`: 密钥生成
- `puf_enroll()` / `puf_reconstruct()`: 模糊提取器注册/重建
- 汉明距离计算

---

## 10. 运行演示

```bash
make puf_demo
./bin/puf_demo
```

---

## 参考资料

- Pappu et al. (2002): Physical One-Way Functions (首次提出 PUF 概念)
- Gassend et al. (2002): Silicon Physical Random Functions (硅 PUF)
- Holcomb et al. (2008): Power-Up SRAM State as an Identifying Fingerprint (SRAM PUF)
- Dodis et al. (2004): Fuzzy Extractors (模糊提取器理论)
- MIT 6.5950: Hardware Security, Lecture 12
