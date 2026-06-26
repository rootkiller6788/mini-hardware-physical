# Mini Spectre — Spectre 推测执行攻击详解

> 本文档深入讲解 Spectre 推测执行攻击 (Speculative Execution Attacks) 的原理与实现

---

## 1. 概述 (Overview)

Spectre 是现代处理器漏洞家族，利用"推测执行" (Speculative Execution) 机制泄露信息。
漏洞于 2018 年 1 月由 Google Project Zero 和相关学术团队公开披露。

### Spectre 变体总览

| 变体 | 名称 | CVE | 影响 | 利用技术 |
|-----|------|-----|------|---------|
| v1 | Bounds Check Bypass | CVE-2017-5753 | 所有乱序执行CPU | BTB训练+越界访问 |
| v2 | Branch Target Injection | CVE-2017-5715 | 分支预测器 | PHT操纵+间接跳转 |
| v3 | Meltdown (Rogue Data Cache Load) | CVE-2017-5754 | Intel/ARM | 异常抑制+缓存泄露 |
| v4 | Speculative Store Bypass | CVE-2018-3639 | 所有乱序执行CPU | 存储转发推测 |
| v5 | Return Stack Buffer | - | 部分CPU | RSB下溢攻击 |

---

## 2. 推测执行基础 (Speculative Execution Primer)

### 2.1 为什么需要推测执行

现代处理器的流水线深度可达 14-19 级（Intel Core）甚至更多。分支指令（如 `if`, `loop`）会打断流水线，导致性能损失。

```
无推测执行:
  [Fetch] [Dec] [Exec] [Mem] [WB]
  ----------------------------------> 遇到分支 -> 流水线清空!
  [Fetch] [Dec] [Exec] [Mem] [WB]   (重新开始，浪费5-10个周期)

推测执行:
  [Fetch] [Dec] [Exec] [Mem] [WB]
  ----------------------------------> 猜测分支方向
  [Fetch] [Dec] [Exec] [Mem] [WB]   (继续执行，不等待)
  -> 正确: 继续使用结果 (吞吐量最大化)
  -> 错误: 丢弃推测状态 (ROLLBACK)
```

### 2.2 分支预测器 (Branch Predictor)

现代处理器的分支预测器包含多个组件：

| 组件 | 功能 | 训练方式 |
|-----|------|---------|
| BTB (Branch Target Buffer) | 存储分支目标地址 | 分支跳转时更新 |
| PHT (Pattern History Table) | 存储分支方向历史 | 基于全局/局部历史 |
| RSB (Return Stack Buffer) | 预测 RET 指令目标 | CALL时压栈, RET时弹栈 |
| ITTAGE | 混合预测器 | 基于路径历史 |

---

## 3. Spectre v1: Bounds Check Bypass (BCB)

### 3.1 攻击原理

Spectre v1 利用分支预测器在有边界检查的代码中诱导错误推测。

```c
// 受害代码（例如在 Linux 内核或安全模块中）
if (x < array1_size) {
    // 分支通常被预测为 TAKEN
    y = probe_array[array1[x] * 4096];
}
```

### 3.2 攻击步骤

```
步骤 1: 训练 BTB
  使用合法的 x (x < array1_size) 访问 5-10 次
  -> BTB 学习: "if (x < size) 通常为真"

步骤 2: 刷新缓存 (Flush)
  clflush(probe_array[0..255])  // 清除 probe 数组缓存

步骤 3: 攻击触发
  使用恶意的 x (x >= array1_size)
  -> BTB 预测为 "TAKEN" (在边界检查完成前)
  -> CPU 推测性地执行:
     y = probe_array[array1[out_of_bounds]] * 4096
  -> 将 array1[malicious] 的值作为索引加载 probe_array

步骤 4: 边界检查完成 -> 错误预测 -> 回滚
  但 probe_array[secret * 4096] 已在缓存中!

步骤 5: 缓存时序攻击恢复秘密
  for i in 0..255:
    t = measure_access(probe_array[i * 4096])
  -> 最快的是 i = secret

步骤 6: 重复对所有越界位置
  读取整个 array1，每次泄露一个字节
```

### 3.3 分支预测器训练细节

```c
// 训练阶段：让预测器相信边界检查总是通过
for (int i = 0; i < 6; i++) {
    x_train = i % array1_size;        // 合法 x，分支 TAKEN
    if (x_train < array1_size) {      // 分支预测器记录 "通常 TAKEN"
        y = array2[array1[x_train]];
    }
}

// 攻击阶段：恶意 x，但预测器仍猜测 TAKEN
x_mal = array1_size + secret_offset;  // 越界!
if (x_mal < array1_size) {            // 预测器猜 "TAKEN"
    // 推测执行进入此分支!
    y = array2[array1[x_mal]];        // 越界访问，泄露秘密
}
// 实际结果：边界检查失败，寄存器回滚，但缓存已泄露...
```

### 3.4 代码示例（JavaScript 版本）

