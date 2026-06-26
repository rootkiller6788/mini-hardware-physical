# mini-riscv-interpreter — 微型 RISC-V 解释器

> Building a tiny RISC-V RV32I interpreter in C: instruction encoding, fetch-decode-execute loop, register file, and memory model.

---

## Overview / 概览

This demo walks through building a minimal RISC-V interpreter capable of executing a subset of the RV32I base integer instruction set. The interpreter implements the classic fetch-decode-execute cycle using software emulation of hardware structures.

### 核心概念

| Concept | Description |
|---------|-------------|
| RV32I | RISC-V 32-bit base integer ISA: 40 instructions |
| Instruction Encoding | R/I/S/B/U/J six formats, fixed 32-bit |
| Register File | 32 general-purpose registers (x0=zero, x1-x31) |
| PC | Program counter, incremented by 4 (or branch target) |
| Memory | Byte-addressable, little-endian, 4KB by default |

---

## Architecture / 架构

### Instruction Formats

```
R-type: | funct7(7) | rs2(5) | rs1(5) | funct3(3) | rd(5) | opcode(7) |
I-type: |    imm[11:0](12)    | rs1(5) | funct3(3) | rd(5) | opcode(7) |
S-type: | imm[11:5](7) | rs2(5) | rs1(5) | funct3(3) | imm[4:0](5) | opcode(7) |
B-type: | imm[12|10:5](7) | rs2(5) | rs1(5) | funct3(3) | imm[4:1|11](5) | opcode(7) |
U-type: |       imm[31:12](20)          | rd(5) | opcode(7) |
J-type: | imm[20|10:1|11|19:12](20) | rd(5) | opcode(7) |
```

### Subset Implemented

| Category | Instructions |
|----------|-------------|
| Arithmetic | ADD, SUB, ADDI |
| Logical | AND, OR, XOR, ANDI, ORI, XORI |
| Shift | SLL, SRL, SRA, SLLI, SRLI, SRAI |
| Comparison | SLT, SLTU, SLTI, SLTIU |
| Branch | BEQ, BNE, BLT, BGE, BLTU, BGEU |
| Jump | JAL, JALR |
| Load/Store | LW, SW |
| Upper Imm | LUI, AUIPC |

### Data Flow / 数据流

```
    +-------------+    +-------------+    +-------------+
    |   FETCH     | -> |   DECODE    | -> |   EXECUTE   |
    | memory[PC]  |    | parse fields|    | ALU + mem   |
    +-------------+    +-------------+    +-------------+
          ^                                      |
          |              +-------------+         |
          +------------- |  WRITEBACK  | <-------+
                         +-------------+
```

---

## Implementation Steps / 实现步骤

### Step 1: Define the ISA Structures

```c
// include/isa.h
#define MAX_REGISTERS   16
#define MEMORY_SIZE     4096

typedef enum {
    OP_ADD, OP_SUB, OP_ADDI, OP_LW, OP_SW,
    OP_BEQ, OP_BNE, OP_JAL, /* ... */
    OP_COUNT
} Opcode;

typedef struct {
    Opcode   opcode;
    uint8_t  rd, rs1, rs2;
    uint8_t  funct3, funct7;
    int32_t  immediate;
    uint32_t raw;
} Instruction;

typedef struct {
    uint32_t registers[MAX_REGISTERS];
    uint32_t pc;
    uint8_t  memory[MEMORY_SIZE];
    bool     halted;
    uint64_t cycles;
} ISAContext;
```

### Step 2: Instruction Decoding

The decoder reads a raw 32-bit word and extracts fields by bit masking:

```c
Instruction isa_decode(uint32_t raw) {
    Instruction inst = {0};
    inst.raw = raw;
    uint8_t opcode = raw & 0x7F;
    inst.rd  = (raw >> 7)  & 0x1F;
    inst.funct3 = (raw >> 12) & 0x7;
    inst.rs1 = (raw >> 15) & 0x1F;
    inst.rs2 = (raw >> 20) & 0x1F;
    inst.funct7 = (raw >> 25) & 0x7F;

    // Sign-extend immediate per format
    int32_t imm_i = sign_extend((raw >> 20) & 0xFFF, 12);
    // ... assign to inst.immediate based on opcode
    return inst;
}
```

### Step 3: Execution Logic

