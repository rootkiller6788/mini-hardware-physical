# Out-of-Order Execution — 乱序执行

> In-depth coverage of out-of-order execution: register renaming, reservation stations, reorder buffer, common data bus, precise exceptions, Tomasulo's algorithm vs scoreboarding.

---

## Introduction / 引言

Out-of-order (OOO) execution allows a processor to execute instructions as soon as their operands are available, rather than strictly in program order. This is the key technique that enables modern high-performance processors to achieve instruction-level parallelism (ILP) beyond the limits of in-order pipelines.

### Why OOO?

| In-Order Limitation | OOO Solution |
|---------------------|--------------|
| One stalled instruction blocks all later ones | Independent instructions bypass the stall |
| RAW hazards force pipeline bubbles | Forwarding + renaming eliminates false deps |
| Functional unit underutilization | Issue queue dispatches to any free FU |
| Cache miss stalls propagate | Non-blocking caches allow other work |

---

## Core Concepts / 核心概念

### 1. Instruction Window

The "window" of instructions that the processor considers for execution:

```
Program Order:  I1  I2  I3  I4  I5  I6  I7  I8
                     ^                   ^
                     |----- WINDOW ------|
                        (being tracked)
Issued:   I1 (in execute)
Pending:  I2 I3 (waiting for operands)
Free:     I4 I5 I6 (will be issued soon)
Resolved: (committed already)
```

### 2. Register Renaming

Every logical register write gets a unique physical register:

```
Original:          After Renaming:
ADD x1, x2, x3     ADD p8, x2, x3     (x1 -> p8)
SUB x1, x4, x5     SUB p9, x4, x5     (x1 -> p9, new mapping!)
ADD x6, x1, x7     ADD p10, p9, x7    (reads latest mapping: p9)
```

This eliminates WAR and WAW hazards — each write creates a new temporary name.

In our implementation, the ROB index serves as the physical register name:

```c
// When issing instruction with destination rd:
p->rob[rob_idx].rd = inst->rd;
p->reg_status[inst->rd].rob_num = rob_idx;   // new mapping
p->reg_status[inst->rd].ready = false;        // value not yet known
```

### 3. Reservation Stations (RS)

Reservation stations buffer instructions waiting for operands.

```
                +---------------------------------+
                |       Reservation Station        |
                +---------------------------------+
                |  Busy: 1 bit                     |
                |  Op: 6 bits (ADD, SUB, etc.)     |
                |  Vj: 32 bits (value of src1)     |
                |  Vk: 32 bits (value of src2)     |
                |  Qj: 5 bits (ROB tag for src1)   |
                |  Qk: 5 bits (ROB tag for src2)   |
                |  Dest: 5 bits (ROB tag of result)|
                +---------------------------------+
```

Rules:
- A RS is "ready" when Qj and Qk are both invalid (operands captured)
- When ready, it can dispatch to any available functional unit
- After execution, the CDB broadcasts the result with its Dest tag

### 4. Common Data Bus (CDB)

The CDB is the broadcast medium. Any result produced by a functional unit is broadcast to ALL reservation stations and the register status table simultaneously.

```
CDB Entry:
  +-------+--------+
  | Value | Tag    |
  | 32b   | log(ROB)|
  +-------+--------+
```

All RS entries compare their Qj/Qk against the CDB tag. On match, they capture the value and clear the tag:

```c
for (int i = 0; i < MAX_RS; i++) {
    if (rs[i].busy && rs[i].qj_valid && rs[i].qj == cdb.tag) {
        rs[i].vj = cdb.value;
        rs[i].qj_valid = false;
    }
    // ... same for qk
}
```

### 5. Reorder Buffer (ROB)

The ROB preserves program order for:
1. **In-order commit**: Results are written to register file in program order
2. **Precise exceptions**: If instruction N faults, N+1, N+2... are squashed
3. **Recovery**: On misprediction, flush all younger ROB entries

