# mini-simple-cpu-rtl — 从 RTL 模块构建最小 CPU

## Overview / 概述

本文档展示如何使用 RTL（Register Transfer Level）设计方法，从基本模块（寄存器文件、ALU、指令译码器、控制单元）构建一个最小的 8 指令 CPU。

This document demonstrates building a minimal 8-instruction CPU using RTL methodology from basic modules: register file, ALU, instruction decoder, and control unit.

---

## Theory / 理论背景

### RTL Design Methodology / RTL 设计方法

RTL（Register Transfer Level）是数字系统设计的一种抽象层次，介于行为级和门级之间。RTL 描述数据在寄存器之间的流动以及对这些数据执行的操作。

**核心概念**：
- 系统状态存储在寄存器中
- 每个时钟周期，数据在寄存器之间通过组合逻辑传输
- 状态转移由时钟边沿触发

### Von Neumann Architecture / 冯·诺依曼架构

```
+-----------------------------------------------+
|                  CPU                           |
|  ┌──────────┐    ┌──────────┐                 |
|  │ Control  │◄───│Instruction│                 |
|  │  Unit    │    │ Decoder  │                 |
|  └────┬─────┘    └──────────┘                 |
|       │                                       |
|  ┌────┴─────┐    ┌──────────┐    ┌─────────┐ |
|  │ Register │◄──►│   ALU    │◄──►│  Data   │ |
|  │  File    │    │          │    │ Memory  │ |
|  └──────────┘    └──────────┘    └─────────┘ |
|                                               |
|  ┌──────────┐                                 |
|  │ Program  │                                 |
|  │ Counter  │                                 |
|  └──────────┘                                 |
+-----------------------------------------------+
```

---

## Instruction Set Architecture / 指令集架构

我们设计一个简单的 8 指令 RISC 风格 ISA。

### Instruction Format / 指令格式 (16-bit)

```
 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
┌─────────────┬──────┬──────┬──────┬─────────────┐
│   Opcode    │  Rd  │  Rs1 │  Rs2 │  Immediate  │
│   4 bits    │3 bits│3 bits│3 bits│   3 bits    │
└─────────────┴──────┴──────┴──────┴─────────────┘
```

### Instruction Set / 指令集

| Opcode | Mnemonic | Format       | Operation            | Description        |
|--------|----------|--------------|----------------------|--------------------|
| 0000   | ADD      | R-type       | Rd = Rs1 + Rs2       | 加法               |
| 0001   | SUB      | R-type       | Rd = Rs1 - Rs2       | 减法               |
| 0010   | AND      | R-type       | Rd = Rs1 & Rs2       | 按位与             |
| 0011   | OR       | R-type       | Rd = Rs1 \| Rs2      | 按位或             |
| 0100   | XOR      | R-type       | Rd = Rs1 ^ Rs2       | 按位异或           |
| 0101   | ADDI     | I-type       | Rd = Rs1 + Imm       | 立即数加法         |
| 0110   | LW       | I-type       | Rd = M[Rs1 + Imm]    | 加载字             |
| 0111   | SW       | I-type       | M[Rs1 + Imm] = Rd    | 存储字             |

### Register File / 寄存器文件 (8 × 8-bit)

| Register | Name  | Convention  |
|----------|-------|-------------|
| R0       | zero  | Hardwired 0 |
| R1       | at    | Assembler temp|
| R2-R3    | v0-v1 | Return values|
| R4-R5    | a0-a1 | Arguments   |
| R6       | t0    | Temporary   |
| R7       | sp    | Stack pointer|

---

## CPU Datapath Design / 数据通路设计

### ASCII Diagram / 原理图

```
                    ┌──────────────────────────┐
                    │     Instruction Memory    │
                    │          (IMEM)           │
                    └──────────┬───────────────┘
                               │ Inst[15:0]
                               ▼
                    ┌──────────────────────┐
                    │  Instruction Decode  │
                    └──┬───┬───┬───┬──────┘
              Opcode  │   │   │   │  Immediate
                 │    │   │   │   │
    ┌────────────┼────┼───┼───┼───┼─────────────┐
    │  Control   │    │   │   │   │             │
    │   Unit     │    │   │   │   │             │
    └──┬────┬────┘    │   │   │   │             │
       │    │         │   │   │   │             │
       │    │    ┌────┴───┴───┴───┴───┐         │
       │    │    │   Register File    │         │
       │    │    │    8 × 8-bit       │         │
       │    │    └──┬──────┬─────────┘         │
       │    │  Rs1 │      │ Rs2                │
       │    │      │      │                    │
       │    │      ▼      ▼                    │
       │    │   ┌─────────────┐                │
       │    └──►│    ALU      │                │
       │ ALUop  │  8-bit      │                │
       │        └──────┬──────┘                │
       │               │ Result                │
       │               ▼                       │
       │        ┌─────────────┐                │
       │        │ Data Memory │                │
       │        │   (DMEM)    │                │
       │        └─────────────┘                │
       │                                      │
       └─────── RegWrite, MemWrite, etc.      │
```

