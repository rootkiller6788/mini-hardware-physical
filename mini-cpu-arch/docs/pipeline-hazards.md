# Pipeline Hazards — 流水线冒险

> Comprehensive analysis of structural, data, and control hazards in pipelined processors with solutions: forwarding, stalling, and branch prediction.

---

## Overview / 概览

Pipeline hazards are situations that prevent the next instruction from executing in the next clock cycle. Understanding and resolving hazards is fundamental to designing high-performance pipelined processors.

### Hazard Taxonomy

```
Pipeline Hazards
|
+-- Structural Hazards
|   +-- Resource conflict (single memory port)
|   +-- Functional unit conflict
|
+-- Data Hazards
|   +-- RAW (Read After Write)  — TRUE dependency
|   +-- WAR (Write After Read)  — ANTI dependency (OOO only)
|   +-- WAW (Write After Write) — OUTPUT dependency (OOO only)
|
+-- Control Hazards
    +-- Branch misprediction
    +-- Jump target resolution
    +-- Exception/interrupt
```

---

## Structural Hazards / 结构冒险

### Problem

Two instructions in different stages need the same hardware resource simultaneously.

```
Example: Single memory port
  Cycle N:   IF needs memory to fetch I5
  Cycle N:   MEM needs memory to load data for I1
  CONFLICT:  Only one can access memory!
```

### Solutions

| Solution | Description | Cost |
|----------|-------------|------|
| Stall | Delay one instruction until resource is free | Performance loss |
| Duplicate | Separate I-cache and D-cache (Harvard architecture) | Area cost |
| Pipelined resource | Multi-cycle resource with pipelined access | Latency increase |
| Multi-port | Multiple read/write ports on register file | Power, area cost |

### Implementation in Our Code

```c
// Harvard architecture: separate instruction and data access
// In pipeline.c, fetch reads from isa.memory[]
// Memory stage also reads/writes isa.memory[]
// If single-ported: must stall on load/store conflicts
```

---

## Data Hazards / 数据冒险

### RAW (Read After Write) — True Dependency

The most common and dangerous hazard type.

```
I1: ADD  x3, x1, x2     // writes x3 in WB (cycle 5)
I2: SUB  x5, x3, x4     // reads x3 in ID (cycle 3)
                          ^^^^^  STALE VALUE!
```

Pipeline timing:
```
Cycle:   1    2    3    4    5
I1:      IF   ID   EX   MEM  WB (x3 written here)
I2:           IF   ID   EX   MEM  WB
                   ^--- x3 read here (2 cycles too early!)
```

### Solution 1: Stalling (Pipeline Interlock)

Insert bubbles until the producing instruction completes.

```
Cycle:   1    2    3    4    5    6    7
I1:      IF   ID   EX   MEM  WB
I2:           IF   ID   *    *    EX   MEM  WB
                         ^----^--- stall slots
```

Cost: 2 lost cycles per dependent instruction.

### Solution 2: Forwarding (Bypassing)

Route ALU result directly to next instruction's input.

```
Cycle:   1    2    3    4    5
I1:      IF   ID   EX   MEM  WB
I2:           IF   ID   EX   MEM  WB
                   ^--- forwarded from EX/MEM
```

Our forwarding implementation:

```c
void forwarding_unit(Pipeline* p) {
    // EX/MEM -> ID/EX forward
    if (p->ex_mem.valid && p->ex_mem.reg_write_en &&
        p->ex_mem.rd != 0 && p->ex_mem.rd == p->id_ex.rs1) {
        p->forward_a = true;
        p->forward_a_val = p->ex_mem.alu_result;
    }

    // MEM/WB -> ID/EX forward (lower priority)
    if (p->mem_wb.valid && p->mem_wb.reg_write_en &&
        p->mem_wb.rd != 0 && p->mem_wb.rd == p->id_ex.rs1 &&
        !(/* check EX/MEM already forwarding to same reg */)) {
        p->forward_a = true;
        p->forward_a_val = p->mem_wb.alu_result;
    }

    // Same for rs2 (forward_b)
}
```

### The Load-Use Hazard (Unavoidable Stall)

When a load is immediately followed by an instruction using the loaded value:

```
I1: LW   x3, 0(x1)    // data available after MEM (cycle 4)
I2: ADD  x5, x3, x4   // needs x3 in EX (cycle 3!)
```

Even with forwarding, the load data is not ready until MEM stage, so we MUST stall:

