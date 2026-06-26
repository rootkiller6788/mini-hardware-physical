# Course Alignment — MIT 6.004 课程对应

## Overview / 概述

本模块 `mini-digital-circuit` 与 MIT 6.004 Computation Structures 课程内容对应关系。同时也参考了 MIT 6.111/6.205（原 6.111 Introductory Digital Systems Laboratory）的实验内容。

This document maps each module component to the corresponding MIT 6.004 chapters and topics.

---

## Module-to-Course Mapping Table

| mini-digital-circuit 模块 | MIT 6.004 章节 | 核心概念 |
|---------------------------|---------------|----------|
| `logic_gate`              | Chapter 4: Combinational Logic | 门电路、布尔代数、真值表、CMOS 门实现 |
| `combinational`           | Chapter 5: Combinational Building Blocks | 半加器、全加器、行波进位加法器、多路选择器 |
| `combinational` (ALU)     | Chapter 8: Arithmetic Circuits | 算术逻辑单元、进位链、溢出检测 |
| `sequential`              | Chapter 6: Sequential Logic | D 触发器、寄存器、SR 锁存器、D 锁存器 |
| `fsm`                     | Chapter 7: Finite State Machines | Moore/Mealy 机、状态编码、序列检测器 |
| `fsm` (controller)        | Chapter 8: Control Structures | 交通灯控制器、FSM 在控制单元中的应用 |
| `rtl_basic`               | Chapter 9: RTL Design | 寄存器传输级抽象、数据通路设计 |
| `rtl_basic` (CPU)         | Chapter 10: Pipelining | 单周期/多周期 CPU、流水线、冒险处理 |

---

## Detailed Alignment / 详细对照

### Chapter 4: Combinational Logic
**6.004 内容**：
- CMOS 晶体管作为开关
- 基本门电路：反相器、与非门、或非门
- 布尔表达式与真值表
- 组合逻辑综合：Sum of Products (SOP), Product of Sums (POS)
- 卡诺图化简

**本模块对应**：
- `include/logic_gate.h`: 定义了 AND, OR, NOT, NAND, NOR, XOR, XNOR 七种门类型
- `src/logic_gate.c`: 实现了每种门的布尔评估函数 `logic_eval()`
- `logic_print_truth_table()`: 打印任意门类型的真值表
- 参考文档 `docs/logic-fundamentals.md` 覆盖布尔代数、De Morgan 定律、K-map

**6.004 实验对应**：Lab 2 - Combinational Logic

### Chapter 5: Combinational Building Blocks
**6.004 内容**：
- 多路选择器（MUX）、解复用器
- 编码器、译码器
- 半加器、全加器
- 行波进位加法器
- 只读存储器（ROM）、可编程逻辑阵列（PLA）

**本模块对应**：
- `include/combinational.h`: 定义了 HalfAdder, FullAdder, RippleAdder 结构
- `src/combinational.c`: 实现 `comb_create()`, `comb_add_gate()`, `comb_evaluate()`
- `comb_print()`: 打印组合逻辑网的导线值
- `include/rtl_basic.h`: 定义了 Mux, Decoder, Encoder 构建器
- `src/rtl_basic.c`: 实现 `rtl_mux_create()`, `rtl_decoder_create()`, `rtl_encoder_create()`

**6.004 实验对应**：Lab 3 - Multiplier (using adders)

### Chapter 6: Sequential Logic
**6.004 内容**：
- 时序逻辑基础：正反馈与双稳态元件
- SR 锁存器、D 锁存器、D 触发器
- 建立时间（setup time）与保持时间（hold time）
- 寄存器与寄存器文件
- 同步时序逻辑设计

**本模块对应**：
- `include/sequential.h`: 定义了 DFlipFlop, Register, SRLatch, DLatch
- `src/sequential.c`: 实现正边沿触发的 D 触发器 `dff_clock()`
- `reg_write()`, `reg_read()`: 寄存器读写操作
- `sr_latch_eval()`: SR 锁存器行为表实现
- `d_latch_set()`, `d_latch_enable()`: D 锁存器实现

**6.004 实验对应**：Lab 4 - Sequential Logic