```javascript
// 浏览器中的 Spectre v1（概念性）
const arr1 = new Uint8Array(10);
const arr2 = new Uint8Array(256 * 4096);

// 步骤 1: 训练
for (let i = 0; i < 6; i++) {
    let x = (i % 10);      // 合法
    let y = arr2[arr1[x] * 4096];
}

// 步骤 2: 攻击
let x = 20;                // 越界
// CPU 推测: arr1[x] -> 访问 arr1 后的内存
let y = arr2[arr1[x] * 4096];  // 泄露到缓存
```

---

## 4. Spectre v2: Branch Target Injection (BTI)

### 4.1 攻击原理

Spectre v2 通过注入间接跳转目标来劫持推测执行流程。

```c
// 受害代码
jmp *%rax   // 间接跳转，目标在 rax
```

攻击者训练 BTB 使 `jmp *%rax` 推测跳转到攻击者控制的代码片段（gadget），然后在推测执行中运行 gadget。

### 4.2 攻击流程

```
1. 攻击者训练 BTB: 将 "jmp *%rax" 映射到攻击者 gadget 地址
2. 受害进程执行 "jmp *%rax"
3. BTB 预测: 跳转到攻击者 gadget
4. Gadget 推测执行: 读取秘密 -> 缓存泄露
5. 实际目标解析完成后: 回滚推测状态
6. 攻击者通过 Flush+Reload 恢复秘密
```

---

## 5. Spectre v4: Speculative Store Bypass (SSB)

### 5.1 攻击原理

推测性存储绕过：当 CPU 推测性地忽略存储-加载依赖时，可能将旧值泄漏给新加载。

```c
// 受害者代码
store x -> mem[addr];    // 存储值 x
load y <- mem[addr];     // 加载值（应该是 x）

// 推测执行可能使用存储前的旧值（store forwarding bypass）
```

---

## 6. 隐蔽通道 (Covert Channel)

Spectre 攻击利用缓存侧信道作为"隐蔽通道"将泄露的数据传输出来。

### 6.1 传输协议

```c
// 编码器: 将数据位编码为缓存状态
void transmit_bit(uint8_t bit) {
    if (bit) {
        access(probe_array[1 * 4096]);  // 缓存线 1 HOT = 位 1
    } else {
        access(probe_array[0 * 4096]);  // 缓存线 0 HOT = 位 0
    }
}

// 解码器: 从缓存状态解码数据位
uint8_t receive_bit(void) {
    int t0 = measure_access(probe_array[0 * 4096]);
    int t1 = measure_access(probe_array[1 * 4096]);
    return (t0 < t1) ? 0 : 1;
}
```

### 6.2 带宽

在典型系统上，此隐蔽通道可达到 500 KB/s。

---

## 7. 防御措施 (Mitigations)

### 7.1 Spectre v1

| 防御 | 描述 |
|-----|------|
| `lfence` 指令 | 串行化指令流，阻止后续指令在边界检查完成前执行 |
| 索引掩码 | `x & (size - 1)` 用于二的幂大小数组 |
| ARM CSDB | 消耗性推测数据屏障 |

```c
// lfence 防御
if (x < array1_size) {
    _mm_lfence();        // 阻止推测超过此点
    y = probe_array[array1[x] * 4096];
}
```

### 7.2 Spectre v2

| 防御 | 描述 |
|-----|------|
| Retpoline | 用返回指令替换间接跳转，避免 BTB 预测 |
| IBRS (Indirect Branch Restricted Speculation) | 微代码补丁，限制间接分支推测 |
| STIBP (Single Thread Indirect Branch Predictors) | 阻止跨超线程分支预测 |
| IBPB (Indirect Branch Prediction Barrier) | 刷新间接分支预测器 |

### 7.3 Spectre v4

| 防御 | 描述 |
|-----|------|
| SSBD (Speculative Store Bypass Disable) | 禁用推测性存储绕过 |
| 内存屏障 | `mfence` / `dsb sy` |

---

## 8. 影响评估

| 维度 | 影响 |
|-----|------|
| 受影响处理器 | 1995年至今几乎所有乱序执行处理器 |
| 跨进程 | 是 (v1, v2, v4) |
| 跨虚拟机 | 是 (v2, v4) |
| 跨内核/用户 | 是 (v3 Meltdown) |
| 远程可利用? | 有限 (需要执行代码，但JavaScript可在浏览器中利用) |
| 性能影响 | 2-30% 取决于防御措施配置 |

---

## 9. 本模块实现

本模块 (`spec_exec_atk.h/c`) 提供了：

- `spectre_v1_simulate()`: Spectre v1 完整仿真
- `spectre_v1_transmit()`: 隐蔽通道传输
- `meltdown_simulate()`: Meltdown 攻击仿真
- `meltdown_leak_byte()`: Meltdown 单字节泄露
- BTB 预测器模型 (64 条目)
- 推测执行引擎 + 回滚机制

---

## 10. 运行演示

```bash
make spectre_demo
make meltdown_demo
./bin/spectre_demo
./bin/meltdown_demo
```

---

## 参考资料

- Kocher et al. (2019): Spectre Attacks: Exploiting Speculative Execution
- Lipp et al. (2018): Meltdown: Reading Kernel Memory from User Space
- Google Project Zero: Reading privileged memory with a side-channel
- ARM: Vulnerability of Speculative Processors to Cache Timing Side-Channel Mechanism
