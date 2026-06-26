# Course Alignment — 课程对齐

> Mapping this module to MIT 6.004, MIT 6.175, and Stanford EE282.

---

## Module-to-Course Mapping

| This Module | MIT 6.004 (Computation Structures) | MIT 6.175 (RISC-V) | Stanford EE282 (Adv. Arch) |
|-------------|-----------------------------------|---------------------|---------------------------|
| `isa` | Ch 11: ISA design, RISC vs CISC | Lab 1-3: RISC-V ISA, encoding | Lecture 1-2: ISA fundamentals |
| `register_file` | Ch 9: Register files, datapath | Lab 4-6: Pipeline datapath | Lecture 3: Register renaming |
| `pipeline` | Ch 9-10: 5-stage pipeline, hazards | Lab 4-6: Pipeline implementation | Lecture 4-5: Pipelining |
| `superscalar` | — | Lab 7-8: Dual-issue | Lecture 6-8: Superscalar |
| `branch_pred` | Ch 10: Branch hazards, prediction | Lab 6: Branch prediction | Lecture 9-11: Branch prediction |
| `ooo_exec` | — | Lab 7-8: OOO execution | Lecture 12-15: OOO, Tomasulo |

---

## MIT 6.004 — Computation Structures

### Chapter Coverage

| Chapter | Topic | Our Implementation |
|---------|-------|-------------------|
| 5 | Sequential logic, FSMs | Pipeline state machines in `pipeline.c` |
| 7-8 | ALU design | Instruction execution in `isa.c` |
| 9 | Single-cycle processor, datapath | `ISAContext` structures |
| 10 | Pipelined processor, hazards | 5-stage pipeline with forwarding |
| 11 | ISA design principles | `isa.h` opcode enum, encoding |
| 12 | Virtual memory | — (future work) |
| 13 | Caches | `demos/mini-cache-sim/` |

### Key 6.004 Concepts Implemented

1. **Stored program concept**: Instructions in `ISAContext.memory[]`, PC advances
2. **Datapath and control**: Execution logic with ALU, memory, register file
3. **Pipeline stages**: IF/ID/EX/MEM/WB with inter-stage registers
4. **Hazard detection**: Forwarding unit resolves data hazards
5. **Branch handling**: Multiple predictor types for control hazards

---

## MIT 6.175 — Constructive Computer Architecture (RISC-V)

### Lab Alignment

| Lab | Topic | Our Module Component |
|-----|-------|---------------------|
| Lab 1 | RISC-V assembly programming | `examples/isa_demo.c` encoding examples |
| Lab 2 | Single-cycle processor | `src/isa.c` instruction execution |
| Lab 3 | 2-stage pipeline | `src/pipeline.c` (extended to 5 stages) |
| Lab 4 | 5-stage pipeline with forwarding | `src/pipeline.c` forwarding unit |
| Lab 5 | Branch prediction | `src/branch_pred.c` 4 predictor types |
| Lab 6 | Cache design | `demos/mini-cache-sim/` |
| Lab 7 | Superscalar execution | `src/superscalar.c` dual-issue |
| Lab 8 | Out-of-order execution | `src/ooo_exec.c` Tomasulo algorithm |

### Key 6.175 Concepts Implemented

1. **Bluespec-like decomposition**: Each stage is a module with well-defined interface
2. **RISC-V instruction encoding**: Full RV32I decode in `isa_decode()`
3. **Hazard resolution**: Forwarding paths and stall logic
4. **Scoreboarding/Tomasulo**: Reservation stations, ROB, CDB
5. **Functional unit modeling**: ALUs, load/store units in `ooo_exec.c`

---

## Stanford EE282 — Advanced Computer Architecture

### Lecture Mapping

| Lecture | Topic | Module Coverage |
|---------|-------|-----------------|
| 1-2 | ISA Fundamentals | `include/isa.h` — RISC-V subset |
| 3 | Pipelining Basics | `include/pipeline.h` — 5-stage IF/ID/EX/MEM/WB |
| 4 | Pipeline Hazards | `docs/pipeline-hazards.md` — forwarding, stalling |
| 5 | Branch Prediction I | `src/branch_pred.c` — bimodal, 2-bit counters |
| 6 | Branch Prediction II | `src/branch_pred.c` — 2-level, gshare |
| 7 | Superscalar I | `src/superscalar.c` — dual-issue, ROB |
| 8 | Superscalar II | `src/superscalar.c` — dispatch, commit |
| 9 | Out-of-Order I | `src/ooo_exec.c` — reservation stations |
| 10 | Out-of-Order II | `src/ooo_exec.c` — Tomasulo algorithm |
| 11 | Memory Hierarchy | `demos/mini-cache-sim/` — cache simulation |
| 12 | Prefetching | Not implemented (future work) |
| 13 | Multi-core | Not implemented (future work) |

### Key EE282 Concepts Implemented

1. **Superscalar dispatch**: Two-wide issue queue with operand readiness tracking
2. **Register renaming**: Via ROB index assignment in `ooo_issue()`
3. **Common Data Bus**: Broadcast mechanism in `ooo_write_result()`
4. **In-order commit**: ROB head-based commit in `ooo_commit()`
5. **Load-use hazard handling**: Stalls when load result needed immediately
6. **Forwarding logic**: MEM-to-EX and WB-to-EX forwarding paths

---

## Learning Progression / 学习路径

```
Week 1-2: ISA (isa.h, isa.c, register_file.c)
  └── Understand instruction encoding, execution model

Week 3-4: Pipeline (pipeline.h, pipeline.c)
  └── 5-stage pipeline, forwarding, hazards

Week 5-6: Branch Prediction (branch_pred.h, branch_pred.c)
  └── Static vs dynamic, 2-bit counters, correlating

Week 7-8: Superscalar (superscalar.h, superscalar.c)
  └── Multi-issue, ROB, dispatch/commit

Week 9-10: OOO Execution (ooo_exec.h, ooo_exec.c)
  └── Tomasulo, reservation stations, CDB
```

---

## Assessment / 评估

Each module component can be tested independently:

| Component | Test Method | Expected Understanding |
|-----------|-------------|----------------------|
| ISA | Run `isa_demo` with hand-crafted programs | Instruction encoding, execution flow |
| Pipeline | Run `pipeline_sim_demo`, observe cycle diagrams | Stage transitions, data hazards |
| Branch Pred | Run `branch_pred_demo`, compare accuracies | Prediction strategies, counter states |
| OOO | Run `ooo_demo`, `tomasulo_demo`, trace RS/ROB | Dynamic scheduling, renaming, commit order |

---

## References / 参考文献

1. MIT 6.004 Course Notes: https://computationstructures.org/
2. MIT 6.175 Lab Manual (RISC-V): http://csg.csail.mit.edu/6.175/
3. Stanford EE282 Lecture Slides (Christos Kozyrakis)
4. Patterson & Hennessy, "Computer Organization and Design RISC-V Edition"
5. Hennessy & Patterson, "Computer Architecture: A Quantitative Approach"
