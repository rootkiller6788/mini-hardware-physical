# mini-tomasulo — 托马苏洛算法深度解析

> Deep dive into Tomasulo's algorithm: reservation stations, register renaming, common data bus, reorder buffer, and precise exceptions.

---

## Overview / 概览

Tomasulo's algorithm, developed by Robert Tomasulo at IBM in 1967 for the IBM System/360 Model 91, is the foundational technique for out-of-order execution in modern superscalar processors. It enables instructions to execute as soon as their operands become available, rather than waiting for prior instructions to complete.

### Why Tomasulo? / 为什么要学托马苏洛？

| Problem | Solution |
|---------|----------|
| RAW hazards stall pipeline | Register renaming eliminates false dependencies |
| Structural hazards limit throughput | Reservation stations buffer pending operations |
| In-order completion wastes cycles | Out-of-order completion with in-order commit |
| WAR/WAW hazards from register reuse | Renaming via ROB tags avoids conflicts |

---

## Architecture / 架构

### System Diagram

```
            +-----------------------+
            |    Instruction Queue   |
            +-----------+-----------+
                        |
                        v
            +-----------+-----------+
            |       ISSUE            |  (dispatch to free RS, allocate ROB)
            +-----------+-----------+
                        |
        +---------------+---------------+
        |               |               |
        v               v               v
 +-----------+   +-----------+   +-----------+
 |  RS ALU0  |   |  RS ALU1  |   |  RS LSU   |
 +-----+-----+   +-----+-----+   +-----+-----+
       |               |               |
       +-------+-------+-------+
               |               |
               v               v
        +-----------+   +-----------+
        |  ALU FUs  |   |  LOAD/ST  |   (functional units)
        +-----+-----+   +-----+-----+
              |               |
              +-------+-------+
                      |
                      v
            +---------+---------+
            |  Common Data Bus   |   (broadcast results + tag)
            +---------+---------+
                      |
                      v
            +---------+---------+
            |  Reorder Buffer    |   (in-order commit)
            +---------+---------+
                      |
                      v
            +---------+---------+
            |  Register File     |   (architectural state)
            +-------------------+
```

### Reservation Station Fields

| Field | Width | Description |
|-------|-------|-------------|
| Busy | 1 bit | Station is occupied |
| Op | 6 bits | Operation to perform |
| Vj | 32 bits | Value of source operand 1 |
| Vk | 32 bits | Value of source operand 2 |
| Qj | log2(ROB) | Tag for pending source 1 |
| Qk | log2(ROB) | Tag for pending source 2 |
| A | 32 bits | Effective address (for loads/stores) |
| Dest | log2(ROB) | ROB entry for this result |

### Reorder Buffer Fields

| Field | Width | Description |
|-------|-------|-------------|
| Busy | 1 bit | Entry is in use |
| Ready | 1 bit | Result has been computed |
| Committed | 1 bit | Result written to architectural state |
| Instruction | 32 bits | The original instruction |
| Value | 32 bits | The result value |
| PC | 32 bits | Program counter of instruction |
| Rd | 5 bits | Destination register |

### Register Status Table

```
Before renaming:                       After renaming:
  x1 = 5                                 x1 -> ROB[0] (value=5, ready=true)
  x2 -> ROB[1] (pending)                 x2 -> ROB[1] (value=?, ready=false)
  x3 -> ROB[2] (pending)                 x3 -> ROB[2] (value=?, ready=false)
```

---

## Implementation Steps / 实现步骤

### Step 1: Define Data Structures

```c
#define MAX_RS    16
#define MAX_ROB   32

typedef struct {
    bool     busy;
    Opcode   op;
    uint32_t vj, vk;         // operand values
    uint32_t qj, qk;         // tags for pending operands
    bool     qj_valid, qk_valid;
    uint32_t dest;           // ROB tag for result
    bool     executing;
    uint32_t cycles_left;
} ReservationStation;

typedef struct {
    uint32_t rob_num;        // which ROB entry produces this value
    bool     ready;           // is value available?
} RegisterStatus;

typedef struct {
    uint32_t value;
    uint32_t tag;
    bool     valid;
} CDBEntry;

typedef struct {
    bool     busy;
    bool     ready;           // result computed
    bool     committed;       // result written to regfile
    Instruction inst;
    uint32_t value;
    uint32_t pc;
    uint32_t rob_index;
    uint8_t  rd;
} ROBEntry;
```

### Step 2: Issue / 发射

```c
void ooo_issue(OOOProcessor* p, const Instruction* inst) {
    int rs_idx = find_free_rs(p);
    int rob_idx = find_free_rob(p);
    if (rs_idx < 0 || rob_idx < 0) return;  // stall: no free RS/ROB

    ReservationStation* rs = &p->rs[rs_idx];
    rs->busy = true;
    rs->op = inst->opcode;
    rs->dest = rob_idx;  // ROB entry index = tag

    // Source operand 1: check register status
    if (inst->rs1 != 0 && !p->reg_status[inst->rs1].ready) {
        rs->qj = p->reg_status[inst->rs1].rob_num;  // wait for tag
        rs->qj_valid = true;
    } else {
        rs->vj = p->registers[inst->rs1];
        rs->qj_valid = false;
    }

    // Source operand 2: same logic
    if (inst->rs2 != 0 && !p->reg_status[inst->rs2].ready) {
        rs->qk = p->reg_status[inst->rs2].rob_num;
        rs->qk_valid = true;
    } else {
        rs->vk = p->registers[inst->rs2];
        rs->qk_valid = false;
    }

    // Allocate ROB entry
    ROBEntry* rob = &p->rob[rob_idx];
    rob->busy = true;
    rob->ready = false;
    rob->inst = *inst;
    rob->rd = inst->rd;

    // Update register status (register renaming!)
    if (inst->rd != 0) {
        p->reg_status[inst->rd].rob_num = rob_idx;
        p->reg_status[inst->rd].ready = false;
    }

    p->rob_tail = (p->rob_tail + 1) % MAX_ROB;
    p->rob_count++;
}
```

