# mini-alu — 从门电路构建 ALU

## Overview / 概述

本文档详细展示如何从最基础的门电路出发，逐步构建一个完整的 8 位算术逻辑单元（ALU）。全程使用本模块 `mini-digital-circuit` 提供的 C 语言门电路模拟 API。

This document demonstrates building a complete 8-bit ALU from basic logic gates, step by step, using the C simulation API provided by `mini-digital-circuit`.

---

## Theory / 理论背景

### Boolean Algebra Foundation / 布尔代数基础

数字电路的基础是布尔代数（Boolean Algebra），由 George Boole 于 1854 年提出。基本运算：

| 运算 | 符号 | 真值表 |
|------|------|--------|
| AND  | A·B  | 11→1, else→0 |
| OR   | A+B  | 00→0, else→1 |
| NOT  | ¬A   | 0→1, 1→0 |

在此基础上派生出：
- **NAND** = ¬(A·B)
- **NOR**  = ¬(A+B)
- **XOR**  = A⊕B = (A·¬B) + (¬A·B)
- **XNOR** = ¬(A⊕B) = A⊙B

---

## Step 1: Half Adder / 半加器

半加器将两个一位二进制数相加，产生和（Sum）与进位（Carry）。

### Truth Table / 真值表

```
  A B | SUM CARRY
 -----|----------
  0 0 |  0    0
  0 1 |  1    0
  1 0 |  1    0
  1 1 |  0    1
```

### Logic / 逻辑

```
SUM   = A ⊕ B
CARRY = A · B
```

### ASCII Diagram / 原理图

```
   A ──┬──┐
       │ XOR├── SUM
   B ──┼──┘
       │
       ├──┐
       │ AND├── CARRY
       └──┘
```

### C Implementation / C 实现

```c
#include "logic_gate.h"

HalfAdder ha = half_adder_create();

// 创建 XOR 门计算 SUM
Wire* xor_in[] = {ha.a, ha.b};
LogicGate g_xor = logic_gate_create(GATE_XOR, xor_in, 2, ha.sum);

// 创建 AND 门计算 CARRY
Wire* and_in[] = {ha.a, ha.b};
LogicGate g_and = logic_gate_create(GATE_AND, and_in, 2, ha.carry);

// 评估
ha.a->value = 1;
ha.b->value = 0;
logic_eval(&g_xor);  // SUM = 1
logic_eval(&g_and);  // CARRY = 0
```

### Expected Output / 预期输出

```
  A B | SUM CARRY
 -----|----------
  0 0 |  0    0
  0 1 |  1    0
  1 0 |  1    0
  1 1 |  0    1
```

---

## Step 2: Full Adder / 全加器

全加器在半加器的基础上增加了进位输入（Carry-in）。

### Truth Table / 真值表

```
  A B Cin | SUM Cout
 ---------|---------
  0 0  0  |  0   0
  0 0  1  |  1   0
  0 1  0  |  1   0
  0 1  1  |  0   1
  1 0  0  |  1   0
  1 0  1  |  0   1
  1 1  0  |  0   1
  1 1  1  |  1   1
```

### Logic / 逻辑

```
SUM   = A ⊕ B ⊕ Cin
Cout  = (A · B) + (Cin · (A ⊕ B))
```

### ASCII Diagram / 原理图

```
   A ──┬──[XOR]── w1 ──[XOR]── SUM
   B ──┘         │
          ┌──────┘
          │
         [AND]── w2
   Cin ──┘
          │
         [AND]── w3 ──┐
   A·B ──[OR]────────[OR]── Cout
   w1·Cin ─────────────┘
```

### C Implementation / C 实现

```c
#include "combinational.h"

Combinational c = comb_create();

// 创建 wires
int w_a   = comb_add_wire(&c, "A");
int w_b   = comb_add_wire(&c, "B");
int w_cin = comb_add_wire(&c, "CIN");
int w_sum = comb_add_wire(&c, "SUM");
int w_cout = comb_add_wire(&c, "COUT");

// 中间 wires
int w_xor1 = comb_add_wire(&c, "xor1");
int w_and1 = comb_add_wire(&c, "and1");
int w_and2 = comb_add_wire(&c, "and2");

// 门电路
int xor1_in[] = {w_a, w_b};
comb_add_gate(&c, GATE_XOR, xor1_in, 2, w_xor1);

int xor2_in[] = {w_xor1, w_cin};
comb_add_gate(&c, GATE_XOR, xor2_in, 2, w_sum);

int and1_in[] = {w_a, w_b};
comb_add_gate(&c, GATE_AND, and1_in, 2, w_and1);

int and2_in[] = {w_xor1, w_cin};
comb_add_gate(&c, GATE_AND, and2_in, 2, w_and2);

int or_in[] = {w_and1, w_and2};
comb_add_gate(&c, GATE_OR, or_in, 2, w_cout);

// 设置输入并评估
c.wires[w_a].value = 1;
c.wires[w_b].value = 1;
c.wires[w_cin].value = 0;
comb_evaluate(&c);
// SUM = 0, Cout = 1
```

---

