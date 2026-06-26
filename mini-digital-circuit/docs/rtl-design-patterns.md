# RTL Design Patterns — RTL 设计模式

## Overview / 概述

本文档汇总寄存器传输级（RTL）设计中常用的设计模式，包括多路选择器、译码器、编码器、优先编码器、桶形移位器和 ALU 分解。

This document collects commonly used RTL design patterns: multiplexer, decoder, encoder, priority encoder, barrel shifter, and ALU decomposition.

---

## 1. Multiplexer (MUX) / 多路选择器

### Definition / 定义

MUX 从多个输入中选择一个输出，由选择信号控制。

```
  in0 ─┐
  in1 ─┤
  in2 ─┼──[MUX]── out
  in3 ─┤
       │
  sel ─┘
```

### 2-to-1 MUX Truth Table / 真值表

```
  sel | out
 -----|-----
   0  | in0
   1  | in1
```

### 4-to-1 MUX from 2-to-1 MUXes / 用 2→1 MUX 构建 4→1 MUX

```
                    ┌──────┐
  in0 ──────────────┤ MUX  │
  in1 ────────┐     │ 2:1  ├──┐
              ├─────┤      │  │
  in2 ────────┤     └──────┘  │     ┌──────┐
              │                ├─────┤ MUX  │── out
  in3 ────────┘     ┌──────┐  │     │ 2:1  │
              ┌─────┤ MUX  │  │     │      │
              │     │ 2:1  ├──┘     └──┬───┘
              │     └──────┘           │
  sel[0] ─────┤                    sel[1]
              │
```

### 4-to-1 MUX Boolean Equation / 布尔方程

```
out = ¬sel₁·¬sel₀·in₀ + ¬sel₁·sel₀·in₁ + sel₁·¬sel₀·in₂ + sel₁·sel₀·in₃
```

### N-to-1 MUX Gate Count / 门数分析

| N (inputs) | sel width | Gates (2-input, SOP) | Critical Path |
|------------|-----------|---------------------|---------------|
| 2          | 1         | 2 AND + 1 OR = 3    | 2 gates       |
| 4          | 2         | 4 AND + 1 OR = 5    | 3 gates       |
| 8          | 3         | 8 AND + 1 OR = 9    | 4 gates       |
| 2^k        | k         | 2^k AND + 1 OR       | k+1 gates     |

### Using the RTL Module / 使用 RTL 模块

```c
#include "rtl_basic.h"

RTLModule mux4 = rtl_mux_create("mux4", 4, 2);
rtl_set_signal(&mux4, "in0", 1);
rtl_set_signal(&mux4, "in1", 0);
rtl_set_signal(&mux4, "in2", 1);
rtl_set_signal(&mux4, "in3", 0);
rtl_set_signal(&mux4, "sel", 2);  // 选择 in2

uint64_t result = rtl_get_signal(&mux4, "out");
printf("mux4(sel=2) = %llu (expected 1)\n", result);
```

---

## 2. Decoder / 译码器

### Definition / 定义

N-to-2^N 译码器将 N 位二进制输入转换为 2^N 位 one-hot 输出。

### 2-to-4 Decoder Truth Table / 真值表

```
  A1 A0 | Y3 Y2 Y1 Y0
 -------|------------
   0  0 |  0  0  0  1
   0  1 |  0  0  1  0
   1  0 |  0  1  0  0
   1  1 |  1  0  0  0
```

### Implementation with AND Gates / 用 AND 门实现

```
  A0 ────┬──────────────┐
         │              │
  ¬A0 ───┼───┐          │
         │   │          │
  A1 ────┼───┼───┐      │
         │   │   │      │
  ¬A1 ───┼───┼───┼───┐  │
         │   │   │   │  │
         ▼   ▼   ▼   ▼  ▼
        [AND][AND][AND][AND]
         │    │    │    │
         Y0   Y1   Y2   Y3
```

**Boolean Equations**:
```
Y0 = ¬A₁ · ¬A₀
Y1 = ¬A₁ ·  A₀
Y2 =  A₁ · ¬A₀
Y3 =  A₁ ·  A₀
```

### 3-to-8 Decoder

| Input | Active Output |
|-------|---------------|
| 000   | Y0            |
| 001   | Y1            |
| 010   | Y2            |
| 011   | Y3            |
| 100   | Y4            |
| 101   | Y5            |
| 110   | Y6            |
| 111   | Y7            |

### Applications / 应用

1. **Memory address decoding**: 选择内存中的特定行/列
2. **Instruction decoding**: 将 opcode 转换为控制信号（one-hot）
3. **Seven-segment display**: 将 BCD 输入映射到 7 段显示
4. **Chip select**: 在多芯片系统中选择目标芯片

---

## 3. Encoder / 编码器

### Definition / 定义

编码器是译码器的逆操作：将 one-hot 输入编码为二进制输出。

### 4-to-2 Encoder Truth Table / 真值表

