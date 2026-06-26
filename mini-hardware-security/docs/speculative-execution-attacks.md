# 推测执行攻击 (Speculative Execution Attacks)

> Spectre、Meltdown 及相关微架构侧信道攻击的全面分析

---

## 1. 根因分析 (Root Cause)

现代高性能处理器使用**乱序执行** (Out-of-Order Execution) 和**推测执行** (Speculative Execution)
来提高指令级并行度 (ILP)。这些机制产生**微架构副作用** (Microarchitectural Side Effects)，
在推测执行被回滚后仍然存在于系统中。

### 核心漏洞机制

```
1. CPU 推测性地执行未授权的指令
2. 推测执行修改微架构状态（缓存、BTB、预测器等）
3. 发现推测错误后，架构状态回滚
4. 但微架构状态**不回滚**
5. 攻击者通过测量微架构副作用（如缓存访问时间）恢复秘密
```

---

## 2. 攻击时间线 (Attack Timeline)

```
2018-01-03: Spectre v1, v2 (Google Project Zero)
2018-01-03: Meltdown (CVE-2017-5754)
2018-03-20: Spectre-NG (Spectre Next Generation)
2018-05-21: Spectre v4 (Speculative Store Bypass, CVE-2018-3639)
2018-06-14: TLBleed
2018-07-10: NetSpectre (网络远程 Spectre)
2018-08-14: Foreshadow/L1TF (CVE-2018-3615, 3620, 3646)
2018-09-12: SpectreRSB (Return Stack Buffer)
2018-11-13: PortSmash (SMT 侧信道)
2019-05-14: MDS 系列 (ZombieLoad, RIDL, Fallout, CVE-2018-12126/27/30)
2019-06-18: TAA (TSX Asynchronous Abort, CVE-2019-11135)
2019-12-10: Plundervolt (CVE-2019-11157)
2020-01-27: CacheOut (CVE-2020-0549)
2020-03-10: LVI (Load Value Injection, CVE-2020-0551)
2020-11-10: Platypus
```

---

## 3. Spectre v1: Bounds Check Bypass

### 3.1 攻击原理图

```
步骤 0: 设定
  array1_size = 16
  array1[0..15] = 合法数据
  array1[16..] = 越界区域 (秘密数据)
  array2[256 * 4096] = 探测数组

步骤 1: 训练 BTB (重复 5 次)
  for x in 0..4:
    if (x < 16) {           // BTB 学: 此分支总是 TAKEN
        y = array2[array1[x] * 4096];
    }

步骤 2: 刷新缓存
  clflush(array2[0..255])  // 清除 array2 从缓存

步骤 3: 攻击 (恶意的 x)
  x = 20  // 越界 (x >= 16)
  
  // 指令流水线:
  // Cycle 1: cmp x, 16          (开始边界检查)
  // Cycle 2: BTB 预测 TAKEN     (预测-在比较结果确认前)
  // Cycle 3: load array1[20]    (推测执行-越界访问!)
  // Cycle 4: shl value, 12      (value * 4096)
  // Cycle 5: load array2[value*4096] (推测执行-缓存加载)
  // Cycle 6: cmp 结果: x >= 16   (边界检查失败)
  // Cycle 7: 回滚 (ROLLBACK)

步骤 4: 测量缓存访问时间
  for i in 0..255:
    t = measure(array2[i * 4096])
    if t < THRESHOLD:
      secret_byte = i

步骤 5: 重复 256 次泄露整个数组
```

---

## 4. Spectre v2: Branch Target Injection

### 4.1 间接跳转机制

```asm
; 受害代码 (内核)
victim:
    jmp    *%rax          ; 间接跳转 (目标地址在寄存器)
                          ; BTB 预测: jmp *%rax -> 上次的目标地址

; 攻击者代码
attacker:
    mov    $0xaddr, %rax   ; 设置相同的寄存器值
    jmp    *%rax           ; 训练 BTB: 将 (jmp*%rax) 映射到攻击者 gadget
```

