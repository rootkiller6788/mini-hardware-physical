#include "pipeline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t sign_extend(uint32_t val, uint32_t bits)
{
    uint32_t mask = 1u << (bits - 1);
    if (val & mask) val |= ((~0u) << bits);
    return (int32_t)val;
}

const char *pipeline_opcode_name(Opcode op)
{
    switch (op) {
    case OP_ADD:  return "ADD";     case OP_SUB:  return "SUB";
    case OP_AND:  return "AND";     case OP_OR:   return "OR";
    case OP_XOR:  return "XOR";     case OP_SLL:  return "SLL";
    case OP_SRL:  return "SRL";     case OP_SRA:  return "SRA";
    case OP_SLT:  return "SLT";     case OP_SLTU: return "SLTU";
    case OP_ADDI: return "ADDI";    case OP_ANDI: return "ANDI";
    case OP_ORI:  return "ORI";     case OP_XORI: return "XORI";
    case OP_LW:   return "LW";      case OP_SW:   return "SW";
    case OP_BEQ:  return "BEQ";     case OP_BNE:  return "BNE";
    case OP_BLT:  return "BLT";     case OP_JAL:  return "JAL";
    case OP_JALR: return "JALR";    case OP_LUI:  return "LUI";
    case OP_AUIPC:return "AUIPC";   case OP_MUL:  return "MUL";
    case OP_DIV:  return "DIV";     case OP_NOP:  return "NOP";
    case OP_HALT: return "HALT";
    default:      return "???";
    }
}

DecodedInstruction pipeline_decode(uint32_t raw, uint32_t pc)
{
    DecodedInstruction inst;
    memset(&inst, 0, sizeof(inst));
    inst.raw = raw;
    inst.pc  = pc;

    uint32_t opcode = raw & 0x7Fu;
    uint32_t funct3 = (raw >> 12) & 0x7u;
    uint32_t funct7 = (raw >> 25) & 0x7Fu;

    inst.rd  = (raw >> 7)  & 0x1Fu;
    inst.rs1 = (raw >> 15) & 0x1Fu;
    inst.rs2 = (raw >> 20) & 0x1Fu;

    switch (opcode) {
    case 0x33:
        switch (funct3) {
        case 0x0: inst.opcode = (funct7 == 0x20) ? OP_SUB : OP_ADD; break;
        case 0x1: inst.opcode = OP_SLL;  break;
        case 0x2: inst.opcode = OP_SLT;  break;
        case 0x3: inst.opcode = OP_SLTU; break;
        case 0x4: inst.opcode = OP_XOR;  break;
        case 0x5: inst.opcode = (funct7 == 0x20) ? OP_SRA : OP_SRL; break;
        case 0x6: inst.opcode = OP_OR;   break;
        case 0x7: inst.opcode = OP_AND;  break;
        default:  inst.opcode = OP_NOP;  break;
        }
        break;
    case 0x13:
        inst.imm = sign_extend((raw >> 20) & 0xFFFu, 12);
        switch (funct3) {
        case 0x0: inst.opcode = OP_ADDI; break;
        case 0x2: inst.opcode = OP_SLT;  break;
        case 0x3: inst.opcode = OP_SLTU; break;
        case 0x4: inst.opcode = OP_XORI; break;
        case 0x6: inst.opcode = OP_ORI;  break;
        case 0x7: inst.opcode = OP_ANDI; break;
        default:  inst.opcode = OP_ADDI; break;
        }
        break;
    case 0x03:
        inst.imm = sign_extend((raw >> 20) & 0xFFFu, 12);
        inst.opcode = OP_LW;
        break;
    case 0x23:
        inst.imm = sign_extend(((raw >> 25) << 5) | ((raw >> 7) & 0x1Fu), 12);
        inst.opcode = OP_SW;
        break;
    case 0x63:
        inst.imm = sign_extend(
            ((raw >> 31) << 12) | (((raw >> 7) & 1) << 11) |
            (((raw >> 25) & 0x3F) << 5) | (((raw >> 8) & 0xF) << 1), 13);
        switch (funct3) {
        case 0x0: inst.opcode = OP_BEQ; break;
        case 0x1: inst.opcode = OP_BNE; break;
        case 0x4: inst.opcode = OP_BLT; break;
        default:  inst.opcode = OP_NOP; break;
        }
        break;
    case 0x37:
        inst.imm = (int32_t)(raw & 0xFFFFF000u);
        inst.opcode = OP_LUI;
        break;
    case 0x17:
        inst.imm = (int32_t)(raw & 0xFFFFF000u);
        inst.opcode = OP_AUIPC;
        break;
    case 0x6F:
        inst.imm = sign_extend(
            ((raw >> 31) << 20) | (((raw >> 12) & 0xFF) << 12) |
            (((raw >> 20) & 1) << 11) | (((raw >> 21) & 0x3FF) << 1), 21);
        inst.opcode = OP_JAL;
        break;
    case 0x67:
        inst.imm = sign_extend((raw >> 20) & 0xFFFu, 12);
        inst.opcode = OP_JALR;
        break;
    default:
        inst.opcode = OP_NOP;
        break;
    }
    return inst;
}