---

## Module Implementations / 模块实现

### 1. Register File / 寄存器文件

```c
#include "sequential.h"
#include "rtl_basic.h"

/* 8 × 8-bit register file */
Register reg_file = reg_create(8);  // 8 个 8-bit DFF 寄存器

void rf_write(int reg_id, unsigned long long value) {
    if (reg_id == 0) return;  // R0 硬连线为 0
    // 使用 reg 结构写入
}

unsigned long long rf_read(int reg_id) {
    if (reg_id == 0) return 0;
    return reg_read_by_id(reg_id);
}
```

### 2. ALU / 算术逻辑单元

```c
typedef enum {
    ALU_ADD, ALU_SUB, ALU_AND,
    ALU_OR,  ALU_XOR, ALU_PASS
} AluOp;

unsigned char alu_execute(AluOp op, unsigned char a, unsigned char b) {
    switch (op) {
    case ALU_ADD: return a + b;
    case ALU_SUB: return a - b;
    case ALU_AND: return a & b;
    case ALU_OR:  return a | b;
    case ALU_XOR: return a ^ b;
    case ALU_PASS: return b;
    default: return 0;
    }
}
```

### 3. Instruction Decoder / 指令译码器

```c
typedef struct {
    unsigned char opcode;    // bits [15:12]
    unsigned char rd;        // bits [11:9]
    unsigned char rs1;       // bits [8:6]
    unsigned char rs2;       // bits [5:3]
    unsigned char imm;       // bits [2:0]
} DecodedInst;

DecodedInst decode(unsigned short inst) {
    DecodedInst di;
    di.opcode = (inst >> 12) & 0xF;
    di.rd     = (inst >> 9)  & 0x7;
    di.rs1    = (inst >> 6)  & 0x7;
    di.rs2    = (inst >> 3)  & 0x7;
    di.imm    = inst & 0x7;
    return di;
}
```

### 4. Control Unit / 控制单元

```c
typedef struct {
    bool reg_write;
    bool mem_read;
    bool mem_write;
    bool alu_src;      // 0=register, 1=immediate
    unsigned char alu_op;
    bool branch;
} ControlSignals;

ControlSignals control_unit(unsigned char opcode) {
    ControlSignals cs = {0};
    switch (opcode) {
    case 0x0: // ADD
        cs.reg_write = true;
        cs.alu_op = 0;
        cs.alu_src = false;
        break;
    case 0x5: // ADDI
        cs.reg_write = true;
        cs.alu_op = 0;
        cs.alu_src = true;
        break;
    case 0x6: // LW
        cs.reg_write = true;
        cs.mem_read = true;
        cs.alu_op = 0;
        cs.alu_src = true;
        break;
    case 0x7: // SW
        cs.mem_write = true;
        cs.alu_op = 0;
        cs.alu_src = true;
        break;
    // ... 其他指令
    }
    return cs;
}
```

---

## Single-Cycle CPU / 单周期 CPU

### Datapath Stages / 数据通路阶段

单周期 CPU 在一个时钟周期内完成以下五个阶段：

1. **IF** (Instruction Fetch): 从程序存储器取指令
2. **ID** (Instruction Decode): 译码指令，读取寄存器
3. **EX** (Execute): ALU 执行运算
4. **MEM** (Memory Access): 访存（LW/SW）
5. **WB** (Write Back): 结果写回寄存器文件

### C Implementation / C 实现 - main loop

```c
#define IMEM_SIZE 256
#define DMEM_SIZE 256

unsigned short imem[IMEM_SIZE];  // 程序存储器
unsigned char  dmem[DMEM_SIZE];  // 数据存储器
unsigned char  regs[8];          // 寄存器文件
unsigned short pc = 0;           // 程序计数器

void cpu_cycle(void) {
    /* IF: 取指 */
    unsigned short inst = imem[pc];

    /* ID: 译码 */
    DecodedInst di = decode(inst);
    ControlSignals cs = control_unit(di.opcode);
    unsigned char rs1_val = regs[di.rs1];
    unsigned char rs2_val = regs[di.rs2];

    /* EX: 执行 */
    unsigned char alu_b = cs.alu_src ? di.imm : rs2_val;
    unsigned char alu_result = alu_execute(cs.alu_op, rs1_val, alu_b);

    /* MEM: 访存 */
    if (cs.mem_read) {
        alu_result = dmem[alu_result];
    }
    if (cs.mem_write) {
        dmem[alu_result] = rs2_val;
    }

    /* WB: 写回 */
    if (cs.reg_write && di.rd != 0) {
        regs[di.rd] = alu_result;
    }

    pc++;
}
```

