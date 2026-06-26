# mini-traffic-light-fsm — 交通灯有限状态机

## Overview / 概述

本文档展示如何使用有限状态机（FSM）设计交通灯控制器，涵盖 Moore 与 Mealy 两种模型，状态编码，以及软件仿真实现。

This document demonstrates traffic light controller design using FSMs, covering Moore vs Mealy models, state encoding, and software simulation.

---

## Theory / 理论背景

### What is an FSM? / 什么是有限状态机？

有限状态机（Finite State Machine, FSM）是一种数学模型，用于描述具有有限数量状态的系统。FSM 在任何时刻处于其中一个状态，并在接收到输入后根据转换规则迁移到下一个状态。

FSM 形式化定义为一个五元组 (S, I, O, δ, ω)：
- **S**: 有限状态集合
- **I**: 输入字母表
- **O**: 输出字母表
- **δ**: S × I → S (状态转移函数)
- **ω**: S → O (Moore) 或 S × I → O (Mealy)

---

### Moore vs Mealy / Moore 与 Mealy 对比

| 特性       | Moore 机           | Mealy 机                |
|------------|--------------------|-------------------------|
| 输出依赖   | 仅当前状态         | 当前状态 + 当前输入     |
| 输出时序   | 状态改变后更新     | 输入改变立即更新        |
| 状态数     | 通常更多           | 通常更少                |
| 同步性     | 完全同步           | 输出可能有毛刺          |
| 典型应用   | 交通灯、计数器     | 序列检测器、边沿检测器  |

### Moore Machine / Moore 机

```
         +--------+
  input  |        |  output
  ──────►│ State  ├────────►
         │ Logic  │
         +--------+
             │
             ▼
         +--------+
         | State  |
         | Memory |
         +--------+
```

输出仅由状态决定：output = ω(state)

### Mealy Machine / Mealy 机

```
         +--------+
  input  |        |  output
  ───┬──►│ State  ├──┬─────►
     │   │ Logic  │  │
     │   +--------+  │
     │        │       │
     │        ▼       │
     │   +--------+   │
     └───│ State  │───┘
         │ Memory │
         +--------+
```

输出由状态和输入共同决定：output = ω(state, input)

---

## Traffic Light Controller / 交通灯控制器

### Specification / 规格说明

- 三个灯状态：Green（绿）→ Yellow（黄）→ Red（红）→ Green
- 定时器信号 `timer_expired` 触发状态转换
- Moore 机：输出编码直接对应灯状态

### State Diagram / 状态图

```
        timer_expired=0
    ┌─────────────────────┐
    │                     │
    ▼                     │
 ┌───────┐  timer=1  ┌───────┐  timer=1  ┌───────┐
 │ GREEN │──────────►│YELLOW │──────────►│  RED  │
 │ 001   │           │ 010   │           │ 100   │
 └───────┘           └───────┘           └───┬───┘
                          ▲                  │
                          │    timer=1       │
                          └──────────────────┘
```

### State Encoding / 状态编码

| State  | Encoding | Light Output      | Duration |
|--------|----------|-------------------|----------|
| GREEN  | 00       | Green ON, others OFF | 30s   |
| YELLOW | 01       | Yellow ON, others OFF| 5s    |
| RED    | 10       | Red ON, others OFF   | 25s   |

### State Transition Table / 状态转换表

| Current State | Input (timer) | Next State | Output (Moore) |
|---------------|---------------|------------|-----------------|
| GREEN  (S0)   | 0             | GREEN  (S0)| 001 (Green)     |
| GREEN  (S0)   | 1             | YELLOW (S1)| 001 (Green)     |
| YELLOW (S1)   | 0             | YELLOW (S1)| 010 (Yellow)    |
| YELLOW (S1)   | 1             | RED    (S2)| 010 (Yellow)    |
| RED    (S2)   | 0             | RED    (S2)| 100 (Red)       |
| RED    (S2)   | 1             | GREEN  (S0)| 100 (Red)       |

---

## C Implementation / C 实现

### Using the FSM Module / 使用 fsm.h 模块

```c
#include "fsm.h"

/* 创建 Moore 型交通灯 FSM */
FSM traffic = fsm_create(FSM_MOORE);

/* 添加状态 */
fsm_add_state(&traffic, "GREEN",  false);  // state 0
fsm_add_state(&traffic, "YELLOW", false);  // state 1
fsm_add_state(&traffic, "RED",    true);   // state 2

/* 添加转换：timer=1 触发 */
fsm_add_transition(&traffic, 0, 1, 1, 0);  // GREEN → YELLOW
fsm_add_transition(&traffic, 1, 1, 2, 0);  // YELLOW → RED
fsm_add_transition(&traffic, 2, 1, 0, 0);  // RED → GREEN

/* 保持转换：timer=0 时保持当前状态 */
fsm_add_transition(&traffic, 0, 0, 0, 0);
fsm_add_transition(&traffic, 1, 0, 1, 0);
fsm_add_transition(&traffic, 2, 0, 2, 0);

/* 模拟 */
int timer_events[] = {0, 0, 1, 0, 1, 0, 1, 0, 0, 1};
fsm_simulate(&traffic, timer_events, 10);
```

### Output Waveform / 输出波形

```
Timer:  0 0 1 0 1 0 1 0 0 1
State:  G G Y Y R R G G G Y
Green:  ██████________________████████████
Yellow: ______████____________________████
Red:    __________████████________________
```