uint32_t pipeline_encode(Opcode op, uint8_t rd, uint8_t rs1,
                          uint8_t rs2, int32_t imm)
{
    uint32_t raw = 0;
    switch (op) {
    case OP_ADD:  raw = 0x33u | (rd << 7) | (0x0 << 12) | (rs1 << 15) | (rs2 << 20) | (0x00 << 25); break;
    case OP_SUB:  raw = 0x33u | (rd << 7) | (0x0 << 12) | (rs1 << 15) | (rs2 << 20) | (0x20 << 25); break;
    case OP_ADDI: raw = 0x13u | (rd << 7) | (0x0 << 12) | (rs1 << 15) | ((imm & 0xFFF) << 20); break;
    case OP_LW:   raw = 0x03u | (rd << 7) | (0x2 << 12) | (rs1 << 15) | ((imm & 0xFFF) << 20); break;
    case OP_SW:   raw = 0x23u | ((imm & 0x1F) << 7) | (0x2 << 12) | (rs1 << 15) | (rs2 << 20) | (((imm >> 5) & 0x7F) << 25); break;
    case OP_BEQ: {
        uint32_t bimm = (uint32_t)imm;
        raw = 0x63u | (((bimm >> 1) & 0xF) << 8) | (0x0 << 12) | (rs1 << 15) | (rs2 << 20) | (((bimm >> 5) & 0x3F) << 25) | (((bimm >> 12) & 1) << 31);
    } break;
    case OP_JAL: {
        uint32_t ji = (uint32_t)(imm & 0x1FFFFF);
        raw = 0x6Fu | (rd << 7) | (((ji >> 1) & 0x3FF) << 21) | (((ji >> 11) & 1) << 20) | (((ji >> 12) & 0xFF) << 12) | (((ji >> 20) & 1) << 31);
    } break;
    case OP_LUI:  raw = 0x37u | (rd << 7) | (imm & 0xFFFFF000u); break;
    case OP_AUIPC: raw = 0x17u | (rd << 7) | (imm & 0xFFFFF000u); break;
    case OP_JALR: raw = 0x67u | (rd << 7) | (0x0 << 12) | (rs1 << 15) | ((imm & 0xFFF) << 20); break;
    case OP_NOP:  raw = 0x13u; break;
    case OP_HALT: raw = 0x0000006Fu; break;
    default:      raw = 0x13u; break;
    }
    return raw;
}

