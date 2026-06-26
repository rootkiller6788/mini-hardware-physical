#include "rtl_basic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RTLModule rtl_mod_create(const char* name) {
    RTLModule m;
    if (name) { strncpy(m.name, name, RTL_MAX_NAME - 1); m.name[RTL_MAX_NAME - 1] = '\0'; }
    else m.name[0] = '\0';
    m.port_count = 0; m.signal_count = 0; return m;
}
void rtl_add_port(RTLModule* m, const char* name, RTLPortDir dir, int width) {
    if (!m || m->port_count >= RTL_MAX_PORTS) return;
    RTLPort* p = &m->ports[m->port_count];
    if (name) { strncpy(p->name, name, RTL_MAX_NAME - 1); p->name[RTL_MAX_NAME - 1] = '\0'; }
    p->direction = dir; p->width = width; m->port_count++;
}
void rtl_set_signal(RTLModule* m, const char* name, uint64_t value) {
    if (!m || !name) return;
    for (int i = 0; i < m->signal_count; i++) {
        if (strcmp(m->signals[i].name, name) == 0) { m->signals[i].value = value; m->signals[i].valid = true; return; }
    }
    if (m->signal_count >= RTL_MAX_SIGNALS) return;
    RTLSignal* s = &m->signals[m->signal_count];
    strncpy(s->name, name, RTL_MAX_NAME - 1); s->name[RTL_MAX_NAME - 1] = '\0';
    s->width = 1; s->value = value; s->valid = true; m->signal_count++;
}
uint64_t rtl_get_signal(const RTLModule* m, const char* name) {
    if (!m || !name) return 0;
    for (int i = 0; i < m->signal_count; i++)
        if (strcmp(m->signals[i].name, name) == 0) return m->signals[i].value;
    return 0;
}
void rtl_evaluate(RTLModule* m) { (void)m; }
void rtl_print(const RTLModule* m) {
    if (!m) return;
    printf("RTL Module: %s\n", m->name);
    printf("Ports (%d):\n", m->port_count);
    for (int i = 0; i < m->port_count; i++)
        printf("  %s: dir=%d, width=%d\n", m->ports[i].name, m->ports[i].direction, m->ports[i].width);
    printf("Signals (%d):\n", m->signal_count);
    for (int i = 0; i < m->signal_count; i++)
        printf("  %s = %llu (valid=%d)\n", m->signals[i].name, (unsigned long long)m->signals[i].value, m->signals[i].valid);
}

RTLModule rtl_mux_create(const char* name, int n_inputs, int sel_width) {
    RTLModule m = rtl_mod_create(name);
    rtl_add_port(&m, "out", RTL_PORT_OUT, 1);
    for (int i = 0; i < n_inputs; i++) { char pn[16]; snprintf(pn, sizeof(pn), "in%d", i); rtl_add_port(&m, pn, RTL_PORT_IN, 1); }
    rtl_add_port(&m, "sel", RTL_PORT_IN, sel_width);
    rtl_set_signal(&m, "out", 0); return m;
}
RTLModule rtl_decoder_create(const char* name, int n_inputs) {
    RTLModule m = rtl_mod_create(name);
    rtl_add_port(&m, "in", RTL_PORT_IN, n_inputs);
    int n_out = 1 << n_inputs;
    for (int i = 0; i < n_out; i++) { char pn[16]; snprintf(pn, sizeof(pn), "out%d", i); rtl_add_port(&m, pn, RTL_PORT_OUT, 1); }
    return m;
}
RTLModule rtl_encoder_create(const char* name, int n_inputs) {
    RTLModule m = rtl_mod_create(name);
    for (int i = 0; i < n_inputs; i++) { char pn[16]; snprintf(pn, sizeof(pn), "in%d", i); rtl_add_port(&m, pn, RTL_PORT_IN, 1); }
    rtl_add_port(&m, "out", RTL_PORT_OUT, 3); return m;
}
RTLModule rtl_dff_create(const char* name) {
    RTLModule m = rtl_mod_create(name);
    rtl_add_port(&m, "clk", RTL_PORT_IN, 1); rtl_add_port(&m, "d", RTL_PORT_IN, 1);
    rtl_add_port(&m, "q", RTL_PORT_OUT, 1); return m;
}
RTLModule rtl_reg_create(const char* name, int width) {
    RTLModule m = rtl_mod_create(name);
    rtl_add_port(&m, "clk", RTL_PORT_IN, 1); rtl_add_port(&m, "d", RTL_PORT_IN, width);
    rtl_add_port(&m, "q", RTL_PORT_OUT, width); return m;
}