Structure:
```
ROB (circular buffer):
  +--------+ +--------+ +--------+          +--------+
  | Entry 0| | Entry 1| | Entry 2|  ...    | Entry31|
  | Busy   | | Busy   | | Busy   |          | Busy   |
  | Ready  | | Ready  | | Ready  |          | Ready  |
  | Value  | | Value  | | Value  |          | Value  |
  | PC     | | PC     | | PC     |          | PC     |
  | Inst   | | Inst   | | Inst   |          | Inst   |
  +--------+ +--------+ +--------+          +--------+
     ^                                         ^
     |--- HEAD ---|         |--- TAIL ---------|
```

---

## Tomasulo vs Scoreboarding

| Feature | Scoreboarding | Tomasulo |
|---------|--------------|----------|
| Origin | CDC 6600 (1964) | IBM 360/91 (1967) |
| Register renaming | No | Yes (via RS + ROB) |
| WAR/WAW handling | Stall | Eliminated by renaming |
| Result forwarding | Through register file | Via CDB broadcast |
| Complexity | Lower | Higher |
| FU utilization | Good | Better |
| Precise exceptions | Difficult | Via ROB in-order commit |

### Scoreboarding Flow

```
1. ISSUE:      Check structural hazards (no free FU for this op?)
2. READ OPERANDS: Wait for operands, check WAR hazards
3. EXECUTION:  Functional unit executes
4. WRITE RESULT: Check WAR hazards with pending readers, write RF
```

### Tomasulo Flow

```
1. ISSUE:      Allocate RS + ROB entry, rename registers
2. DISPATCH:   When operands ready (Qj=Qk=0), send to FU
3. EXECUTION:  FU completes, puts result on CDB
4. WRITE RESULT: CDB broadcasts, RS capture values, ROB updated
5. COMMIT:     In-order from ROB head, write architectural RF
```

---

## Tomasulo Algorithm Step-by-Step

### Data Structures

```c
typedef struct {
    ReservationStation rs[16];   // MAX_RS = 16
    RegisterStatus reg_status[16];
    ROBEntry rob[32];            // MAX_ROB = 32
    CDBEntry cdb;
    uint32_t registers[16];
    uint8_t  memory[4096];
} OOOProcessor;
```

### Step 1: Issue

For each new instruction (up to issue width):

1. Find a free reservation station (`find_free_rs()`)
2. Find a free ROB entry (`find_free_rob()`)
3. Read `reg_status[rs1]` and `reg_status[rs2]`:
   - If `ready`: copy value into RS.Vj/Vk
   - If not ready: copy ROB tag into RS.Qj/Qk
4. Allocate ROB entry with instruction metadata
5. Update `reg_status[rd]` to point to this ROB entry (rename!)
6. Advance `rob_tail`

### Step 2: Execute (Dispatch)

For each RS entry where both operands are ready (Qj and Qk both invalid):

1. Send to functional unit
2. Functional unit performs operation (may take multiple cycles)
3. Result placed on CDB

In our simplified model, execution takes 1 cycle:

```c
for (int i = 0; i < MAX_RS; i++) {
    if (!rs[i].busy || rs[i].qj_valid || rs[i].qk_valid) continue;
    // Ready to execute
    uint32_t result = execute_operation(rs[i].op, rs[i].vj, rs[i].vk);
    cdb.value = result;
    cdb.tag = rs[i].dest;
    cdb.valid = true;
    rs[i].busy = false;
}
```

### Step 3: Write Result (CDB Broadcast)

On CDB valid:

1. Write value to `rob[cdb.tag].value`, set `rob[cdb.tag].ready = true`
2. For each register i: if `reg_status[i].rob_num == cdb.tag`, set `ready = true`
3. For each RS j:
   - If `rs[j].qj_valid && rs[j].qj == cdb.tag`: capture value, clear Qj
   - If `rs[j].qk_valid && rs[j].qk == cdb.tag`: capture value, clear Qk
4. Clear CDB

### Step 4: Commit

Starting from ROB head, while `rob[head].ready`:

1. Write `rob[head].value` to `registers[rob[head].rd]` (if rd != 0)
2. Mark ROB entry as committed / free
3. Advance `rob_head`, decrement `rob_count`