int32_t pipeline_alu_compute(Opcode op, int32_t a, int32_t b)
{
    switch (op) {
    case OP_ADD:  case OP_ADDI: return a + b;
    case OP_SUB:  return a - b;
    case OP_AND:  case OP_ANDI: return a & b;
    case OP_OR:   case OP_ORI:  return a | b;
    case OP_XOR:  case OP_XORI: return a ^ b;
    case OP_SLL:  return a << (b & 0x1F);
    case OP_SRL:  return (int32_t)((uint32_t)a >> (b & 0x1F));
    case OP_SRA:  return a >> (b & 0x1F);
    case OP_SLT:  case OP_SLTU: return (a < b) ? 1 : 0;
    case OP_MUL:  return a * b;
    case OP_DIV:  return (b != 0) ? (a / b) : 0;
    case OP_BEQ:  return (a == b) ? 1 : 0;
    case OP_BNE:  return (a != b) ? 1 : 0;
    case OP_BLT:  return (a < b)  ? 1 : 0;
    case OP_LUI:  return b;
    case OP_AUIPC: return a + b;
    case OP_JAL:  case OP_JALR: return a + 4;
    default:      return 0;
    }
}

static bool is_load_op(Opcode op)  { return op == OP_LW; }
static bool is_store_op(Opcode op) { return op == OP_SW; }

HazardInfo pipeline_detect_hazards(const PipelineProcessor *p)
{
    HazardInfo info;
    memset(&info, 0, sizeof(info));
    info.type = HAZARD_NONE;

    if (!p->id_ex.valid) return info;

    uint8_t id_rs1 = p->id_ex.inst.rs1;
    uint8_t id_rs2 = p->id_ex.inst.rs2;
    uint8_t ex_rd  = p->ex_mem.inst.rd;
    bool    ex_rw  = (p->ex_mem.inst.opcode != OP_SW) &&
                     (p->ex_mem.inst.opcode != OP_BEQ) &&
                     (p->ex_mem.inst.opcode != OP_BNE) &&
                     (p->ex_mem.inst.opcode != OP_BLT) &&
                     (p->ex_mem.inst.opcode != OP_NOP) &&
                     (p->ex_mem.inst.opcode != OP_HALT);

    if (p->ex_mem.valid && ex_rd != REG_ZERO && ex_rw) {
        if (ex_rd == id_rs1 || ex_rd == id_rs2) {
            if (p->ex_mem.inst.opcode == OP_LW) {
                info.type = HAZARD_RAW;
                info.stall_cycles = 1;
                info.src_reg = (ex_rd == id_rs1) ? id_rs1 : id_rs2;
                info.dst_reg = ex_rd;
                info.affected_pc = p->id_ex.inst.pc;
                return info;
            }
        }
    }

    if (p->ex_mem.valid && ex_rd != REG_ZERO && ex_rw &&
        (ex_rd == id_rs1 || ex_rd == id_rs2)) {
        info.type = HAZARD_RAW;
        info.stall_cycles = 0;
        info.src_reg = (ex_rd == id_rs1) ? id_rs1 : id_rs2;
        info.dst_reg = ex_rd;
    }

    return info;
}

ForwardingInfo pipeline_detect_forwarding(const PipelineProcessor *p)
{
    ForwardingInfo fw;
    memset(&fw, 0, sizeof(fw));
    fw.src1_fw = FW_NONE;
    fw.src2_fw = FW_NONE;

    uint8_t ex_rd  = p->fw_ex_rd;
    uint8_t mem_rd = p->fw_mem_rd;
    uint8_t rs1 = p->id_ex.inst.rs1;
    uint8_t rs2 = p->id_ex.inst.rs2;

    if (ex_rd != REG_ZERO && ex_rd == rs1) {
        fw.src1_fw = FW_FROM_EX;
        fw.fw_val1 = p->fw_ex_result;
    }
    if (ex_rd != REG_ZERO && ex_rd == rs2) {
        fw.src2_fw = FW_FROM_EX;
        fw.fw_val2 = p->fw_ex_result;
    }

    if (mem_rd != REG_ZERO && mem_rd == rs1 && fw.src1_fw == FW_NONE) {
        fw.src1_fw = FW_FROM_MEM;
        fw.fw_val1 = p->fw_mem_result;
    }
    if (mem_rd != REG_ZERO && mem_rd == rs2 && fw.src2_fw == FW_NONE) {
        fw.src2_fw = FW_FROM_MEM;
        fw.fw_val2 = p->fw_mem_result;
    }

    return fw;
}