### Test Program / 测试程序

```c
/* 简单程序：计算 3 + 5，结果存入 R3 */
imem[0] = 0x5023;  // ADDI R1, R0, 3   (R1 = 0 + 3)
imem[1] = 0x5045;  // ADDI R2, R0, 5   (R2 = 0 + 5)
imem[2] = 0x0623;  // ADD  R3, R1, R2  (R3 = R1 + R2)

for (int i = 0; i < 3; i++) {
    cpu_cycle();
}

printf("R3 = %d (expected 8)\n", regs[3]);
```

### Expected Output / 预期输出

```
Cycle 0: ADDI R1, R0, 3  → R1 = 3
Cycle 1: ADDI R2, R0, 5  → R2 = 5
Cycle 2: ADD  R3, R1, R2 → R3 = 8

Final register values:
  R0=0  R1=3  R2=5  R3=8  R4=0  R5=0  R6=0  R7=0
```

---

## Extending the CPU / CPU 扩展

### Multi-Cycle Implementation / 多周期实现

将 5 个阶段分散到 5 个时钟周期：

```c
typedef enum { IF, ID, EX, MEM, WB } CpuStage;
CpuStage stage = IF;

void cpu_multi_cycle(void) {
    switch (stage) {
    case IF: /* 取指 */ stage = ID; break;
    case ID: /* 译码 */ stage = EX; break;
    case EX: /* 执行 */ stage = MEM; break;
    case MEM: /* 访存 */ stage = WB; break;
    case WB: /* 写回 */ stage = IF; pc++; break;
    }
}
```

### Pipelined Implementation / 流水线实现

5 级流水线，每级之间有流水线寄存器：

```
IF/ID  →  ID/EX  →  EX/MEM  →  MEM/WB
```

需要处理数据冒险（forwarding）和控制冒险（branch prediction）。

### Adding Instructions / 添加指令

扩展指令集示例：

| Opcode | Mnemonic | Description    |
|--------|----------|----------------|
| 1000   | BEQ      | Branch if equal|
| 1001   | JMP      | Unconditional jump|
| 1010   | SLT      | Set less than  |
| 1011   | NOR      | Bitwise NOR    |

---

## Comparison with LC-3 / 与 LC-3 对比

| Feature        | Our CPU    | LC-3        |
|----------------|-----------|-------------|
| Word size      | 8-bit     | 16-bit      |
| Registers      | 8         | 8 (R0-R7)   |
| Instructions   | 8         | 15          |
| Addressing     | Register + Imm | PC-relative |
| Data memory    | 256 bytes | 64K words   |

---

## Performance Metrics / 性能指标

| Metric               | Single-Cycle | Multi-Cycle | Pipelined |
|----------------------|-------------|-------------|-----------|
| CPI                  | 1           | 3-5         | ~1        |
| Clock period         | Long        | Short       | Short     |
| Throughput           | 1 inst/cycle| 0.25/cycle  | ~1/cycle  |
| Gate count           | ~2000       | ~1500       | ~3000     |

---

## Build & Run / 编译运行

The CPU simulation can be built using any of the demo targets:

```bash
cd mini-digital-circuit
make  # builds all demos
./bin/alu_demo      # ALU component
./bin/counter_demo  # Register/counter component
./bin/fsm_demo      # FSM control logic
```

To build a standalone CPU simulation:

```bash
gcc -Wall -Wextra -O2 -I include -o bin/cpu_sim \
    examples/cpu_sim.c \
    src/sequential.c src/combinational.c \
    src/logic_gate.c src/fsm.c src/rtl_basic.c -lm
./bin/cpu_sim
```

---

## References / 参考文献

- MIT 6.004, Chapters 9-10: RTL, Pipelining
- MIT 6.111/6.205, Lectures 10-12: CPU Design
- Patterson & Hennessy, "Computer Organization and Design" (RISC-V Edition)
- Yale Patt, "Introduction to Computing Systems" (LC-3)
