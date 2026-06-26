# Logic Fundamentals — 数字逻辑基础

## Overview / 概述

本文档覆盖数字电路中的布尔代数基础，包括基本定理、De Morgan 定律、卡诺图化简、SOP/POS 形式以及门的通用性。

This document covers Boolean algebra fundamentals, De Morgan's laws, Karnaugh maps, SOP/POS forms, and gate universality.

---

## Boolean Algebra Axioms / 布尔代数公理

布尔代数是一个有补分配格（complemented distributive lattice），定义在集合 B = {0, 1} 上。

### Basic Operations / 基本运算

| Name  | Symbol          | Truth Table                      |
|-------|-----------------|----------------------------------|
| AND   | X · Y (or X∧Y)  | 0·0=0, 0·1=0, 1·0=0, 1·1=1      |
| OR    | X + Y (or X∨Y)  | 0+0=0, 0+1=1, 1+0=1, 1+1=1      |
| NOT   | ¬X (or X')      | ¬0=1, ¬1=0                       |

### Axioms / 五大公理

1. **Closure**: a, b ∈ B → a+b ∈ B, a·b ∈ B, ¬a ∈ B
2. **Identity**: a+0 = a, a·1 = a
3. **Commutativity**: a+b = b+a, a·b = b·a
4. **Distributivity**: a·(b+c) = a·b + a·c, a+(b·c) = (a+b)·(a+c)
5. **Complement**: a + ¬a = 1, a · ¬a = 0

---

## Theorems / 基本定理

### Single-Variable Theorems / 单变量定理

| # | Theorem           | Dual                |
|---|-------------------|---------------------|
| T1| X + 0 = X         | X · 1 = X           |
| T2| X + 1 = 1         | X · 0 = 0           |
| T3| X + X = X         | X · X = X  (幂等律) |
| T4| ¬(¬X) = X (对合律) |                     |
| T5| X + ¬X = 1        | X · ¬X = 0          |

### Multi-Variable Theorems / 多变量定理

| #   | Theorem                    | Dual                          |
|-----|----------------------------|-------------------------------|
| T6  | X + Y = Y + X              | X · Y = Y · X  (交换律)       |
| T7  | (X+Y)+Z = X+(Y+Z)          | (X·Y)·Z = X·(Y·Z) (结合律)    |
| T8  | X·(Y+Z) = X·Y + X·Z        | X+Y·Z = (X+Y)·(X+Z) (分配律)  |
| T9  | X + X·Y = X (吸收律)       | X·(X+Y) = X                   |
| T10 | X·Y + X·¬Y = X (合并律)    | (X+Y)·(X+¬Y) = X              |
| T11 | X·Y + ¬X·Z + Y·Z = X·Y + ¬X·Z (一致律) | (X+Y)(¬X+Z)(Y+Z) = (X+Y)(¬X+Z) |

---

## De Morgan's Laws / 德摩根定律

De Morgan 定律是布尔代数中最强大的变换规则：

```
¬(X · Y) = ¬X + ¬Y
¬(X + Y) = ¬X · ¬Y
```

### General Form / 广义形式

```
¬(X₁ · X₂ · ... · Xₙ) = ¬X₁ + ¬X₂ + ... + ¬Xₙ
¬(X₁ + X₂ + ... + Xₙ) = ¬X₁ · ¬X₂ · ... · ¬Xₙ
```

### Proof by Truth Table / 真值表证明

```
  X Y | X·Y | ¬(X·Y) | ¬X+¬Y | X+Y | ¬(X+Y) | ¬X·¬Y
 -----|-----|--------|-------|-----|--------|------
  0 0 |  0  |   1    |   1   |  0  |   1    |  1
  0 1 |  0  |   1    |   1   |  1  |   0    |  0
  1 0 |  0  |   1    |   1   |  1  |   0    |  0
  1 1 |  1  |   0    |   0   |  1  |   0    |  0
```

### Application: Gate Transformation / 应用：门转换

```
  ┌───┐     ┌───┐        ┌───┐
  │AND│────►│NOT│   ≡    │NAND│
  └───┘     └───┘        └───┘

  ┌───┐     ┌───┐        ┌───┐
  │OR │────►│NOT│   ≡    │NOR│
  └───┘     └───┘        └───┘
```

使用 De Morgan 定律，可以使用 NAND 或 NOR 门实现任何逻辑功能。

---

## Gate Universality / 门的通用性

### NAND as Universal Gate / NAND 作为通用门

NAND 门是**功能完备**的（functionally complete），任何布尔函数都可以仅用 NAND 门实现：

**NOT from NAND**:
```
  A ──┬──┐
      │NAND├── ¬A
  A ──┼──┘
      │
```

**AND from NAND**:
```
  A ──┬──[NAND]────[NAND]── A·B
  B ──┘          ┌──┘
                 │
```

**OR from NAND**:
```
  A ──[NAND]──┐
              ├──[NAND]── A+B
  B ──[NAND]──┘
```

### NOR as Universal Gate / NOR 作为通用门

NOR 门同样是功能完备的：

**NOT from NOR**: A NOR A = ¬A
**OR from NOR**: ¬(A NOR B) = A + B
**AND from NOR**: ¬(¬A NOR ¬B) = A · B

### C Code: NAND-only Circuit Simulation / NAND 电路模拟

```c
#include "logic_gate.h"

/* 仅用 NAND 门实现 XOR */
/* XOR = (A NAND (A NAND B)) NAND (B NAND (A NAND B)) */

Wire* a = wire_create("A");
Wire* b = wire_create("B");
Wire* w1 = wire_create("W1");  // A NAND B
Wire* w2 = wire_create("W2");  // A NAND w1
Wire* w3 = wire_create("W3");  // B NAND w1
Wire* w_out = wire_create("XOR_OUT");

Wire* g1_in[] = {a, b};       LogicGate g1 = logic_gate_create(GATE_NAND, g1_in, 2, w1);
Wire* g2_in[] = {a, w1};      LogicGate g2 = logic_gate_create(GATE_NAND, g2_in, 2, w2);
Wire* g3_in[] = {b, w1};      LogicGate g3 = logic_gate_create(GATE_NAND, g3_in, 2, w3);
Wire* g4_in[] = {w2, w3};     LogicGate g4 = logic_gate_create(GATE_NAND, g4_in, 2, w_out);

/* 验证 XOR: 4 个 NAND 门实现 */

for (int av = 0; av <= 1; av++) {
    for (int bv = 0; bv <= 1; bv++) {
        a->value = av; b->value = bv;
        logic_eval(&g1);
        logic_eval(&g2);
        logic_eval(&g3);
        logic_eval(&g4);
        printf("XOR(%d,%d) = %d\n", av, bv, w_out->value);
    }
}
```

**Expected output**:
```
XOR(0,0) = 0
XOR(0,1) = 1
XOR(1,0) = 1
XOR(1,1) = 0
```

---

## Sum of Products (SOP) / 和之积形式

### Definition / 定义

SOP 形式是将布尔函数表示为多个乘积项（Product Terms，即 AND 项）的 OR：
```
F = Σm(i, j, k, ...)
```

其中每个 m(i) 是一个**最小项（minterm）**，对应真值表中输出为 1 的行。

### Example: 2-input XOR

True table for XOR:
```
  A B | XOR
 -----|-----
  0 0 |  0
  0 1 |  1   ← minterm m(1)
  1 0 |  1   ← minterm m(2)
  1 1 |  0
```

SOP: `XOR = ¬A·B + A·¬B`

### Example: Majority Function / 三变量表决函数

```
  A B C | M
 -------|---
  0 0 0 | 0
  0 0 1 | 0
  0 1 0 | 0
  0 1 1 | 1  ← m(3)
  1 0 0 | 0
  1 0 1 | 1  ← m(5)
  1 1 0 | 1  ← m(6)
  1 1 1 | 1  ← m(7)
```

SOP: `M = ¬A·B·C + A·¬B·C + A·B·¬C + A·B·C`

---

## Product of Sums (POS) / 积之和形式

### Definition / 定义

POS 形式是将布尔函数表示为多个和项（Sum Terms，即 OR 项）的 AND：
```
F = ΠM(j, k, l, ...)
```

其中每个 M(j) 是一个**最大项（maxterm）**，对应真值表中输出为 0 的行。

对于上面的 XOR:
POS: `XOR = (A+B) · (¬A+¬B)`

---

## Karnaugh Maps (K-Maps) / 卡诺图

K-map 是一种图形化的布尔化简工具，由 Maurice Karnaugh 于 1953 年提出。

### 2-Variable K-Map

```
     B
    0   1
  ┌───┬───┐
A 0│   │   │
  ├───┼───┤
  1│   │   │
  └───┴───┘
```

### 3-Variable K-Map

```
      BC
     00  01  11  10
  ┌───┬───┬───┬───┐
A 0│   │   │   │   │
  ├───┼───┼───┼───┤
  1│   │   │   │   │
  └───┴───┴───┴───┘
```

### 4-Variable K-Map

```
       CD
      00  01  11  10
  ┌───┬───┬───┬───┐
  │00 │   │   │   │
AB├───┼───┼───┼───┤
  │01 │   │   │   │
  ├───┼───┼───┼───┤
  │11 │   │   │   │
  ├───┼───┼───┼───┤
  │10 │   │   │   │
  └───┴───┴───┴───┘
```

### K-Map Simplification Rules / 化简规则

1. 圈出所有 **1**（SOP 时）或所有 **0**（POS 时）
2. 每个圈必须是 2ⁿ 大小（1, 2, 4, 8, ...）的矩形
3. 圈可以包裹边缘（toroidal topology）
4. 每个圈尽可能大（最小化项数）
5. 每个 1 至少被一个圈覆盖

### Example: Majority Function K-Map / 表决函数 K-map

```
      BC
     00  01  11  10
  ┌───┬───┬───┬───┐
A 0│ 0 │ 0 │ 1 │ 0 │
  ├───┼───┼───┼───┤
  1│ 0 │ 1 │ 1 │ 1 │
  └───┴───┴───┴───┘
```

化简结果：`M = A·B + B·C + A·C`

这比原始 SOP 少了两个项和一个字面量！

---

## Gate-Level Optimization Metrics / 门级优化指标

| Metric         | SOP (before) | K-map (after) | Improvement |
|----------------|-------------|---------------|-------------|
| Product terms  | 4           | 3             | -25%        |
| Literals       | 12          | 6             | -50%        |
| Gate count     | 5 (4 AND + 1 OR) | 4 (3 AND + 1 OR) | -20% |
| Critical path  | 2 gates     | 2 gates       | same        |

---

## Don't Care Conditions / 无关项条件

在某些设计中，某些输入组合永远不会出现，标注为 **X (don't care)**：

```
      BC
     00  01  11  10
  ┌───┬───┬───┬───┐
A 0│ 0 │ X │ 1 │ 0 │    X 可以当作 1 或 0 使用，以获得更大圈
  ├───┼───┼───┼───┤
  1│ 0 │ 1 │ 1 │ 1 │
  └───┴───┴───┴───┘
```

---

## Hazards in Combinational Logic / 组合逻辑中的冒险

### Static-1 Hazard / 静态-1 冒险

当输出应当保持 1 但短暂变为 0 时发生。

**Example**: `F = A·B + ¬A·C`，当 B=C=1 且 A 从 1→0 变化时。

修复方法：添加冗余项 `B·C`（一致律定理 T11）。

### Static-0 Hazard

输出应当保持 0 但短暂变为 1。

### Dynamic Hazard

输出应当在 0→1 之间跳变一次，但跳变多次。

**Rule**: 在两级 SOP 电路中不会出现动态冒险。

---

## Common Standard Forms / 常用标准形式

| Form | Description | Example |
|------|-------------|---------|
| SOP  | Sum of Products | F = AB + ¬AC + BC |
| POS  | Product of Sums | F = (A+B)(¬A+C)(B+C) |
| NAND-NAND | All NAND gates | F = NAND(NAND(A,B), NAND(¬A,C), NAND(B,C)) |
| NOR-NOR   | All NOR gates  | F = NOR(NOR(A,B), NOR(¬A,C), NOR(B,C)) |
| AOI (AND-OR-Invert) | AND tree → NOR | F = ¬(AB + ¬AC + BC) |

---

## References / 参考文献

- George Boole, "An Investigation of the Laws of Thought" (1854)
- Claude Shannon, "A Symbolic Analysis of Relay and Switching Circuits" (1938) — 将布尔代数引入电路设计
- Maurice Karnaugh, "The Map Method for Synthesis of Combinational Logic Circuits" (1953)
- Edward J. McCluskey, "Logic Design Principles" — Quine-McCluskey 算法