bool pipeline_needs_stall(const PipelineProcessor *p)
{
    HazardInfo h = pipeline_detect_hazards(p);
    return (h.type == HAZARD_RAW && h.stall_cycles > 0);
}

void pipeline_init(PipelineProcessor *p)
{
    memset(p, 0, sizeof(*p));
    p->regfile[REG_ZERO] = 0;
    p->regfile[REG_SP]   = PIPELINE_DMEM_SIZE - 16;
    p->pc = 0;
}

void pipeline_load_program(PipelineProcessor *p,
                            const uint32_t *instrs, size_t count)
{
    if (count > PIPELINE_IMEM_SIZE) count = PIPELINE_IMEM_SIZE;
    memcpy(p->imem, instrs, count * sizeof(uint32_t));
    p->halted = false;
    p->pc = 0;
}

void pipeline_reset(PipelineProcessor *p)
{
    memset(&p->if_id, 0, sizeof(p->if_id));
    memset(&p->id_ex, 0, sizeof(p->id_ex));
    memset(&p->ex_mem, 0, sizeof(p->ex_mem));
    memset(&p->mem_wb, 0, sizeof(p->mem_wb));
    p->pc = 0;
    p->stall = false;
    p->flush = false;
    p->halted = false;
    p->cycles = 0;
    p->instructions_committed = 0;
    p->bubbles = 0;
    p->branch_count = 0;
    p->branch_mispredictions = 0;
    p->loads = 0;
    p->stores = 0;
    p->raw_stalls = 0;
    p->forwarding_hits = 0;
    p->ipc = 0.0;
}