---

## Example Trace / 执行轨迹

Program:
```
I1: ADDI x1, x0, 5
I2: ADDI x2, x0, 3
I3: ADD  x3, x1, x2
I4: SUB  x4, x3, x1
```

### After Issue Phase

```
RS[0]: (I1) op=ADDI vj=0  vk=5  qj=N qk=N dest=ROB0
RS[1]: (I2) op=ADDI vj=0  vk=3  qj=N qk=N dest=ROB1
RS[2]: (I3) op=ADD  vj=5  vk=3  qj=N qk=N dest=ROB2
RS[3]: (I4) op=SUB  vj=?  vk=5  qj=ROB2 qk=N dest=ROB3

ROB[0]: (I1) rd=1 ready=0
ROB[1]: (I2) rd=2 ready=0
ROB[2]: (I3) rd=3 ready=0
ROB[3]: (I4) rd=4 ready=0

Register Status:
  x1 -> ROB0 (!ready)   x2 -> ROB1 (!ready)
  x3 -> ROB2 (!ready)   x4 -> ROB3 (!ready)
```

### After Execution Phase

```
CDB broadcast {tag=ROB0, val=5}
  -> ROB[0].ready=1
  -> RS[2].vj=5 (Qj clear) [no effect: already had value]
  -> RS[3].vj=5 if Qj matched

CDB broadcast {tag=ROB1, val=3}
  -> ROB[1].ready=1

RS[2] now: op=ADD vj=5 vk=3 qj=N qk=N -> ready to execute!
CDB broadcast {tag=ROB2, val=8}
  -> ROB[2].ready=1
  -> RS[3].vj=8 (Qj captured!)
  -> reg_status[x3].ready=1

RS[3] now: op=SUB vj=8 vk=5 qj=N qk=N -> ready to execute!
CDB broadcast {tag=ROB3, val=3}
  -> ROB[3].ready=1
```

### Commit Phase

```
ROB head=0: ready -> commit x1=5
ROB head=1: ready -> commit x2=3
ROB head=2: ready -> commit x3=8
ROB head=3: ready -> commit x4=3
All committed!
```

---

## Precise Exceptions / 精确异常

For precise exceptions, we must ensure that when instruction N faults:
1. All instructions BEFORE N have committed
2. NO instruction AFTER N has committed

Solution: Exception flag in ROB entry. During commit, if ROB entry has exception:
1. Stop commit
2. Flush all younger ROB entries (free RS, clear register mappings)
3. Set PC to exception handler

---

## Performance Modeling / 性能模型

### IPC Limits

```
Peak IPC = issue_width × FU_count
Actual IPC = min(issue_width, ILP, FU_availability)
```

For our 2-wide, 2-ALU, 1-LSU design:
- Peak IPC: 2 (CPI = 0.5)
- Typical IPC: 1.2-1.8 (limited by dependencies, cache misses)

### Resources

| Resource | Size | Scaling |
|----------|------|---------|
| RS | 16 | Larger = more ILP, more power |
| ROB | 32 | Larger = more ILP, longer exception recovery |
| FU | 3 (2 ALU + 1 LSU) | More = higher peak IPC |
| Register File | 16 logical | With rename: 32+ physical needed |

---

## References / 参考

1. Tomasulo, R.M. (1967). "An Efficient Algorithm for Exploiting Multiple Arithmetic Units." IBM Journal of Research and Development, 11(1), 25-33.
2. Thornton, J.E. (1964). "Parallel Operation in the Control Data 6600." AFIPS.
3. Smith, J.E. & Pleszkun, A.R. (1988). "Implementing Precise Interrupts in Pipelined Processors." IEEE TC.
4. Hennessy & Patterson, "Computer Architecture: A Quantitative Approach", Chapter 3.
5. Shen & Lipasti, "Modern Processor Design: Fundamentals of Superscalar Processors."
6. MIT 6.175 RISC-V OOO Processor Lab Manual.
7. Stanford EE282: Out-of-Order Processors lecture notes.