### 4.2 攻击流程

```
1. 攻击者在用户空间执行: jmp *%rax -> gadget_addr
   -> BTB 记录: 当执行 jmp *%rax 时，目标为 gadget_addr
   
2. 攻击者促使内核执行: jmp *%rax (间接跳转)
   -> BTB 预测: 目标 = gadget_addr (来自步骤 1 的污染)
   -> CPU 推测地执行 gadget_addr 的代码
   
3. Gadget 在推测中读取秘密数据并加载到缓存

4. 内核发现错误预测 -> 回滚

5. 攻击者通过 Flush+Reload 恢复秘密
```

### 4.3 Retpoline 防御

```asm
; Retpoline: 用 return 替换 indirect jmp
; 因为 return 由 RSB 预测而非 BTB
jmp retpoline_r11
...
retpoline_r11:
    call   retpoline_setup    ; push next_addr 到 RSB
retpoline_setup:
    pause
    lfence
    mov    %r11, (%rsp)       ; 覆盖返回地址
    ret                       ; 使用 RSB 而不是 BTB
```

---

## 5. Meltdown (CVE-2017-5754)

### 5.1 原理

Meltdown 利用 "异常抑制" (exception suppression) 突破用户态和内核态的内存隔离。

```c
// Meltdown 核心代码 (概念性)
uint8_t secret = *(uint8_t *)kernel_address;   // 行 1: 触发 #PF 异常
int idx = secret * 4096;                       // 行 2: 将秘密值编码为缓存索引
uint8_t junk = probe_array[idx];               // 行 3: 缓存加载 (使用秘密值)
```

```
执行时序:
  T1: 指令 1 发出 load 请求 (内核地址)
  T2: MMU 检测到权限违规 -> 排队 #PF 异常
  T3: 但在异常递交前，CPU 推测性地执行指令 2, 3
  T4: 指令 2: CPU 使用读回的秘密值计算 idx
  T5: 指令 3: CPU 加载 probe_array[idx] -> 缓存中!
  T6: #PF 异常被递交 -> 架构状态回滚
  T7: 但 probe_array[secret*4096] 仍然在缓存中!
```

### 5.2 影响

| 平台 | 受影响 |
|-----|-------|
| Intel x86 (2010-) | 完全受影响 |
| ARM Cortex-A75 | 受影响 |
| IBM POWER | 受影响 |
| AMD (Zen+) | 基本不受影响 (不同的页表步进处理) |

### 5.3 KPTI 防御

KPTI (Kernel Page-Table Isolation) 在用户态时取消映射内核页面：

```
无 KPTI:
  用户态页表: [用户页面] [内核页面 (仅 supervisor 可访问)]
  攻击: 推测性地读取内核页面

有 KPTI:
  用户态页表: [用户页面] [最小 trampoline 页面]
  内核态页表: [用户页面] [完整内核页面]
  攻击: 推测性读取无法访问内核页面 (不在页表中!)
```

---

## 6. MDS 系列 (Microarchitectural Data Sampling)

### 6.1 攻击类型

| 攻击 | CVE | 泄露源 |
|-----|-----|-------|
| ZombieLoad | CVE-2018-12130 | Fill Buffer (行填充缓冲) |
| RIDL | CVE-2018-12127 | LFBS (加载端口缓冲区) |
| Fallout | CVE-2018-12126 | Store Buffer (存储缓冲) |
| TAA | CVE-2019-11135 | TSX 中的异步缓冲区 |

### 6.2 共同原理

所有 MDS 攻击利用以下微架构结构存储了之前操作的数据：