```
  Y3 Y2 Y1 Y0 | A1 A0
 -------------|------
   0  0  0  1 |  0  0
   0  0  1  0 |  0  1
   0  1  0  0 |  1  0
   1  0  0  0 |  1  1
```

### Boolean Equations / 布尔方程

```
A₁ = Y₃ + Y₂
A₀ = Y₃ + Y₁
```

### Where Encoders Fizzle / 编码器的局限

普通编码器假设**恰好一个**输入有效。当多个输入同时有效时，普通编码器产生不确定结果。此时需要优先编码器（Priority Encoder）。

---

## 4. Priority Encoder / 优先编码器

### Definition / 定义

优先编码器在多个输入同时有效时，输出最高优先级输入的编码。

### 4-to-2 Priority Encoder Truth Table

```
  Y3 Y2 Y1 Y0 | A1 A0 | Valid
 -------------|-------|------
   0  0  0  0 |  X  X |  0     ← 无有效输入
   0  0  0  1 |  0  0 |  1
   0  0  1  X |  0  1 |  1     ← Y1 优先于 Y0
   0  1  X  X |  1  0 |  1     ← Y2 优先于 Y1, Y0
   1  X  X  X |  1  1 |  1     ← Y3 优先级最高
```

X = don't care (0 or 1)

### Boolean Equations

```
Valid = Y₃ + Y₂ + Y₁ + Y₀
A₁    = Y₃ + Y₂
A₀    = Y₃ + ¬Y₂·Y₁
```

### Implementation

```c
typedef struct {
    unsigned char output;
    bool valid;
} PriorityEncoderResult;

PriorityEncoderResult priority_encode_4to2(
    bool y0, bool y1, bool y2, bool y3
) {
    PriorityEncoderResult r;
    r.valid = y0 || y1 || y2 || y3;

    if (y3)      { r.output = 3; }
    else if (y2) { r.output = 2; }
    else if (y1) { r.output = 1; }
    else if (y0) { r.output = 0; }
    else         { r.output = 0; }

    return r;
}
```

---

## 5. Barrel Shifter / 桶形移位器

### Definition / 定义

桶形移位器可以在一个时钟周期内完成任意位数的移位/循环移位操作。

### 8-bit Barrel Shifter Architecture / 架构

```
                Shift Amount
                     │
          ┌──────┬───┼───┬──────┐
          │      │   │   │      │
          ▼      ▼   │   ▼      ▼
  in[7:0]─────────────[MUX Layer 1]── w1[7:0] ──[MUX Layer 2]── w2[7:0] ──[MUX Layer 3]── out[7:0]
          shift 0/1?          shift 0/2?                shift 0/4?
          (sel[0])            (sel[1])                  (sel[2])
```

### Stage Decomposition

| Stage | Shift Amount | Data Path                          |
|-------|-------------|------------------------------------|
| 1     | 0 or 1      | in[i] or in[(i-1) mod 8]           |
| 2     | 0 or 2      | w1[i] or w1[(i-2) mod 8]           |
| 3     | 0 or 4      | w2[i] or w2[(i-4) mod 8]           |

### Truth Table Example (shift left by 3)

```
  Input:  10110010
  Shift:  3
  Output: 10010000  (left logical shift)

  Bit Index: 7 6 5 4 3 2 1 0
   in:        1 0 1 1 0 0 1 0
  out:        1 0 0 1 0 0 0 0
```

### C Implementation / C 实现

```c
unsigned char barrel_shift_left(unsigned char data, unsigned char amount) {
    unsigned char result = 0;
    for (int i = 0; i < 8; i++) {
        if (amount <= i && (data >> (i - amount)) & 1) {
            result |= (1 << i);
        }
    }
    return result;
}

unsigned char barrel_rotate_right(unsigned char data, unsigned char amount) {
    amount = amount % 8;
    return (data >> amount) | (data << (8 - amount));
}
```

---

## 6. ALU Decomposition / ALU 分解

### 1-bit ALU / 1 位 ALU

```
  A ──┬──[AND]──────┐
  B ──┘              │
  A ──┬──[OR]───────┤
  B ──┘              │
  A ──┬──[FA]───────┼──[MUX 4:1]── Result
  B ──┘  │           │
         Cout  op[1:0]┘
```

### 8-bit ALU: Bit-Slice Architecture

```
          A[7] B[7]        A[1] B[1]        A[0] B[0]
            │   │             │   │             │   │
    Cout ──[1-bit ALU]── ... ──[1-bit ALU]── ... ──[1-bit ALU]── Cin
            │                 │                 │
         R[7]              R[1]              R[0]

  op ──────────────── control to all slices ──────────────
```

### ALU Operation Encoding

