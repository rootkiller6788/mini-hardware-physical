# mini-digital-circuit — 数字电路 (C 语言实现)

> 参考 MIT 6.004 Computation Structures, MIT 6.111/6.205

## 模块-课程对应表

| 模块            | MIT 6.004 章节        | 核心内容                             |
|-----------------|-----------------------|--------------------------------------|
| `logic_gate`    | Ch 4: Combinational   | 基本门电路、真值表、布尔代数         |
| `combinational` | Ch 5, 8: Building Blocks | 半加器、全加器、行波进位加法器     |
| `sequential`    | Ch 6: Sequential      | D 触发器、寄存器、SR/D 锁存器        |
| `fsm`           | Ch 7-8: FSM & Control | Moore/Mealy 状态机、序列检测器       |
| `rtl_basic`     | Ch 9-10: RTL & Pipe   | 寄存器传输级、MUX/Decoder/Encoder    |

## 目录树

```
mini-digital-circuit/
├── include/
│   ├── logic_gate.h         # 门电路：AND/OR/NOT/NAND/NOR/XOR/XNOR
│   ├── combinational.h      # 组合逻辑：HalfAdder/FullAdder/RippleAdder
│   ├── sequential.h         # 时序逻辑：DFF/Register/SRLatch/DLatch
│   ├── fsm.h                # 有限状态机：Moore/Mealy
│   └── rtl_basic.h          # RTL基础：MUX/Decoder/Encoder
├── src/
│   ├── logic_gate.c
│   ├── combinational.c
│   ├── sequential.c
│   ├── fsm.c
│   └── rtl_basic.c
├── examples/
│   ├── gate_sim_demo.c      # 门电路仿真演示
│   ├── alu_demo.c           # 8-bit ALU 演示
│   ├── fsm_demo.c           # 交通灯 + 序列检测器 FSM
│   └── counter_demo.c       # 4-bit 计数器 + DFF/SR Latch
├── demos/
│   ├── mini-alu/            # ALU 构建教程（半加器→8-bit ALU）
│   ├── mini-traffic-light-fsm/  # 交通灯 FSM 教程
│   └── mini-simple-cpu-rtl/     # 最小 CPU RTL 构建教程
├── docs/
│   ├── course-alignment.md  # MIT 6.004/6.111 课程对应表
│   ├── logic-fundamentals.md # 布尔代数、卡诺图、De Morgan 定律
│   └── rtl-design-patterns.md # RTL 设计模式汇总
├── tests/
├── benches/
├── Makefile
└── README.md
```

## 编译与运行

```bash
# 编译所有示例
make

# 单独编译
make gate_sim_demo
make alu_demo
make fsm_demo
make counter_demo

# 运行
./bin/gate_sim_demo
./bin/alu_demo
./bin/fsm_demo
./bin/counter_demo

# 运行所有测试
make test

# 清理
make clean
```

## 核心能力

| 能力              | API 示例                                         |
|-------------------|--------------------------------------------------|
| 门电路评估        | `logic_eval(&gate)`                              |
| 真值表打印        | `logic_print_truth_table(GATE_XOR)`              |
| 组合逻辑网构建    | `comb_add_gate(&c, GATE_AND, inputs, 2, out_id)` |
| 全加器            | `Combination` + 5 gates per bit                  |
| D 触发器          | `dff_clock(&dff)` (正边沿触发)                   |
| 寄存器读写        | `reg_write(&r, val)` / `reg_read(&r)`            |
| FSM 状态转移      | `fsm_step(&fsm, input)`                          |
| FSM 序列模拟      | `fsm_simulate(&fsm, inputs, len)`                |
| RTL 端口管理      | `rtl_add_port(&m, "clk", RTL_PORT_IN, 1)`        |
| MUX/Decoder 构建  | `rtl_mux_create("m4", 4, 2)`                     |

## 依赖

- C99 编译器 (gcc/clang)
- libc + libm (仅标准库)
- 无其他外部依赖