## Step 3: Ripple Carry Adder / 行波进位加法器

将 N 个全加器级联，最低位 Cin=0，每级 Cout 接入下一级 Cin。

### ASCII Diagram / 原理图 (4-bit)

```
  A3 B3      A2 B2      A1 B1      A0 B0
   │  │        │  │        │  │        │  │
  ┌┴──┴┐      ┌┴──┴┐      ┌┴──┴┐      ┌┴──┴┐
  │ FA │◄─────│ FA │◄─────│ FA │◄─────│ FA │◄── 0 (Cin)
  └┬──┬┘      └┬──┬┘      └┬──┬┘      └┬──┬┘
   │  │        │  │        │  │        │  │
  Cout S3     C3  S2     C2  S1     C1  S0
```

### Propagation Delay / 传播延迟

行波进位加法器的关键缺点：进位必须从最低位逐级传播到最高位，延迟为 O(N)·t(FA)。对于 8 位加法器，最坏情况延迟 = 8 × 2 个门延迟 ≈ 16 gate delays。

### C Implementation / C 实现

```c
RippleAdder ra = ripple_adder_create(8);

// 为每个 bit 创建全加器门电路
// 使用 Combinational 结构管理
Combinational c = comb_create();
// ... 见 alu_demo.c 完整实现
```

---

## Step 4: 1-bit ALU / 1 位 ALU

1 位 ALU 支持多种运算，通过控制信号选择：

```
Control | Operation
--------|-----------
  000   | A AND B
  001   | A OR B
  010   | A + B (add)
  110   | A - B (subtract)
  111   | SLT (set less than)
```

使用多路选择器（MUX）选择输出：

```
   A ──┬──[AND]──┐
   B ──┘         │
   A ──┬──[OR] ──┤
   B ──┘         ├──[MUX 8:1]── Result
   A ──┬──[FA] ──┤
   B ──┘         │
   ...           │
Control ─────────┘
```

---

## Step 5: 8-bit ALU / 8 位 ALU

将 8 个 1 位 ALU 并行连接，处理进位链，添加零检测、溢出检测等标志位。

### Operations / 支持的操作

| Op Code | Operation | Description       |
|---------|-----------|-------------------|
| 000     | ADD       | A + B             |
| 001     | SUB       | A - B             |
| 010     | AND       | A & B (bitwise)   |
| 011     | OR        | A \| B (bitwise)  |
| 100     | XOR       | A ^ B (bitwise)   |

### Test Cases / 测试用例

```
Op   |   A      B   | Result
-----|--------------|--------
ADD  | 0x0A  0x05   |  0x0F
SUB  | 0x0A  0x05   |  0x05
AND  | 0x55  0xAA   |  0x00
OR   | 0x55  0xAA   |  0xFF
XOR  | 0x55  0xAA   |  0xFF
ADD  | 0x7F  0x01   |  0x80   ← 溢出
SUB  | 0x00  0x01   |  0xFF   ← 借位
```

### Gate-Level Verification / 门级验证

使用 `combinational.c` 创建实际的 8 位行波进位加法器，验证门级仿真结果：

```c
// 构建 8-bit ripple carry adder
for (int i = 0; i < 8; i++) {
    // 每位创建 5 个门：2 XOR, 2 AND, 1 OR
    comb_add_gate(&c, GATE_XOR, xor1_in, 2, w_xor1);
    comb_add_gate(&c, GATE_AND, and1_in, 2, w_and1);
    comb_add_gate(&c, GATE_XOR, xor2_in, 2, w_s[i]);
    comb_add_gate(&c, GATE_AND, and2_in, 2, w_and2);
    comb_add_gate(&c, GATE_OR,  or_in,  2, w_carry[i+1]);
}
// 总计: 8 × 5 = 40 个门
```

### Expected Output / 预期输出

```
0x0A + 0x05 = 0x0F (C=0)  [gate-level]
0x55 + 0xAA = 0xFF (C=0)  [gate-level]
0x7F + 0x01 = 0x80 (C=0)  [gate-level]
0xFF + 0x01 = 0x00 (C=1)  [gate-level]
```

---

## Performance Analysis / 性能分析

| Component     | Gates Used | Gate Delays (critical path) |
|---------------|------------|-----------------------------|
| Half Adder    | 2          | 2 (XOR + AND in parallel)   |
| Full Adder    | 5          | 3 (XOR→AND→OR)              |
| 8-bit RCA     | 40         | ~17 (2×8 + 1)               |
| 8-bit ALU     | ~200       | ~25                         |

---

## Build & Run / 编译运行

```bash
cd mini-digital-circuit
make alu_demo
./bin/alu_demo
```

Or compile manually:

```bash
gcc -Wall -Wextra -O2 -I include -o bin/alu_demo examples/alu_demo.c src/logic_gate.c src/combinational.c -lm
./bin/alu_demo
```

---

## References / 参考文献

- MIT 6.004 Computation Structures, Chapter 8: Combinational Logic
- MIT 6.111/6.205, Lecture 4: Arithmetic Circuits
- Harris & Harris, "Digital Design and Computer Architecture", Chapter 5