| ALUop[2:0] | Function      | Formula  |
|------------|---------------|----------|
| 000        | AND           | A & B    |
| 001        | OR            | A \| B   |
| 010        | ADD           | A + B    |
| 011        | SUB           | A + ¬B + 1 |
| 100        | XOR           | A ^ B    |
| 101        | NOR           | ¬(A \| B) |
| 110        | SLT (set less than) | (A < B) ? 1 : 0 |
| 111        | PASS B        | B        |

Subtraction uses 2's complement: `A - B = A + ¬B + 1`

### Flags Generated by ALU

| Flag | Meaning                    | Condition                  |
|------|----------------------------|----------------------------|
| Z    | Zero                       | Result == 0                |
| N    | Negative                   | Result[7] == 1 (MSB)       |
| C    | Carry out                  | Carry from bit 7           |
| V    | Overflow                   | (Sign mismatch)            |

**Overflow Detection** (signed):
```
V = (A[7] & B[7] & ¬R[7]) | (¬A[7] & ¬B[7] & R[7])
  = CarryIn[7] ⊕ CarryOut[7]
```

---

## 7. Tri-State Buffer / 三态缓冲器

### Definition / 定义

三态缓冲器有三种输出状态：0, 1, 和高阻态 (Z)。

```
  in ──[Tri-State]── out
         │
  enable ┘

  enable=1: out = in
  enable=0: out = Z (high impedance)
```

### Application: Shared Bus

```
  dev0 ──[Tri-State]──┐
                       │
  dev1 ──[Tri-State]──┼── BUS
                       │
  dev2 ──[Tri-State]──┘

  Only ONE enable at a time!
```

---

## 8. Register File / 寄存器文件

### Architecture (8 × 8-bit, 2 read ports, 1 write port)

```
           RdAddr1[2:0]    RdAddr2[2:0]    WrAddr[2:0]
                │               │               │
         ┌──────┼───────┐       │       ┌───────┼──────┐
         │  Decoder     │       │       │ Decoder       │
         │  3-to-8      │       │       │ 3-to-8        │
         └──┬───┬───┬───┘       │       └──┬───┬───┬────┘
            │   │   │           │          │   │   │
         ┌──┴───┴───┴───────────┴──────────┴───┴───┴──┐
         │              Register Array (8 × 8)         │
         └──┬───┬───┬───┬───────────┬───┬───┬───┬─────┘
            │   │   │   │           │   │   │   │
         ┌──┴───┴───┴───┘       ┌──┴───┴───┴───┘
         │   MUX 8:1    │       │   MUX 8:1    │
         └──────┬───────┘       └──────┬───────┘
                │                      │
            RdData1                 RdData2
```

---

## 9. Adder Variants / 加法器变体

| Type                  | Gate Delay  | Area (gates) | Notes                |
|-----------------------|------------|-------------|----------------------|
| Ripple Carry (RCA)    | O(n) × tFA | O(n)        | 最简单，最慢         |
| Carry Lookahead (CLA) | O(log n)   | O(n log n)  | 快速，面积大         |
| Carry Select (CSLA)   | O(√n)      | O(n √n)     | 面积与速度折中       |
| Carry Skip (CSKA)     | O(√n)      | O(n)        | 分组跳跃进位         |
| Prefix (Kogge-Stone)  | O(log n)   | O(n log n)  | 最快，平行前缀计算   |

---

## 10. Clock Domain Crossing (CDC) / 跨时钟域

### Synchronizer (2-FF) / 同步器

```
  async_in ──┬──[DFF]────[DFF]── sync_out
             │     clk      clk
             │
  (from clock domain A)   (to clock domain B)
```

### MTBF (Mean Time Between Failures)

```
MTBF = e^(tr·fc) / (fd · fc · To)
```

其中：
- `tr` = resolve time (建立+保持时间)
- `fc` = 目标时钟频率
- `fd` = 异步输入变化率
- `To` = 亚稳态窗口

---

## Summary Table / 设计模式汇总

| Pattern           | Input Size     | Output Size     | Gate Count   | Latency |
|-------------------|---------------|----------------|-------------|---------|
| MUX               | 2^N × 1        | 1               | 2^N + log N  | O(logN) |
| Decoder           | N              | 2^N             | 2^N          | 1       |
| Encoder           | 2^N            | N               | 2^N (wire)   | 1       |
| Priority Encoder  | 2^N            | N+1 (with valid)| ~2^N/2      | log N   |
| Barrel Shifter    | W              | W               | W × log W    | log W   |
| ALU (N-bit)       | 2N+op_width   | N+flags         | N × gates/bit| ~N      |
| Register File     | addr×3 + data  | data×2          | N×8 + MUXes  | 2-3     |

---

## References / 参考文献

- Weste & Harris, "CMOS VLSI Design", Chapters 1, 10, 11
- Rabaey, Chandrakasan, Nikolic, "Digital Integrated Circuits", Chapters 6, 7, 11
- Hennessy & Patterson, Appendix C: "The Basics of Logic Design"
- Cliff Cummings, "Clock Domain Crossing (CDC) Design & Verification Techniques" (SNUG 2008)