bool pipeline_cycle(PipelineProcessor *p)
{
    if (p->halted) return false;
    p->cycles++;

    /* === Writeback Stage (WB) === */
    if (p->mem_wb.valid) {
        DecodedInstruction *inst = &p->mem_wb.inst;
        int32_t wb_val = p->mem_wb.alu_result;
        if (inst->opcode == OP_LW) wb_val = p->mem_wb.mem_data;
        if (inst->rd != REG_ZERO && p->mem_wb.reg_write)
            p->regfile[inst->rd] = wb_val;
        p->fw_wb_result = wb_val;
        p->fw_wb_rd     = (uint8_t)inst->rd;
        p->instructions_committed++;
    } else {
        p->fw_wb_rd = REG_ZERO;
    }

    /* === Memory Stage (MEM) === */
    /* MEM->WB transfer: copy common fields from EX/MEM latch */
    p->mem_wb.valid      = p->ex_mem.valid;
    p->mem_wb.inst       = p->ex_mem.inst;
    p->mem_wb.alu_result = p->ex_mem.alu_result;
    p->mem_wb.reg_write  = true;  /* most ops write back */

    if (p->mem_wb.valid) {
        p->fw_mem_result = p->mem_wb.alu_result;
        p->fw_mem_rd     = (uint8_t)p->mem_wb.inst.rd;

        if (is_load_op(p->mem_wb.inst.opcode)) {
            uint32_t addr = (uint32_t)(p->mem_wb.alu_result);
            if (addr / 4 < PIPELINE_DMEM_SIZE) {
                p->mem_wb.mem_data = p->dmem[addr / 4];
                p->loads++;
            }
        }
        if (is_store_op(p->mem_wb.inst.opcode)) {
            uint32_t addr = (uint32_t)(p->mem_wb.alu_result);
            if (addr / 4 < PIPELINE_DMEM_SIZE) {
                p->dmem[addr / 4] = p->ex_mem.store_data;
                p->stores++;
            }
            p->mem_wb.reg_write = false;
        }
    }

    /* === Execute Stage (EX) === */
    if (p->flush) {
        memset(&p->ex_mem, 0, sizeof(p->ex_mem));
        p->bubbles++;
        p->flush = false;
    } else if (!p->stall) {
        /* ID->EX transfer: copy common fields */
        p->ex_mem.valid         = p->id_ex.valid;
        p->ex_mem.inst          = p->id_ex.inst;
        p->ex_mem.rs1_val       = p->id_ex.rs1_val;
        p->ex_mem.rs2_val       = p->id_ex.rs2_val;
        p->ex_mem.branch_taken  = p->id_ex.branch_taken;
        p->ex_mem.branch_target = p->id_ex.branch_target;
    }

    if (p->ex_mem.valid) {
        p->fw_ex_result = p->ex_mem.alu_result;
        p->fw_ex_rd     = (uint8_t)p->ex_mem.inst.rd;

        DecodedInstruction *inst = &p->ex_mem.inst;
        int32_t op1 = p->ex_mem.rs1_val;
        int32_t op2 = p->ex_mem.rs2_val;

        switch (inst->opcode) {
        case OP_LUI:
            p->ex_mem.alu_result = inst->imm;
            break;
        case OP_AUIPC:
            p->ex_mem.alu_result = (int32_t)(inst->pc + (uint32_t)inst->imm);
            break;
        case OP_SW:
            p->ex_mem.alu_result = op1 + inst->imm;
            p->ex_mem.store_data = op2;
            break;
        case OP_LW:
            p->ex_mem.alu_result = op1 + inst->imm;
            break;
        case OP_BEQ: case OP_BNE: case OP_BLT: {
            int32_t cmp = pipeline_alu_compute(inst->opcode, op1, op2);
            p->branch_count++;
            if (cmp) {
                p->ex_mem.branch_taken = true;
                p->ex_mem.branch_target = (uint32_t)((int32_t)inst->pc + inst->imm);
            } else {
                p->ex_mem.branch_taken = false;
            }
            p->ex_mem.alu_result = (int32_t)(inst->pc + 4);
            break;
        }
        case OP_JAL:
            p->branch_count++;
            p->ex_mem.branch_taken = true;
            p->ex_mem.branch_target = (uint32_t)((int32_t)inst->pc + inst->imm);
            p->ex_mem.alu_result = (int32_t)(inst->pc + 4);
            break;
        case OP_JALR:
            p->ex_mem.branch_taken = true;
            p->ex_mem.branch_target = (uint32_t)((op1 + inst->imm) & ~1);
            p->ex_mem.alu_result = (int32_t)(inst->pc + 4);
            break;
        case OP_HALT:
            p->halted = true;
            break;
        default:
            p->ex_mem.alu_result = pipeline_alu_compute(inst->opcode, op1, op2);
            break;
        }
    }

    /* === Decode Stage (ID) === */
    if (p->stall) {
        memset(&p->id_ex, 0, sizeof(p->id_ex));
        p->bubbles++;
        p->raw_stalls++;
        p->stall = false;
    } else if (p->flush) {
        memset(&p->id_ex, 0, sizeof(p->id_ex));
        p->bubbles++;
    } else {
        /* IF->ID transfer: copy common fields */
        p->id_ex.valid         = p->if_id.valid;
        p->id_ex.inst          = p->if_id.inst;
        p->id_ex.rs1_val       = p->if_id.rs1_val;
        p->id_ex.rs2_val       = p->if_id.rs2_val;
        p->id_ex.branch_taken  = false;
        p->id_ex.branch_target = 0;

        if (p->id_ex.valid) {
            DecodedInstruction *inst = &p->id_ex.inst;
            ForwardingInfo fw = pipeline_detect_forwarding(p);

            if (fw.src1_fw != FW_NONE) {
                p->id_ex.rs1_val = fw.fw_val1;
                p->forwarding_hits++;
            } else {
                p->id_ex.rs1_val = p->regfile[inst->rs1];
            }

            if (fw.src2_fw != FW_NONE) {
                p->id_ex.rs2_val = fw.fw_val2;
                p->forwarding_hits++;
            } else {
                p->id_ex.rs2_val = p->regfile[inst->rs2];
            }

            if (inst->opcode == OP_ADDI || inst->opcode == OP_ANDI ||
                inst->opcode == OP_ORI  || inst->opcode == OP_XORI ||
                inst->opcode == OP_LW   || inst->opcode == OP_SW)
                p->id_ex.rs2_val = inst->imm;

            if (pipeline_needs_stall(p))
                p->stall = true;
        }
    }

    /* === Fetch Stage (IF) === */
    if (!p->stall) {
        if (p->pc / 4 < PIPELINE_IMEM_SIZE) {
            uint32_t raw_inst = p->imem[p->pc / 4];
            p->if_id.inst = pipeline_decode(raw_inst, p->pc);
            p->if_id.pc_plus4 = p->pc + 4;
            p->if_id.valid = true;
        } else {
            p->if_id.valid = false;
        }
    }

    /* PC Update */
    if (p->ex_mem.valid && p->ex_mem.branch_taken) {
        p->pc = p->ex_mem.branch_target;
        p->flush = true;
    } else if (!p->stall) {
        p->pc += 4;
    }

    if (p->ex_mem.valid && p->ex_mem.branch_taken) {
        if (p->ex_mem.branch_target != p->ex_mem.inst.pc + 4)
            p->branch_mispredictions++;
    }

    if (p->pc >= PIPELINE_IMEM_SIZE * 4 && !p->if_id.valid)
        p->halted = true;

    return !p->halted;
}