```
    CPU 核心
    ┌──────────────────────┐
    │  执行端口            │
    │  ┌───┐ ┌───┐         │
    │  │AL0│ │AL1│ ...      │
    │  └───┘ └───┘         │
    │                      │
    │  加载缓冲区 LFBS     │  ◄── 保留先前 load 的值
    │  ┌───┐ ┌───┐         │
    │  │LB0│ │LB1│ ...      │
    │  └───┘ └───┘         │
    │                      │
    │  存储缓冲区 SB       │  ◄── 保留先前 store 的值
    │  ┌───┐ ┌───┐         │
    │  │SB0│ │SB1│ ...      │
    │  └───┘ └───┘         │
    │                      │
    │  行填充缓冲 LFB      │  ◄── 保留从缓存/内存读取的值
    │  ┌───┐ ┌───┐         │
    │  │LFB│ │LFB│ ...      │
    │  └───┘ └───┘         │
    └──────────────────────┘
```

---

## 7. Foreshadow / L1TF (L1 Terminal Fault)

### 7.1 攻击 SGX 飞地

Foreshadow 专门针对 Intel SGX 飞地进行攻击：

```
1. 攻击者生成访问 SGX EPC 页面的 load 指令
2. 该访问触发终端故障 (L1 数据缓存未命中)
3. 在故障处理前，L1 缓存中的数据被推测性地读取
4. 如果 L1 缓存中恰好有飞地的数据 -> 秘密泄露!
5. L1D flush (在 EENTER/EEXIT 时刷新 L1D) 是为防御
```

---

## 8. 防御措施总表

| 防御 | 针对 | 硬件/软件 | 性能影响 |
|-----|------|---------|---------|
| Retpoline | Spectre v2 | 软件 | 0-10% |
| IBRS/IBPB | Spectre v2 | 硬件 (微代码) | 2-15% |
| STIBP | Spectre v2 (HT) | 硬件 (微代码) | 3-10% |
| lfence | Spectre v1 | 软件 | 0-2% |
| KPTI | Meltdown | 软件 | 5-30% |
| L1D flush | Foreshadow | 硬件 (微代码) | 0-5% |
| MDS buffer clear | MDS | 硬件 (微代码) | 0-5% |
| SSBD | Spectre v4 | 硬件 (微代码) | 0-5% |
| MBE (Mode Based Execute) | 通用 | 硬件 | 0% |

---

## 9. 防御原则

### 9.1 序列化指令 (Serializing Instructions)

- x86 `lfence`: 加载序列化
- x86 `mfence`: 内存序列化
- ARM `DSB`: 数据同步屏障
- ARM `ISB`: 指令同步屏障

### 9.2 推测屏障 (Speculation Barriers)

- ARM `CSDB`: 消耗性推测数据屏障
- ARM `SSBB`: 推测存储旁路屏障
- x86 `iret`: 隐式推测屏障

### 9.3 索引掩码

```c
// 安全数组访问 (Spectre v1 防御)
int safe_array_access(int *array, int x, int size) {
    int mask = size - 1;  // 假设 size 是 2 的幂
    x &= mask;            // 将 x 限制在 [0, size-1]
    return array[x];
}
```

---

## 10. 本模块实现

| 攻击 | 实现函数 | 文件 |
|-----|---------|------|
| Spectre v1 | `spectre_v1_simulate()` | `spec_exec_atk.c` |
| Spectre 隐蔽通道 | `spectre_v1_transmit()` | `spec_exec_atk.c` |
| Meltdown | `meltdown_simulate()` | `spec_exec_atk.c` |
| Meltdown 泄露字节 | `meltdown_leak_byte()` | `spec_exec_atk.c` |
| BTB 训练 | `spec_exec_btb_train()` | `spec_exec_atk.c` |
| 缓存探测 | `spec_exec_probe_cache()` | `spec_exec_atk.c` |

---

## 参考资料

- Kocher et al. (2019): Spectre Attacks: Exploiting Speculative Execution (IEEE S&P 2019)
- Lipp et al. (2018): Meltdown: Reading Kernel Memory from User Space (USENIX Security 2018)
- Van Bulck et al. (2018): Foreshadow: Extracting the Keys to Intel SGX Kingdom (USENIX Security 2018)
- Intel: Deep Dive: Analyzing Potential Bounds Check Bypass Vulnerabilities
- ARM: Cache Speculation Side-Channels (Arm Whitepaper 2018)
