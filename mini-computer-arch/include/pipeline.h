#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* === L1: Core Definitions — RISC-V-like 5-stage pipeline ISA === */

#define PIPELINE_MAX_REGS      32
#define PIPELINE_IMEM_SIZE    256
#define PIPELINE_DMEM_SIZE   1024
#define PIPELINE_STAGES        5

#define PIPELINE_FW_DEPTH       3
#define PIPELINE_ROB_SIZE      16
#define PIPELINE_IQ_SIZE        8

typedef enum {
    OP_ADD,  OP_SUB,  OP_AND,  OP_OR,   OP_XOR,
    OP_SLL,  OP_SRL,  OP_SRA,  OP_SLT,  OP_SLTU,
    OP_ADDI, OP_ANDI, OP_ORI,  OP_XORI,
    OP_LW,   OP_SW,   OP_BEQ,  OP_BNE,  OP_BLT,
    OP_JAL,  OP_JALR, OP_LUI,  OP_AUIPC,
    OP_MUL,  OP_DIV,  OP_NOP,  OP_HALT
} Opcode;

typedef enum {
    REG_ZERO = 0,  REG_RA = 1,  REG_SP = 2,  REG_GP = 3, REG_TP = 4,
    REG_T0 = 5, REG_T1 = 6, REG_T2 = 7,
    REG_S0 = 8, REG_S1 = 9,
    REG_A0 = 10, REG_A1 = 11
} RegisterAlias;

typedef struct {
    Opcode   opcode;
    uint8_t  rd;
    uint8_t  rs1;
    uint8_t  rs2;
    int32_t  imm;
    uint32_t raw;
    uint32_t pc;
} DecodedInstruction;

/* L2: Pipeline inter-stage registers */
typedef struct {
    bool          valid;
    DecodedInstruction inst;
    uint32_t      pc_plus4;
    int32_t       rs1_val;
    int32_t       rs2_val;
    bool          rs1_ready;
    bool          rs2_ready;
    uint8_t       rs1_tag;
    uint8_t       rs2_tag;
} IFID_Register;

typedef struct {
    bool          valid;
    DecodedInstruction inst;
    int32_t       rs1_val;
    int32_t       rs2_val;
    int32_t       alu_result;
    bool          branch_taken;
    uint32_t      branch_target;
} IDEX_Register;

typedef struct {
    bool          valid;
    DecodedInstruction inst;
    int32_t       rs1_val;
    int32_t       rs2_val;
    int32_t       alu_result;
    int32_t       store_data;
    bool          branch_taken;
    uint32_t      branch_target;
    bool          mem_read;
    bool          mem_write;
} EXMEM_Register;

typedef struct {
    bool          valid;
    DecodedInstruction inst;
    int32_t       mem_data;
    int32_t       alu_result;
    bool          reg_write;
} MEMWB_Register;

/* L3: Hazard types */
typedef enum {
    HAZARD_NONE, HAZARD_RAW, HAZARD_WAW, HAZARD_WAR,
    HAZARD_STRUCTURAL, HAZARD_CONTROL
} HazardType;

typedef struct {
    HazardType type;
    uint8_t    stall_cycles;
    uint32_t   affected_pc;
    uint8_t    src_reg;
    uint8_t    dst_reg;
} HazardInfo;

/* L3: Forwarding paths */
typedef enum {
    FW_NONE, FW_FROM_EX, FW_FROM_MEM, FW_FROM_WB
} ForwardSource;

typedef struct {
    ForwardSource src1_fw;
    ForwardSource src2_fw;
    int32_t       fw_val1;
    int32_t       fw_val2;
} ForwardingInfo;

/* L3: Pipeline processor state */
typedef struct {
    int32_t   regfile[PIPELINE_MAX_REGS];
    uint32_t  pc;
    uint32_t  imem[PIPELINE_IMEM_SIZE];
    int32_t   dmem[PIPELINE_DMEM_SIZE];

    IFID_Register   if_id;
    IDEX_Register   id_ex;
    EXMEM_Register  ex_mem;
    MEMWB_Register  mem_wb;

    bool      stall;
    bool      flush;
    bool      halted;

    uint64_t  cycles;
    uint64_t  instructions_committed;
    uint64_t  bubbles;
    uint64_t  branch_count;
    uint64_t  branch_mispredictions;
    uint64_t  loads;
    uint64_t  stores;
    uint64_t  raw_stalls;
    uint64_t  forwarding_hits;
    double    ipc;

    int32_t   fw_ex_result;
    uint8_t   fw_ex_rd;
    int32_t   fw_mem_result;
    uint8_t   fw_mem_rd;
    int32_t   fw_wb_result;
    uint8_t   fw_wb_rd;
} PipelineProcessor;

/* API */
void   pipeline_init(PipelineProcessor *p);
void   pipeline_load_program(PipelineProcessor *p, const uint32_t *instrs, size_t count);
void   pipeline_reset(PipelineProcessor *p);
bool   pipeline_cycle(PipelineProcessor *p);
void   pipeline_run(PipelineProcessor *p, uint64_t max_cycles);

DecodedInstruction pipeline_decode(uint32_t raw, uint32_t pc);
uint32_t pipeline_encode(Opcode op, uint8_t rd, uint8_t rs1, uint8_t rs2, int32_t imm);
const char *pipeline_opcode_name(Opcode op);

HazardInfo     pipeline_detect_hazards(const PipelineProcessor *p);
ForwardingInfo pipeline_detect_forwarding(const PipelineProcessor *p);
bool           pipeline_needs_stall(const PipelineProcessor *p);

int32_t pipeline_alu_compute(Opcode op, int32_t a, int32_t b);

void   pipeline_compute_ipc(PipelineProcessor *p);
double pipeline_efficiency(const PipelineProcessor *p);
double pipeline_amdal_speedup(double fraction_parallel, int num_cores);

void   pipeline_print_state(const PipelineProcessor *p);
void   pipeline_print_stats(const PipelineProcessor *p);
void   pipeline_print_hazards(const PipelineProcessor *p);

void   pipeline_optimize_schedule(PipelineProcessor *p);
double pipeline_cpi_stack(const PipelineProcessor *p);
void   pipeline_analyze_bottleneck(const PipelineProcessor *p);

void   pipeline_ooo_reserve_station(PipelineProcessor *p);
void   pipeline_tomasulo_step(PipelineProcessor *p);
bool   pipeline_register_renaming(PipelineProcessor *p, uint8_t arch_reg, uint8_t *phys_reg);

#endif