void pipeline_run(PipelineProcessor *p, uint64_t max_cycles)
{
    for (uint64_t i = 0; i < max_cycles; i++) {
        if (!pipeline_cycle(p)) break;
    }
}

double pipeline_amdal_speedup(double fraction_parallel, int num_cores)
{
    if (num_cores <= 0) return 1.0;
    if (fraction_parallel < 0.0) fraction_parallel = 0.0;
    if (fraction_parallel > 1.0) fraction_parallel = 1.0;
    double serial_fraction = 1.0 - fraction_parallel;
    double denominator = serial_fraction + fraction_parallel / (double)num_cores;
    if (denominator < 1e-10) return (double)num_cores;
    return 1.0 / denominator;
}

void pipeline_compute_ipc(PipelineProcessor *p)
{
    if (p->cycles > 0)
        p->ipc = (double)p->instructions_committed / (double)p->cycles;
}

double pipeline_efficiency(const PipelineProcessor *p)
{
    uint64_t useful_cycles = (p->cycles > p->bubbles) ? (p->cycles - p->bubbles) : 0;
    if (p->cycles == 0) return 0.0;
    return (double)useful_cycles / (double)p->cycles;
}

double pipeline_cpi_stack(const PipelineProcessor *p)
{
    if (p->instructions_committed == 0) return 0.0;
    return (double)p->cycles / (double)p->instructions_committed;
}

void pipeline_analyze_bottleneck(const PipelineProcessor *p)
{
    printf("=== Pipeline Bottleneck Analysis ===\n");
    printf("  Total CPI:  %.3f\n", pipeline_cpi_stack(p));
    printf("  IPC:        %.3f\n", p->ipc);
    printf("  Efficiency: %.1f%%\n", pipeline_efficiency(p) * 100.0);
    double bubble_frac = (p->cycles > 0) ?
        (double)p->bubbles / (double)p->cycles : 0.0;
    printf("  Bubble rate: %.1f%%\n", bubble_frac * 100.0);
    if (p->branch_count > 0) {
        double mispred_rate =
            (double)p->branch_mispredictions / (double)p->branch_count * 100.0;
        printf("  Branch misprediction: %.1f%% (%llu/%llu)\n",
               mispred_rate,
               (unsigned long long)p->branch_mispredictions,
               (unsigned long long)p->branch_count);
        if (mispred_rate > 5.0)
            printf("  => BOTTLENECK: High branch misprediction rate\n");
    }
    if (p->raw_stalls > p->instructions_committed / 20)
        printf("  => BOTTLENECK: Frequent RAW stalls\n");
    if (p->forwarding_hits > 0) {
        double fw_ratio = (p->instructions_committed > 0) ?
            (double)p->forwarding_hits / (double)p->instructions_committed : 0.0;
        printf("  Forwarding hits: %llu (%.2f per instr)\n",
               (unsigned long long)p->forwarding_hits, fw_ratio);
    }
    printf("========================================\n");
}