```
Cycle:   1    2    3    4    5    6
I1:      IF   ID   EX   MEM  WB
I2:           IF   ID   *    EX   MEM  WB
                        ^--- 1-cycle stall
```

```c
static bool detect_load_use_hazard(Pipeline* p) {
    if (p->id_ex.mem_read &&
        (p->id_ex.rd == p->if_id.rs1 || p->id_ex.rd == p->if_id.rs2)) {
        return true;
    }
    return false;
}
```

### WAR and WAW Hazards (OOO Only)

These "false" dependencies only arise with out-of-order execution:

```
WAR: I1 writes x3 AFTER I2 reads x3 (but I2 executed first in OOO)
WAW: I1 and I2 both write x3 (I2 must appear to write last)
```

Both solved by **register renaming** — assign a unique physical register/ROB entry for each write.

---

## Control Hazards / 控制冒险

### Branch Pipeline Penalty

```
BEQ x1, x2, target     // branch resolved in EX (cycle 3)
                         // but two instructions fetched speculatively!
Cycle:   1    2    3    4
BEQ:     IF   ID   EX   MEM  WB
I+1:          IF   ID   *    (squashed if taken)
I+2:               IF   *    (squashed if taken)
```

### Solutions

| Solution | Description | Misprediction Penalty |
|----------|-------------|----------------------|
| Freeze pipeline | Stall until branch resolved | 2 cycles |
| Predict not-taken | Continue fetching sequential | 0 if NT, 2 if T |
| Predict taken | Fetch from branch target | 0 if T, 2 if NT |
| Static BTFN | Backward taken, forward not taken | ~1 cycle avg |
| Dynamic prediction | 2-bit counter, correlating, etc. | 0-1 cycle avg |

---

## Pipeline Diagrams / 流水线图示

### No Hazard (Ideal)

```
    1     2     3     4     5     6     7     8
I1  IF    ID    EX    MEM   WB
I2        IF    ID    EX    MEM   WB
I3              IF    ID    EX    MEM   WB
I4                    IF    ID    EX    MEM   WB
I5                          IF    ID    EX    MEM   WB
```

### Data Hazard with Forwarding (1 gap)

```
    1     2     3     4     5     6     7     8
I1  IF    ID    EX    MEM   WB
I2        IF    ID    EX    MEM   WB     (I2 uses I1 result with forwarding)
I3              IF    ID    EX    MEM   WB
```

### Data Hazard Without Forwarding (3 gaps)

```
    1     2     3     4     5     6     7     8     9
I1  IF    ID    EX    MEM   WB
I2        IF    ID    *     *     EX    MEM   WB
I3              IF    *     *     ID    EX    MEM   WB
```

### Branch Misprediction (2 squashed)

```
    1     2     3     4     5     6     7
B    IF    ID    EX    MEM   WB
I1        IF    ID    *     *     IF    ID    (restart)
I2              IF    *     *           IF    (squashed twice!)
```

---

## Hazard Statistics / 冒险统计

From Hennessy & Patterson (typical SPECint):

| Hazard Type | Frequency | Cycles Lost (ideal) | Cycles Lost (forwarding) |
|-------------|-----------|---------------------|--------------------------|
| Load-use | 25% of loads | 1 per | 1 (must stall) |
| RAW (non-load) | 15% of ALU | 2 per | 0 (forwarded) |
| Branch misprediction | varies | 2 per mispredict | 0-1 (dynamic predictor) |
| Structural | rare | 1 per conflict | 0 (split I/D cache) |

---

## Implementation in Our Codebase

| File | Hazard Handling |
|------|----------------|
| `src/pipeline.c` | Forwarding unit, load-use stall detection, branch flushing |
| `src/superscalar.c` | ROB-based register renaming for WAR/WAW elimination |
| `src/ooo_exec.c` | Full renaming via ROB, RS operand tagging, CDB broadcast |
| `src/branch_pred.c` | 5 predictor types to minimize control hazard penalty |

---

## References / 参考

- Hennessy & Patterson, "Computer Architecture: A Quantitative Approach", Appendix C
- Patterson & Hennessy, "Computer Organization and Design", Chapter 4
- MIT 6.004 Lecture Notes: Pipelined Beta
- MIT 6.175 Pipeline Hazard Lab
- "A Pipelined Multi-core MIPS Machine" (Stratford et al., 2018)
