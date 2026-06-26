# mini-cpu-arch — CPU 架构 (C 语言实现)

> 参考 MIT 6.004 Computation Structures, MIT 6.175 RISC-V Processor Design, Stanford EE282 Advanced Computer Architecture

---

## Module Status: COMPLETE ✅

- **L1-L6**: Complete
- **L7**: Complete (4 applications: Amdahl scaling, MESI coherence, TLB+page walk, FR-FCFS scheduling)
- **L8**: Partial (MESI MOESI extension documented, PLRU tree algorithm)
- **L9**: Partial (Industry trends documented, not implemented)
- **include/ + src/**: 3201 lines (exceeds 3000-line threshold)

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
| `cache` | `include/cache.h` | `src/cache.c` | Cache hierarchy, MESI coherence, AMAT | Ch 14 | — | Lec 12-15 |
| `perf_model` | `include/perf_model.h` | `src/perf_model.c` | Amdahl/Gustafson, CPI stack, Roofline | — | — | Lec 1-2 |
| `memory_system` | `include/memory_system.h` | `src/memory_system.c` | TLB, page table walk, FR-FCFS controller | Ch 15-16 | — | Lec 12-13 |

---

## 目录树

```
mini-cpu-arch/
├── include/
│   ├── isa.h                RISC-V ISA 定义
│   ├── pipeline.h            5 级流水线
│   ├── superscalar.h         超标量双发射
│   ├── branch_pred.h         分支预测器
│   ├── ooo_exec.h            Tomasulo 乱序执行
│   ├── register_file.h       寄存器堆
│   ├── cache.h               Cache 层次 + MESI 协议
│   ├── perf_model.h          性能建模 (Amdahl/Gustafson/Roofline)
│   └── memory_system.h       TLB + 页表 + 内存控制器
├── src/
│   ├── isa.c
│   ├── pipeline.c
│   ├── superscalar.c
│   ├── branch_pred.c
│   ├── ooo_exec.c
│   ├── register_file.c
│   ├── cache.c               (542 行)
│   ├── perf_model.c          (283 行)
│   └── memory_system.c       (435 行)
├── examples/
│   ├── isa_demo.c            ISA 取指-译码-执行演示
│   ├── pipeline_sim_demo.c   流水线冒险与 forwarding 演示
│   ├── branch_pred_demo.c    5 种分支预测器精度对比
│   ├── ooo_demo.c            乱序执行 (Tomasulo) 演示
│   ├── tomasulo_demo.c       详细分步 Tomasulo 流程
│   └── mem_hierarchy_demo.c  缓存/MESI/AMAT/TLB/FR-FCFS 综合演示
├── demos/
├── docs/
├── tests/
├── benches/
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

## 九层知识覆盖 (L1-L9)

| Level | 名称 | 核心条目 | 状态 |
|-------|------|---------|------|
| **L1** | Definitions | RISC-V ISA structs, CacheGeometry, CPIStack, TLBEntry, MESIState, PageTableEntry, RooflineModel, PipelineReg, ROBEntry, ReservationStation | ✅ Complete |
| **L2** | Core Concepts | 5-stage pipeline, Tomasulo algorithm, branch prediction, cache hierarchy, virtual memory, CPI decomposition, Amdahl's Law, Gustafson's Law, MESI coherence, TLB | ✅ Complete |
| **L3** | Engineering Structures | Multi-level cache hierarchy (inclusive L1->L2->L3), ROB+RS+CDB, page table walk, forwarding unit, FR-FCFS memory controller, PLRU tree bits | ✅ Complete |
| **L4** | Standards/Theorems | Amdahl limit (1/serial), Gustafson linear scaling, AMAT formula, Iron Law of Performance, FLP connection to cache coherence, MESI state machine | ✅ Complete |
| **L5** | Algorithms | Tomasulo (issue/exec/CDB/commit), PLRU tree replacement, FR-FCFS scheduling, page table walk, bimodal/gshare/two-level prediction, forwarding with hazard detection | ✅ Complete |
| **L6** | Canonical Problems | End-to-end memory hierarchy demo (cache+MESI+TLB+FR-FCFS), 5-stage pipeline simulation, branch predictor accuracy comparison, Roofline bottleneck analysis | ✅ Complete |
| **L7** | Applications | Multi-core speedup prediction, TLB+page table MMU translation, cache hierarchy AMAT analysis, MESI multi-core coherence, memory controller scheduling optimization | ✅ Complete (5 apps) |
| **L8** | Advanced Topics | MOESI protocol extension (documented), PLRU tree approximation of LRU, multi-level inclusive cache inclusion policy, communication-aware Amdahl model | Partial+ |
| **L9** | Industry Frontiers | AI compilers / MLIR (documented), Confidential Computing (documented), DDR5/LPDDR5 controller evolution (documented) | Partial |

## 核心定理列表

| 定理 | 公式 | 来源 | 实现位置 |
|------|------|------|---------|
| Amdahl's Law | S(N) = 1 / ((1-f) + f/N) | Amdahl, 1967 | `perf_model.c:amdahl_speedup()` |
| Gustafson's Law | S(N) = N - α(N-1) | Gustafson, 1988 | `perf_model.c:gustafson_speedup()` |
| Iron Law | Time = IC × CPI × T_cycle | Hennessy & Patterson | `perf_model.c:cpi_stack_from_counters()` |
| AMAT | T_avg = T_hit + MR × MP | — | `cache.c:cache_hierarchy_amat()` |
| Roofline | GFLOPS = min(Peak, BW × OI) | Williams et al., 2009 | `perf_model.c:roofline_memory_bound_gflops()` |
| FLP (coherence) | MESI = consensus reduction | Fischer/Lynch/Paterson, 1985 | `cache.c:mesi_bus_*()` (documented) |

## 核心算法列表

| 算法 | 复杂度 | 位置 |
|------|--------|------|
| PLRU Tree Replacement | O(log W) per eviction | `cache.c:plru_update()` |
| FR-FCFS Memory Scheduling | O(N) per decision | `memory_system.c:memctrl_schedule()` |
| Tomasulo Issue/Exec/CDB/Commit | O(RS+ROB) per cycle | `ooo_exec.c:ooo_*()` |
| Page Table Walk | O(levels) per miss | `memory_system.c:mmu_translate()` |
| Bimodal/Gshare/Two-Level Prediction | O(1) per branch | `branch_pred.c:bp_predict()` |
| Optimal Processor Count | O(max_N) brute force | `perf_model.c:optimal_proc_count()` |

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
- 全分支条件处理 (BEQ/BNE/BLT/BGE/BLTU/BGEU)

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
- I-type 立即数正确捕获和传播

### 6. 寄存器堆 (`register_file`)
- 32-entry 寄存器文件
- 多端口读写模拟: 4 读端口 + 2 写端口
- x0 写保护

### 7. Cache 层次 (`cache`)
- 直接映射/组相联/全相联
- LRU/FIFO/Random/PLRU 替换策略
- Write-through/Write-back 写策略
- 多级层次 (L1I, L1D, L2, L3)
- AMAT 计算与推导
- MESI 缓存一致性协议 (完整状态机)

### 8. 性能建模 (`perf_model`)
- Amdahl's Law + 通信开销扩展
- Gustafson's Law (弱扩展)
- CPI Stack 分解 (base/cache/branch/pipeline/other)
- Roofline Model (计算/内存带宽上限)
- 最优处理器数量搜索
- 并行效率评估

### 9. 内存系统 (`memory_system`)
- TLB 模拟 (LRU/FIFO/Random 替换)
- 多级页表 (2-level 简化)
- 完整 MMU 地址翻译路径 (TLB → Page Walk)
- FR-FCFS 内存控制器调度算法
- Row-buffer hit/miss 延迟模型

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

## 九校课程映射

| 学校 | 课程 | 章节映射 | 本模块对应 |
|------|------|---------|-----------|
| **MIT** | 6.004 Computation Structures | Ch 9-16 (pipeline, cache, VM) | pipeline, cache, memory_system |
| **MIT** | 6.175 RISC-V Processor | Lab 1-8 (ISA → OOO) | isa, pipeline, ooo_exec |
| **Stanford** | EE282 Advanced CA | Lec 1-15 | superscalar, branch_pred, cache |
| **Berkeley** | CS 152 Computer Architecture | Cache/VM/TLB | cache, memory_system |
| **Berkeley** | CS 267 HPC | Roofline, Amdahl | perf_model |
| **CMU** | 15-418 Parallel Systems | Lec 6-8 (cache coherence) | cache (MESI) |
| **CMU** | 15-740 Advanced CA | OOO, superscalar | ooo_exec, superscalar |
| **UT Austin** | ECE 382V VLSI Design | Memory systems | memory_system (FR-FCFS) |
| **ETH** | 263-0006 Computer Architecture | Full pipeline design | pipeline, isa |
| **清华** | 计算机体系结构 | CPU 全套 | 全部模块 |

## 编码规范

- C99 标准, 仅依赖 libc + libm
- 函数: `snake_case`, 类型: `PascalCase`, 常量: `UPPER_SNAKE_CASE`
- 头文件保护: `#ifndef MODULE_H` / `#define MODULE_H` / `#endif`
- 所有头文件包含 `#include <stdbool.h>`
- 源文件包含对应 `.h` 文件
- 每个函数独立实现一个知识点，禁止函数桩
- 所有边界条件 (null pointer, zero division, OOM) 显式检查

---

## 参考资料

1. MIT 6.004: https://computationstructures.org/
2. MIT 6.175: http://csg.csail.mit.edu/6.175/
3. Stanford EE282: https://web.stanford.edu/class/ee282/
4. RISC-V Specification: https://riscv.org/technical/specifications/
5. Patterson & Hennessy, "Computer Organization and Design RISC-V Edition"
6. Hennessy & Patterson, "Computer Architecture: A Quantitative Approach"