void pipeline_optimize_schedule(PipelineProcessor *p)
{
    printf("=== Pipeline Schedule Optimization ===\n");
    printf("  RAW stalls:         %llu\n", (unsigned long long)p->raw_stalls);
    printf("  Forwarding hits:    %llu\n", (unsigned long long)p->forwarding_hits);
    printf("  Branch mispredicts: %llu\n", (unsigned long long)p->branch_mispredictions);
    printf("  Bubbles:            %llu\n", (unsigned long long)p->bubbles);
    printf("  Optimizations suggested:\n");
    if (p->raw_stalls > 0)
        printf("  - Schedule independent instr after loads\n");
    if (p->branch_mispredictions > p->branch_count / 10)
        printf("  - Consider loop unrolling\n");
    if (p->bubbles > p->cycles / 5)
        printf("  - Reorder instructions to fill delay slots\n");
    printf("========================================\n");
}

void pipeline_ooo_reserve_station(PipelineProcessor *p)
{
    printf("=== OoO Reserve Station Model ===\n");
    printf("  IQ size:  %d entries\n", PIPELINE_IQ_SIZE);
    printf("  ROB size: %d entries\n", PIPELINE_ROB_SIZE);
    printf("  FW depth: %d\n", PIPELINE_FW_DEPTH);
    printf("  Current IPC: %.3f\n", p->ipc);
    printf("  Tomasulo (1967): Reservation stations enable\n");
    printf("  out-of-order execution by tracking operand\n");
    printf("  availability via tag broadcasting on CDB.\n");
    printf("========================================\n");
}

void pipeline_tomasulo_step(PipelineProcessor *p)
{
    printf("=== Tomasulo Algorithm Step ===\n");
    printf("  IBM 360/91 (Tomasulo 1967, IBM JRD 11:25-33)\n");
    printf("  Three stages per cycle:\n");
    printf("  1. Issue:  dispatch to reservation station\n");
    printf("  2. Execute: when operands ready (out-of-order)\n");
    printf("  3. Write:   broadcast result on Common Data Bus\n");
    printf("  Current IPC: %.3f\n", p->ipc);
    printf("========================================\n");
}

bool pipeline_register_renaming(PipelineProcessor *p, uint8_t arch_reg,
                                 uint8_t *phys_reg)
{
    static uint8_t rat[PIPELINE_MAX_REGS];
    static bool    rat_init = false;
    if (!rat_init) {
        for (int i = 0; i < PIPELINE_MAX_REGS; i++) rat[i] = (uint8_t)i;
        rat_init = true;
    }
    if (arch_reg >= PIPELINE_MAX_REGS) return false;
    *phys_reg = rat[arch_reg];
    (void)p;
    return true;
}

void pipeline_print_state(const PipelineProcessor *p)
{
    printf("=== Pipeline State (Cycle %llu) ===\n", (unsigned long long)p->cycles);
    printf("  PC: 0x%08X  Halted:%s  Stall:%s  Flush:%s\n",
           p->pc, p->halted ? "Y" : "N",
           p->stall ? "Y" : "N", p->flush ? "Y" : "N");
    if (p->if_id.valid)
        printf("  IF/ID:  %-6s r%02d,r%02d,r%02d (PC=0x%08X)\n",
               pipeline_opcode_name(p->if_id.inst.opcode),
               p->if_id.inst.rd, p->if_id.inst.rs1, p->if_id.inst.rs2,
               p->if_id.inst.pc);
    else printf("  IF/ID:  (bubble)\n");
    if (p->id_ex.valid)
        printf("  ID/EX:  %-6s r%02d=%d, r%02d=%d\n",
               pipeline_opcode_name(p->id_ex.inst.opcode),
               p->id_ex.inst.rs1, p->id_ex.rs1_val,
               p->id_ex.inst.rs2, p->id_ex.rs2_val);
    else printf("  ID/EX:  (bubble)\n");
    if (p->ex_mem.valid) {
        printf("  EX/MEM: %-6s r%02d=%d",
               pipeline_opcode_name(p->ex_mem.inst.opcode),
               p->ex_mem.inst.rd, p->ex_mem.alu_result);
        if (p->ex_mem.branch_taken)
            printf(" [BR->0x%08X]", p->ex_mem.branch_target);
        printf("\n");
    } else printf("  EX/MEM: (bubble)\n");
    if (p->mem_wb.valid)
        printf("  MEM/WB: %-6s r%02d=%d\n",
               pipeline_opcode_name(p->mem_wb.inst.opcode),
               p->mem_wb.inst.rd, p->mem_wb.alu_result);
    else printf("  MEM/WB: (bubble)\n");
    printf("========================================\n");
}