### Step 3: Execute / 执行

```c
void ooo_execute(OOOProcessor* p) {
    for (int i = 0; i < MAX_RS; i++) {
        ReservationStation* rs = &p->rs[i];
        if (!rs->busy || rs->qj_valid || rs->qk_valid) continue;
        // Both operands ready -> dispatch to functional unit

        uint32_t a = rs->vj;
        uint32_t b = rs->vk;
        uint32_t result = 0;

        switch (rs->op) {
            case OP_ADD:  result = a + b; break;
            case OP_SUB:  result = a - b; break;
            case OP_AND:  result = a & b; break;
            // ... more operations
        }

        p->cdb.value = result;
        p->cdb.tag = rs->dest;
        p->cdb.valid = true;
        rs->busy = false;
    }
}
```

### Step 4: Write Result / 结果回写 (CDB Broadcast)

```c
void ooo_write_result(OOOProcessor* p) {
    if (!p->cdb.valid) return;

    // Update ROB entry
    p->rob[p->cdb.tag].value = p->cdb.value;
    p->rob[p->cdb.tag].ready = true;

    // Update register status
    for (int i = 0; i < MAX_REGISTERS; i++) {
        if (p->reg_status[i].rob_num == p->cdb.tag) {
            p->reg_status[i].ready = true;
            p->registers[i] = p->cdb.value;
        }
    }

    // Forward to waiting reservation stations
    for (int i = 0; i < MAX_RS; i++) {
        ReservationStation* rs = &p->rs[i];
        if (rs->busy && rs->qj_valid && rs->qj == p->cdb.tag) {
            rs->vj = p->cdb.value;
            rs->qj_valid = false;  // operand captured!
        }
        if (rs->busy && rs->qk_valid && rs->qk == p->cdb.tag) {
            rs->vk = p->cdb.value;
            rs->qk_valid = false;
        }
    }
    p->cdb.valid = false;
}
```

### Step 5: Commit / 提交 (In-Order)

```c
void ooo_commit(OOOProcessor* p) {
    // Commit only from ROB head, in program order
    while (p->rob_count > 0) {
        ROBEntry* rob = &p->rob[p->rob_head];
        if (!rob->ready) break;  // youngest unready instruction blocks

        // Write architectural state
        if (rob->rd != 0) {
            p->registers[rob->rd] = rob->value;
        }
        rob->busy = false;
        rob->committed = true;
        p->rob_head = (p->rob_head + 1) % MAX_ROB;
        p->rob_count--;
        p->inst_committed++;
    }
}
```

---

## Example Trace / 执行示例

Consider the following instruction sequence:

```
I1: ADDI x1, x0, 5     I2: ADDI x2, x0, 3
I3: ADD  x3, x1, x2    I4: SUB  x4, x3, x1
```

### Cycle-by-Cycle Trace

```
Cycle 1: Issue I1, I2
  RS[0] <- I1 (op=ADDI, vj=0, vk=5, qj=N, qk=N, dest=ROB0)
  RS[1] <- I2 (op=ADDI, vj=0, vk=3, qj=N, qk=N, dest=ROB1)
  RAT: x1->ROB0  x2->ROB1

Cycle 2: Execute I1, I2; Issue I3
  I1 completes, CDB broadcasts {tag=ROB0, value=5}
  I2 completes, CDB broadcasts {tag=ROB1, value=3}
  RS[2] <- I3 (op=ADD, vj=5, vk=3, qj=N, qk=N, dest=ROB2)
  RAT: x1=ready(5) x2=ready(3) x3->ROB2

Cycle 3: Execute I3; Issue I4
  I3 completes, CDB broadcasts {tag=ROB2, value=8}
  RS[3] <- I4 (op=SUB, vj=8, vk=5, qj=N, qk=N, dest=ROB3)
  RAT: x3=ready(8) x4->ROB3

Cycle 4: Execute I4
  I4 completes, CDB broadcasts {tag=ROB3, value=3}
  RAT: x4=ready(3)

Cycle 5: Commit
  ROB head = ROB0 (ready) -> commit x1=5
  ROB head = ROB1 (ready) -> commit x2=3
  ROB head = ROB2 (ready) -> commit x3=8
  ROB head = ROB3 (ready) -> commit x4=3
  All committed!
```

---

## Key Insights / 关键要点

1. **Register renaming** is the core idea: each write creates a new "physical" name (ROB index)
2. **Reservation stations** decouple issue from execution
3. **CDB** is a shared bus that broadcasts results to all waiting stations
4. **In-order commit** ensures precise exceptions: if instruction N faults, all younger instructions are squashed
5. **ROB** doubles as the physical register file in this simplified implementation

## References / 参考

- Tomasulo, R.M. (1967). "An Efficient Algorithm for Exploiting Multiple Arithmetic Units." IBM Journal.
- Hennessy & Patterson, "Computer Architecture: A Quantitative Approach", Appendix C
- MIT 6.175 RISC-V OOO Processor Lab
- Stanford EE282: Superscalar and Out-of-Order Execution

---

## Build & Run / 构建与运行

```bash
make
./bin/ooo_demo      # Basic OOO demo
./bin/tomasulo_demo # Detailed Tomasulo step-by-step
```

Expected output: step-by-step state dumps showing RS allocation, CDB broadcasts, and ROB commit order.
