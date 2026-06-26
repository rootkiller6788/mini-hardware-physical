#ifndef RTL_BASIC_H
#define RTL_BASIC_H

#include <stdbool.h>
#include <stdint.h>

#define RTL_MAX_PORTS     32
#define RTL_MAX_SIGNALS   64
#define RTL_MAX_NAME      64
#define RTL_MAX_STAGES    16
#define PIPELINE_MAX_REGS 32

typedef enum {
    RTL_PORT_IN,
    RTL_PORT_OUT,
    RTL_PORT_INOUT
} RTLPortDir;

typedef struct {
    char        name[RTL_MAX_NAME];
    RTLPortDir  direction;
    int         width;
} RTLPort;

typedef struct {
    char        name[RTL_MAX_NAME];
    int         width;
    uint64_t    value;
    bool        valid;
} RTLSignal;

typedef struct {
    char        name[RTL_MAX_NAME];
    RTLPort     ports[RTL_MAX_PORTS];
    int         port_count;
    RTLSignal   signals[RTL_MAX_SIGNALS];
    int         signal_count;
} RTLModule;

typedef enum {
    STAGE_IF,
    STAGE_ID,
    STAGE_EX,
    STAGE_MEM,
    STAGE_WB
} PipelineStageName;

typedef struct {
    uint32_t    pc;
    uint32_t    instruction;
    uint32_t    rs1_data;
    uint32_t    rs2_data;
    uint32_t    rd_addr;
    uint32_t    alu_result;
    uint32_t    mem_data;
    bool        reg_write;
    bool        mem_read;
    bool        mem_write;
    bool        branch_taken;
    uint32_t    branch_target;
    bool        valid;
    bool        stalled;
} PipelineRegister;

typedef struct {
    PipelineRegister if_id;
    PipelineRegister id_ex;
    PipelineRegister ex_mem;
    PipelineRegister mem_wb;
    uint32_t    reg_file[PIPELINE_MAX_REGS];
    uint32_t    pc;
    int         cycles;
    int         instructions;
    int         stalls;
    int         bubbles;
    bool        data_hazard;
    bool        control_hazard;
    int         forwarded_from;
    uint32_t    forwarded_value;
} FiveStagePipeline;

RTLModule rtl_mod_create(const char* name);
void      rtl_add_port(RTLModule* m, const char* name, RTLPortDir dir, int width);
void      rtl_set_signal(RTLModule* m, const char* name, uint64_t value);
uint64_t  rtl_get_signal(const RTLModule* m, const char* name);
void      rtl_evaluate(RTLModule* m);
void      rtl_print(const RTLModule* m);
RTLModule rtl_mux_create(const char* name, int n_inputs, int sel_width);
RTLModule rtl_decoder_create(const char* name, int n_inputs);
RTLModule rtl_encoder_create(const char* name, int n_inputs);
RTLModule rtl_dff_create(const char* name);
RTLModule rtl_reg_create(const char* name, int width);
FiveStagePipeline pipeline_create(void);
bool    pipeline_fetch(FiveStagePipeline* p, uint32_t instruction);
int     pipeline_cycle(FiveStagePipeline* p);
void    pipeline_write_reg(FiveStagePipeline* p, int reg, uint32_t value);
uint32_t pipeline_read_reg(const FiveStagePipeline* p, int reg);
void    pipeline_stats(const FiveStagePipeline* p, int* cycles, int* instrs, int* stalls, int* bubbles, double* cpi);
void    pipeline_print(const FiveStagePipeline* p);
void    pipeline_reset(FiveStagePipeline* p);
bool    pipeline_detect_data_hazard(const PipelineRegister* prev, const PipelineRegister* curr);
uint32_t pipeline_forward(const FiveStagePipeline* p, int rs_addr);
void    pipeline_flush(FiveStagePipeline* p);

#endif