```c
void isa_execute(ISAContext* ctx, const Instruction* inst) {
    uint32_t* regs = ctx->registers;
    uint32_t a = regs[inst->rs1];
    uint32_t b = regs[inst->rs2];
    uint32_t next_pc = ctx->pc + 4;
    uint32_t result = 0;

    switch (inst->opcode) {
        case OP_ADD:
            result = a + b;
            break;
        case OP_ADDI:
            result = a + (uint32_t)inst->immediate;
            break;
        case OP_LW: {
            uint32_t addr = a + inst->immediate;
            result = *(uint32_t*)&ctx->memory[addr];  // little-endian
            break;
        }
        case OP_SW: {
            uint32_t addr = a + inst->immediate;
            ctx->memory[addr + 0] = b & 0xFF;
            ctx->memory[addr + 1] = (b >> 8) & 0xFF;
            ctx->memory[addr + 2] = (b >> 16) & 0xFF;
            ctx->memory[addr + 3] = (b >> 24) & 0xFF;
            break;
        }
        case OP_BEQ:
            if (a == b) next_pc = ctx->pc + inst->immediate;
            break;
        case OP_JAL:
            result = ctx->pc + 4;
            next_pc = ctx->pc + inst->immediate;
            break;
        // ... all other opcodes
    }

    if (inst->rd > 0 && inst->opcode != OP_SW) {
        regs[inst->rd] = result;
    }
    ctx->pc = next_pc;
}
```

### Step 4: Fetch-Decode-Execute Loop

```c
void isa_step(ISAContext* ctx) {
    Instruction inst = isa_fetch(ctx);   // read memory[pc]
    inst = isa_decode(inst.raw);          // parse fields
    isa_execute(ctx, &inst);              // run
}

void isa_fetch(const ISAContext* ctx) {
    // Read 4 bytes from ctx->memory[ctx->pc]
    uint32_t raw = ctx->memory[pc] |
                   (ctx->memory[pc+1] << 8) |
                   (ctx->memory[pc+2] << 16) |
                   (ctx->memory[pc+3] << 24);
    return isa_decode(raw);
}
```

### Step 5: Running a Program

```c
int main(void) {
    ISAContext ctx;
    isa_init(&ctx);

    // Manual encoding: ADDI x1, x0, 10  = I-type
    uint32_t prog[] = {
        0x00A00093,  // ADDI x1, x0, 10
        0x01400113,  // ADDI x2, x0, 20
        0x002081B3,  // ADD  x3, x1, x2
    };
    isa_load_program(&ctx, prog, 3);

    for (int i = 0; i < 3; i++) {
        isa_step(&ctx);
    }
    isa_dump_registers(&ctx);
    // Expected: x1=10, x2=20, x3=30
}
```

---

## Expected Output / 预期输出

```
=== mini-riscv-interpreter Demo ===

Loading 3 instructions into memory.

--- Initial Register State ---
  x0 = 0x00000000  x1 = 0x00000000  x2 = 0x00000000  x3 = 0x00000000

[Step 1] PC=0x0000
  Fetch: raw=0x00A00093
  Decode: op=ADDI  rd=x1  rs1=x0  imm=10
  Execute: x1 = 0 + 10 = 10

[Step 2] PC=0x0004
  Fetch: raw=0x01400113
  Decode: op=ADDI  rd=x2  rs1=x0  imm=20
  Execute: x2 = 0 + 20 = 20

[Step 3] PC=0x0008
  Fetch: raw=0x002081B3
  Decode: op=ADD   rd=x3  rs1=x1  rs2=x2
  Execute: x3 = 10 + 20 = 30

--- Final Register State ---
  x0 = 0x00000000  x1 = 0x0000000A  x2 = 0x00000014  x3 = 0x0000001E
```

---

## Key Design Decisions

1. **x0 hardwired to zero**: Writes to x0 are silently ignored, reads always return 0
2. **16 registers (not 32)**: Simplified for teaching; real RV32I has 32 registers (x0-x31)
3. **4096-byte memory**: Sufficient for small programs; real systems have GB+ addressable
4. **Big-endian decoding**: Actual RISC-V is little-endian, but our trace format is big-endian for clarity
5. **No privilege levels**: Flat memory, no MMU, no exception handling (ecall/mret not supported)
6. **Single-cycle execution model**: Each instruction completes in one isa_step() call (no pipeline modeling here)

## Extensions / 扩展

- Add CSR (Control and Status Register) support for cycle counting
- Implement the M-extension (multiply/divide)
- Add ELF loader to read compiled RISC-V binaries
- Support system calls (ecall) for I/O

## References / 参考

- RISC-V Specification v2.2 (RV32I Base Integer Instruction Set)
- MIT 6.004 Computation Structures: Instruction Set Architectures
- MIT 6.175 RISC-V Processor Design Lab
- "Computer Organization and Design RISC-V Edition" by Patterson & Hennessy

---

## Build & Run / 构建与运行

```bash
# From mini-cpu-arch directory
make

# Run the ISA demo
./bin/isa_demo

# Or compile manually
gcc -Wall -Wextra -O2 -I include -o bin/riscv_interpreter \
    src/isa.c examples/isa_demo.c
./bin/riscv_interpreter
```

Expected runtime: < 1 second. Output shows step-by-step execution of a 3-instruction program.