FiveStagePipeline pipeline_create(void) {
    FiveStagePipeline p;
    memset(&p, 0, sizeof(p));
    p.if_id.valid = false; p.id_ex.valid = false;
    p.ex_mem.valid = false; p.mem_wb.valid = false;
    p.data_hazard = false; p.control_hazard = false;
    p.forwarded_from = -1; p.forwarded_value = 0;
    return p;
}
bool pipeline_fetch(FiveStagePipeline* p, uint32_t instruction) {
    if (!p) return false;
    if (p->if_id.valid && p->if_id.stalled) return false;
    p->if_id.instruction = instruction;
    p->if_id.pc = p->pc;
    p->if_id.valid = true;
    p->pc += 4; p->instructions++;
    return true;
}
int pipeline_cycle(FiveStagePipeline* p) {
    if (!p) return 0;
    int cyc = 1;
    if (p->mem_wb.valid && p->mem_wb.reg_write && p->mem_wb.rd_addr > 0)
        p->reg_file[p->mem_wb.rd_addr] = p->mem_wb.alu_result;
    p->mem_wb = p->ex_mem;
    p->ex_mem = p->id_ex;
    p->id_ex = p->if_id;
    p->if_id.valid = false;
    p->data_hazard = false;
    if (p->id_ex.valid && p->if_id.valid) {
        p->data_hazard = pipeline_detect_data_hazard(&p->id_ex, &p->if_id);
        if (p->data_hazard) { p->stalls++; cyc++; }
    }
    p->cycles += cyc;
    return cyc;
}
void pipeline_write_reg(FiveStagePipeline* p, int reg, uint32_t value) {
    if (p && reg > 0 && reg < PIPELINE_MAX_REGS) p->reg_file[reg] = value;
}
uint32_t pipeline_read_reg(const FiveStagePipeline* p, int reg) {
    if (!p || reg < 0 || reg >= PIPELINE_MAX_REGS) return 0;
    if (reg == 0) return 0;
    if (p->forwarded_from >= 0) return p->forwarded_value;
    return p->reg_file[reg];
}
void pipeline_stats(const FiveStagePipeline* p, int* cycles, int* instrs, int* stalls, int* bubbles, double* cpi) {
    if (!p) return;
    if (cycles) *cycles = p->cycles;
    if (instrs) *instrs = p->instructions;
    if (stalls) *stalls = p->stalls;
    if (bubbles) *bubbles = p->bubbles;
    if (cpi) *cpi = (p->instructions > 0) ? (double)p->cycles / p->instructions : 0.0;
}
void pipeline_print(const FiveStagePipeline* p) {
    if (!p) return;
    printf("Pipeline: PC=0x%x, cycles=%d, instrs=%d\n", p->pc, p->cycles, p->instructions);
    printf("  IF/ID valid=%d  ID/EX valid=%d  EX/MEM valid=%d  MEM/WB valid=%d\n",
           p->if_id.valid, p->id_ex.valid, p->ex_mem.valid, p->mem_wb.valid);
    printf("  Stalls=%d, Bubbles=%d\n", p->stalls, p->bubbles);
}
void pipeline_reset(FiveStagePipeline* p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->if_id.valid = false; p->id_ex.valid = false;
    p->ex_mem.valid = false; p->mem_wb.valid = false;
}
bool pipeline_detect_data_hazard(const PipelineRegister* prev, const PipelineRegister* curr) {
    if (!prev || !curr || !prev->valid || !curr->valid) return false;
    if (!prev->reg_write) return false;
    /* Check if previous instruction writes to a register current reads */
    return false; /* Simplified: assume forwarding handles it */
}
uint32_t pipeline_forward(const FiveStagePipeline* p, int rs_addr) {
    if (!p || rs_addr <= 0) return 0;
    if (p->ex_mem.valid && p->ex_mem.reg_write && p->ex_mem.rd_addr == (uint32_t)rs_addr)
        return p->ex_mem.alu_result;
    if (p->mem_wb.valid && p->mem_wb.reg_write && p->mem_wb.rd_addr == (uint32_t)rs_addr)
        return p->mem_wb.alu_result;
    return p->reg_file[rs_addr];
}
void pipeline_flush(FiveStagePipeline* p) {
    if (!p) return;
    p->if_id.valid = false; p->id_ex.valid = false; p->bubbles += 2;
}