void pipeline_print_stats(const PipelineProcessor *p)
{
    printf("=== Pipeline Performance Statistics ===\n");
    printf("  Cycles:              %llu\n", (unsigned long long)p->cycles);
    printf("  Instrs Committed:    %llu\n", (unsigned long long)p->instructions_committed);
    printf("  IPC:                 %.3f\n", p->ipc);
    printf("  Efficiency:          %.1f%%\n", pipeline_efficiency(p) * 100.0);
    printf("  Bubbles:             %llu\n", (unsigned long long)p->bubbles);
    printf("  Branches:            %llu\n", (unsigned long long)p->branch_count);
    printf("  Mispredictions:      %llu\n", (unsigned long long)p->branch_mispredictions);
    if (p->branch_count > 0)
        printf("  Mispred Rate:        %.1f%%\n",
               (double)p->branch_mispredictions / (double)p->branch_count * 100.0);
    printf("  RAW Stalls:          %llu\n", (unsigned long long)p->raw_stalls);
    printf("  Forwarding Hits:     %llu\n", (unsigned long long)p->forwarding_hits);
    printf("  Loads:               %llu\n", (unsigned long long)p->loads);
    printf("  Stores:              %llu\n", (unsigned long long)p->stores);
    double speedup_4 = pipeline_amdal_speedup(0.90, 4);
    double speedup_8 = pipeline_amdal_speedup(0.90, 8);
    printf("  Amdahl(P=0.90,N=4):  %.2fx\n", speedup_4);
    printf("  Amdahl(P=0.90,N=8):  %.2fx\n", speedup_8);
    printf("========================================\n");
}

void pipeline_print_hazards(const PipelineProcessor *p)
{
    printf("=== Pipeline Hazard Analysis ===\n");
    HazardInfo h = pipeline_detect_hazards(p);
    printf("  Current hazard: ");
    switch (h.type) {
    case HAZARD_NONE:   printf("None\n"); break;
    case HAZARD_RAW:    printf("RAW (stall=%d)\n", h.stall_cycles); break;
    case HAZARD_WAW:    printf("WAW\n"); break;
    case HAZARD_WAR:    printf("WAR\n"); break;
    case HAZARD_STRUCTURAL: printf("Structural\n"); break;
    case HAZARD_CONTROL:printf("Control\n"); break;
    }
    printf("  Stall needed: %s\n", pipeline_needs_stall(p) ? "YES" : "NO");
    ForwardingInfo fw = pipeline_detect_forwarding(p);
    printf("  Forwarding src1: ");
    switch (fw.src1_fw) {
    case FW_NONE:    printf("None\n"); break;
    case FW_FROM_EX: printf("EX/MEM (val=%d)\n", fw.fw_val1); break;
    case FW_FROM_MEM:printf("MEM/WB (val=%d)\n", fw.fw_val1); break;
    case FW_FROM_WB: printf("WB (val=%d)\n", fw.fw_val1); break;
    }
    printf("  Forwarding src2: ");
    switch (fw.src2_fw) {
    case FW_NONE:    printf("None\n"); break;
    case FW_FROM_EX: printf("EX/MEM (val=%d)\n", fw.fw_val2); break;
    case FW_FROM_MEM:printf("MEM/WB (val=%d)\n", fw.fw_val2); break;
    case FW_FROM_WB: printf("WB (val=%d)\n", fw.fw_val2); break;
    }
    printf("========================================\n");
}
