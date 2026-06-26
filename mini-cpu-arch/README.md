# mini-cpu-arch — CPU 架构 (C 语言实现)

> 参考 MIT 6.004 Computation Structures, MIT 6.175 RISC-V Processor Design, Stanford EE282 Advanced Computer Architecture

---

## 模块-课程对照表

| 模块 | 头文件 | 源文件 | 核心概念 | MIT 6.004 | MIT 6.175 | EE282 |
|------|--------|--------|----------|-----------|-----------|-------|
| `isa` | `include/isa.h` | `src/isa.c` | RISC-V RV32I, fetch-decode-execute | Ch 11 | Lab 1-3 | Lec 1-2 |
| `register_file` | `include/register_file.h` | `src/register_file.c` | 多端口寄存器堆, x0 保护 | Ch 9 | Lab 4 | Lec 3 |
| `pipeline` | `include/pipeline.h` | `src/pipeline.c` | 5 级流水线, forwarding, 冒险 | Ch 9-10 | Lab 4-6 | Lec 4-5 |
| `superscalar` | `include/superscalar.h` | `src/superscalar.c` | 双发射, ROB, 乱序提交 | — | Lab 7-8 | Lec 6-8 |
| `branch_pred` | `include/branch_pred.h` | `src/branch_pred.c` | 分支预测: bimodal, gshare, 锦标赛 | Ch 10 | Lab 6 | Lec 9-11 |
| `ooo_exec` | `include/ooo_exec.h` | `src/ooo_exec.c` | Tomasulo 算法, RS, CDB | — | Lab 7-8 | Lec 12-15 |

---

## 目录树

```
mini-cpu-arch/
├── include/
│   ├── isa.h               RISC-V ISA 定义
│   ├── pipeline.h           5 级流水线
│   ├── superscalar.h        超标量双发射
│   ├── branch_pred.h        分支预测器
│   ├── ooo_exec.h           Tomasulo 乱序执行
│   └── register_file.h      寄存器堆
├── src/
│   ├── isa.c
│   ├── pipeline.c
│   ├── superscalar.c
│   ├── branch_pred.c
│   ├── ooo_exec.c
│   └── register_file.c
├── examples/
│   ├── isa_demo.c           ISA 取指-译码-执行演示
│   ├── pipeline_sim_demo.c  流水线冒险与 forwarding 演示
│   ├── branch_pred_demo.c   4 种分支预测器精度对比
│   ├── ooo_demo.c           乱序执行 (Tomasulo) 演示
│   └── tomasulo_demo.c      详细分步 Tomasulo 流程
├── demos/
│   ├── mini-riscv-interpreter/
│   │   └── README.md        微型 RISC-V 解释器
│   ├── mini-tomasulo/
│   │   └── README.md        Tomasulo 算法深度解析
│   ├── mini-branch-predictor/
│   │   └── README.md        分支预测技术综述
│   └── mini-cache-sim/
│       └── README.md        CPU 缓存层次模拟
├── docs/
│   ├── course-alignment.md  课程对齐
│   ├── pipeline-hazards.md  流水线冒险详解
│   ├── branch-prediction-survey.md  分支预测综述
│   └── ooo-execution.md    乱序执行详解
├── tests/                   (预留测试目录)
├── benches/                 (预留基准测试目录)
├── Makefile
└── README.md
```

---

## 构建命令

```bash
# 构建所有演示程序
make

# 构建单个程序
make isa_demo
make pipeline_sim_demo
make branch_pred_demo
make ooo_demo
make tomasulo_demo

# 运行所有演示 (测试)
make test

# 清理
make clean
```

---

## 核心能力

### 1. ISA 模拟 (`isa`)
- RISC-V RV32I 子集: ADD, SUB, ADDI, LW, SW, BEQ, JAL 等 30+ 条指令
- 6 种指令格式解码 (R/I/S/B/U/J)
- 寄存器堆 x0-x15, x0 硬连线为 0
- 4096 字节 byte-addressable 内存
- 取指-译码-执行单步跟踪

### 2. 流水线模拟 (`pipeline`)
- 经典 5 级流水线: IF → ID → EX → MEM → WB
- 级间寄存器: IF/ID, ID/EX, EX/MEM, MEM/WB
- Forwarding 单元: EX/MEM → ID/EX, MEM/WB → ID/EX
- Load-use 冒险检测 + stall 插入
- 分支 misprediction 气泡刷新

### 3. 分支预测 (`branch_pred`)
- 5 种预测器: Always Taken, Always Not Taken, Bimodal, Two-Level, Gshare
- 256-entry BHT, 64-column PHT, 6-bit GHR
- 2-bit 饱和计数器 (SN/WN/WT/ST)
- 精度统计: predict/update/accuracy API
- 可扩展至 tournament, perceptron, TAGE

### 4. 超标量 (`superscalar`)
- 双路发射 (2-way issue)
- 32-entry Reorder Buffer
- In-order issue, out-of-order completion, in-order commit
- 操作数就绪检测, ROB-based register renaming

### 5. 乱序执行 (`ooo_exec`)
- Tomasulo 算法完整实现
- 16-entry Reservation Stations
- 32-entry ROB
- Common Data Bus (CDB) 广播
- 寄存器重命名 (ROB tag 映射)
- 功能单元: 2 ALU + 1 Load/Store

### 6. 寄存器堆 (`register_file`)
- 32-entry 寄存器文件
- 多端口读写模拟: 4 读端口 + 2 写端口
- x0 写保护

---

## 使用示例

```c
#include "isa.h"
#include "pipeline.h"

int main(void) {
    ISAContext ctx;
    isa_init(&ctx);

    uint32_t prog[] = { 0x00A00093, 0x01400113, 0x002081B3 };
    isa_load_program(&ctx, prog, 3);

    for (int i = 0; i < 3; i++) isa_step(&ctx);
    isa_dump_registers(&ctx);
    return 0;
}
```

---

## 编码规范

- C99 标准, 仅依赖 libc + libm
- 函数: `snake_case`, 类型: `PascalCase`, 常量: `UPPER_SNAKE_CASE`
- 头文件保护: `#ifndef MODULE_H` / `#define MODULE_H` / `#endif`
- 所有头文件包含 `#include <stdbool.h>`
- 源文件包含对应 `.h` 文件

---

## 参考资料

1. MIT 6.004: https://computationstructures.org/
2. MIT 6.175: http://csg.csail.mit.edu/6.175/
3. Stanford EE282: https://web.stanford.edu/class/ee282/
4. RISC-V Specification: https://riscv.org/technical/specifications/
5. Patterson & Hennessy, "Computer Organization and Design RISC-V Edition"
6. Hennessy & Patterson, "Computer Architecture: A Quantitative Approach"