### Expected Output / 预期输出

```
FSM simulation start (type=Moore, start=GREEN):
  [GREEN] --(0)--> [GREEN]
  [GREEN] --(0)--> [GREEN]
  [GREEN] --(1)--> [YELLOW]
  [YELLOW] --(0)--> [YELLOW]
  [YELLOW] --(1)--> [RED]
  [RED] --(0)--> [RED]
  [RED] --(1)--> [GREEN]
  [GREEN] --(0)--> [GREEN]
  [GREEN] --(0)--> [GREEN]
  [GREEN] --(1)--> [YELLOW]
Final state: YELLOW
```

---

## Sequence Detector / 序列检测器

### Design / 设计

检测重叠出现的序列 "101"。这是一个经典的 FSM 设计问题。

### State Diagram / 状态图 (Moore)

```
               1
    ┌──────────────────────┐
    │                      │
    ▼                      │
 ┌──────┐  1   ┌──────┐  0  ┌──────┐  1  ┌──────┐
 │  S0  │─────►│  S1  │────►│ S10  │────►│ S101 │ (output=1)
 │  0   │◄─────│  0   │◄────│  0   │◄────│  1   │
 └──┬───┘  0   └──────┘  1  └──┬───┘  0  └──┬───┘
    │                          │             │
    └──────────────────────────┘             │
              0                              1
```

### Transition Table / 转换表

| State  | Meaning         | in=0 → | in=1 → |
|--------|-----------------|--------|--------|
| S0     | Initial/Reset   | S0     | S1     |
| S1     | Got '1'         | S10    | S1     |
| S10    | Got '10'        | S0     | S101   |
| S101   | Got '101' (out!)| S10    | S1     |

### C Implementation / C 实现

```c
FSM seq = fsm_create(FSM_MOORE);
fsm_add_state(&seq, "S0",   false);
fsm_add_state(&seq, "S1",   false);
fsm_add_state(&seq, "S10",  false);
fsm_add_state(&seq, "S101", true);  // accept state

fsm_add_transition(&seq, 0, 0, 0, 0);
fsm_add_transition(&seq, 0, 1, 1, 0);
fsm_add_transition(&seq, 1, 0, 2, 0);
fsm_add_transition(&seq, 1, 1, 1, 0);
fsm_add_transition(&seq, 2, 0, 0, 0);
fsm_add_transition(&seq, 2, 1, 3, 0);
fsm_add_transition(&seq, 3, 0, 2, 0);
fsm_add_transition(&seq, 3, 1, 1, 0);

int inputs[] = {1, 0, 1, 0, 1, 1, 0, 1};
fsm_simulate(&seq, inputs, 8);
```

### Expected Output / 预期输出

```
FSM simulation start (type=Moore, start=S0):
  [S0] --(1)--> [S1]
  [S1] --(0)--> [S10]
  [S10] --(1)--> [S101]   ← '101' detected!
  [S101] --(0)--> [S10]
  [S10] --(1)--> [S101]   ← '101' detected again!
  [S101] --(1)--> [S1]
  [S1] --(0)--> [S10]
  [S10] --(1)--> [S101]   ← '101' detected again!
Final state: S101
```

---

## Mealy Edge Detector / Mealy 上升沿检测器

### Design / 设计

检测信号的上升沿（0→1 跳变）。

### State Diagram / 状态图

```
        0/0
   ┌──────────┐
   │          │
   ▼          │
┌──────┐  1/1 ┌──────┐
│ LOW  │─────►│ HIGH │
└──────┘◄─────└──────┘
        0/0
                1/0
           ┌──────────┐
           │          │
           └──────────┘
```

### C Implementation / C 实现

```c
FSM edge = fsm_create(FSM_MEALY);
fsm_add_state(&edge, "LOW",  false);
fsm_add_state(&edge, "HIGH", false);

fsm_add_transition(&edge, 0, 0, 0, 0);  // LOW,  in=0 → LOW,  out=0
fsm_add_transition(&edge, 0, 1, 1, 1);  // LOW,  in=1 → HIGH, out=1 (edge!)
fsm_add_transition(&edge, 1, 0, 0, 0);  // HIGH, in=0 → LOW,  out=0
fsm_add_transition(&edge, 1, 1, 1, 0);  // HIGH, in=1 → HIGH, out=0
```

---

## State Encoding Trade-offs / 状态编码权衡

| Encoding    | Bits per State | Logic Complexity | Power  |
|-------------|---------------|------------------|--------|
| Binary      | ⌈log₂(N)⌉     | Medium           | Medium |
| One-hot     | N             | Low              | High   |
| Gray        | ⌈log₂(N)⌉     | High             | Low    |

For traffic light (3 states):
- **Binary**: 2 bits, 2 registers
- **One-hot**: 3 bits, 3 registers (simpler decode logic)

---

## Build & Run / 编译运行

```bash
cd mini-digital-circuit
make fsm_demo
./bin/fsm_demo
```

Or compile manually:

```bash
gcc -Wall -Wextra -O2 -I include -o bin/fsm_demo examples/fsm_demo.c src/fsm.c -lm
./bin/fsm_demo
```

---

## References / 参考文献

- MIT 6.004, Chapter 7: Finite State Machines
- MIT 6.111/6.205, Lecture 8: FSM Design
- Kohavi & Jha, "Switching and Finite Automata Theory"