### Chapter 7: Finite State Machines
**6.004 内容**：
- FSM 形式化定义（五元组）
- Moore 与 Mealy 机的区别
- 状态转移图与状态转移表
- FSM 状态编码策略：二进制、One-hot、Gray 码
- FSM 在控制逻辑中的应用

**本模块对应**：
- `include/fsm.h`: 定义了 FSM, FSMState, FSMTransition 结构
- `src/fsm.c`: 实现 `fsm_step()` 状态转移，`fsm_simulate()` 输入序列模拟
- 支持 Moore 和 Mealy 两种模型
- 示例：交通灯控制器、序列检测器、边沿检测器
- 文档 `demos/mini-traffic-light-fsm/README.md` 详细分析

**6.004 实验对应**：Lab 5 - FSM Design

### Chapter 8: Control Structures & Arithmetic
**6.004 内容**：
- ROM/PAL 编码的状态机
- 微码（microcode）控制
- ALU 设计与运算电路
- 进位选择加法器、超前进位加法器

**本模块对应**：
- `examples/alu_demo.c`: 完整的 8 位 ALU 门级仿真
- `examples/fsm_demo.c`: 交通灯 FSM + 序列检测器
- 文档 `demos/mini-alu/README.md` 详细分析 ALU 构建过程

**6.004 实验对应**：Lab 6 - ALU & Simple Processor

### Chapter 9: RTL Design
**6.004 内容**：
- RTL 设计方法学
- 数据通路与控制器分离
- 寄存器传输语言
- 数据通路元件：寄存器文件、ALU、MUX

**本模块对应**：
- `include/rtl_basic.h`: 定义了 RTLModule, RTLPort, RTLSignal 结构
- `src/rtl_basic.c`: 实现 RTL 信号管理与评估框架
- `demos/mini-simple-cpu-rtl/README.md`: 从 RTL 构建 CPU 的详细教程
- 文档 `docs/rtl-design-patterns.md` 覆盖常用 RTL 模式

**6.004 实验对应**：Lab 7 - RTL Design

### Chapter 10: Pipelining
**6.004 内容**：
- 流水线原理
- 流水线冒险：结构冒险、数据冒险、控制冒险
- 转发（forwarding/bypassing）
- 分支预测
- 流水线性能分析

**本模块对应**：
- `demos/mini-simple-cpu-rtl/README.md` 中讨论了单周期→多周期→流水线演进
- 列出了数据冒险转发和 branch prediction 的概念代码

**6.004 实验对应**：Lab 8 - Pipelined Processor（Beta）

---

## MIT 6.111/6.205 Correlation

| 6.111/6.205 Topic | mini-digital-circuit Coverage |
|-------------------|-------------------------------|
| Verilog/SystemVerilog for synthesis | N/A (纯 C 模拟，未做 HDL 对等) |
| FPGA architecture (LUT, FF, BRAM) | logic_gate + combinational 覆盖了 LUT 概念 |
| Timing constraints & clock domain crossing | sequential.c 中的 clk 边沿触发逻辑 |
| PLL & clock management | 简化版：dff_clock() 的上升沿触发 |
| IP integration (Xilinx/Altera) | N/A |
| High-level synthesis (HLS) | N/A |

---

## Summary / 总结

| 主题 | 6.004 覆盖 | 6.111/6.205 补充 | 本模块状态 |
|------|-----------|-----------------|-----------|
| 门电路与布尔代数 | Ch 4 |    | ✅ 完整 |
| 组合/算术电路 | Ch 5, 8 |    | ✅ 完整 |
| 时序逻辑 | Ch 6 |    | ✅ 完整 |
| FSM 设计 | Ch 7, 8 | FSM 在 FPGA 上的实现 | ✅ 完整 |
| RTL 设计 | Ch 9 | SystemVerilog RTL | ✅ 概念完整 |
| 流水线 CPU | Ch 10 | 时序约束与流水线 | ✅ 概念完整 |
| FPGA 相关 |    | LUT, FF, BRAM, DSP | ⚠️ 仅概念层面 |

---

## References / 参考文献

- MIT 6.004 Course Website: https://computationstructures.org/
- MIT 6.111/6.205 Course Website: https://fpga.mit.edu/
- Ward & Halstead, "Computation Structures" (MIT Press, 1990)
- Harris & Harris, "Digital Design and Computer Architecture" (ARM/RISC-V Edition)
