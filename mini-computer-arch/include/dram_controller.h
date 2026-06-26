#ifndef DRAM_CONTROLLER_H
#define DRAM_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

/* L1: Core Definitions — DRAM Controller and Memory Scheduling */

#define DRAM_MAX_BANKS        8
#define DRAM_MAX_RANKS        2
#define DRAM_ROWS_PER_BANK 8192
#define DRAM_COLS_PER_ROW  1024
#define DRAM_BURST_LENGTH     8
#define DRAM_REQ_QUEUE_SIZE  64
#define DRAM_TRANS_QUEUE_SIZE  32

/* L1: Standard DRAM timing parameters (DDR4-3200 JEDEC JESD79-4B) */
typedef struct {
    uint32_t tCK;        /* Clock cycle time in ps (312.5ps for DDR4-3200) */
    uint32_t tRCD;       /* RAS to CAS delay in cycles (14)      */
    uint32_t tCL;        /* CAS latency in cycles (14)            */
    uint32_t tRAS;       /* Row active time in cycles (32)       */
    uint32_t tRP;        /* Row precharge time in cycles (14)    */
    uint32_t tRC;        /* Row cycle time = tRAS + tRP (46)     */
    uint32_t tRTP;       /* Read to precharge (6)                */
    uint32_t tWR;        /* Write recovery time (14)             */
    uint32_t tWTR;       /* Write to read delay (4)              */
    uint32_t tRRD;       /* Row activate to row activate (4)     */
    uint32_t tFAW;       /* Four activation window (20)          */
    uint32_t tRFC;       /* Refresh cycle time (350ns = 560)     */
    uint32_t tREFI;      /* Refresh interval (7.8us = 12480)     */
    uint32_t tCCD;       /* Column to column delay (4)           */
    uint32_t tBL;        /* Burst length (8 for BL8)             */
} DRAMTiming;

/* L1: DRAM command types */
typedef enum {
    CMD_ACTIVATE,
    CMD_READ,
    CMD_WRITE,
    CMD_PRECHARGE,
    CMD_REFRESH,
    CMD_NOP
} DRAMCommandType;

typedef struct {
    DRAMCommandType type;
    uint32_t   bank;
    uint32_t   row;
    uint32_t   col;
    uint32_t   request_id;
} DRAMCommand;

/* L2: Memory request with scheduling metadata */
typedef struct {
    uint64_t   address;
    uint32_t   bank;
    uint32_t   row;
    uint32_t   col;
    uint32_t   rank;
    bool       is_write;
    uint64_t   arrival_time;
    uint64_t   deadline;       /* QoS deadline */
    uint32_t   priority;       /* 0=low, 3=high */
    uint32_t   id;
    bool       completed;
} MemRequest;

/* L2: Bank state machine */
typedef enum {
    BANK_IDLE,
    BANK_ACTIVATING,
    BANK_ACTIVE,
    BANK_PRECHARGING,
    BANK_REFRESHING
} BankState;

typedef struct {
    BankState   state;
    uint32_t    open_row;
    bool        row_valid;
    uint64_t    ready_time;     /* When next command can be issued */
    uint64_t    active_count;
    uint64_t    access_count;
    uint64_t    row_miss_count;
    uint64_t    row_hit_count;
} DRAMBank;

/* L3: Memory scheduling policies */
typedef enum {
    SCHED_FCFS,          /* First-Come-First-Serve */
    SCHED_FR_FCFS,       /* FR-FCFS: Row-hit-first then FCFS (Rixner 2000) */
    SCHED_STFM,          /* Shortest-Time-First-Miss */
    SCHED_PARBS,         /* Parallelism-Aware Batch (Mutlu & Moscibroda 2008) */
    SCHED_ATLAS,         /* Adaptive per-Thread Least Attained Service (Kim 2010) */
    SCHED_TCM,           /* Thread Cluster Memory scheduling (Kim 2010) */
    SCHED_BLISS          /* Blacklisting Memory Scheduler (Subramanian 2013) */
} SchedulingPolicy;

/* L8: Memory interference analysis (Subramanian et al. 2013 — MISE model) */
typedef struct {
    double   slowdown;          /* Memory-induced slowdown */
    double   service_rate;
    double   queueing_delay;
    double   interference_ratio;
    uint64_t alone_cycles;
    uint64_t shared_cycles;
} ThreadInterference;

/* L3: DRAM Controller */
typedef struct {
    DRAMBank        banks[DRAM_MAX_BANKS * DRAM_MAX_RANKS];
    uint32_t        num_banks;
    uint32_t        num_ranks;
    DRAMTiming      timing;

    MemRequest      req_queue[DRAM_REQ_QUEUE_SIZE];
    uint32_t        req_head;
    uint32_t        req_tail;
    uint32_t        req_count;

    DRAMCommand     trans_queue[DRAM_TRANS_QUEUE_SIZE];
    uint32_t        trans_head;
    uint32_t        trans_tail;
    uint32_t        trans_count;

    SchedulingPolicy policy;
    uint64_t        cycles;
    uint64_t        total_reads;
    uint64_t        total_writes;
    uint64_t        row_hits;
    uint64_t        row_conflicts;
    uint64_t        row_misses;          /* Empty bank access */
    uint64_t        refresh_cycles;
    uint64_t        stall_cycles;
    double          bandwidth_utilization;

    /* Bank-level parallelism tracking */
    uint32_t        active_banks;
    uint64_t        bank_level_parallelism;
    uint32_t        blp_samples;

    /* L8: Thread interference tracking */
    ThreadInterference thread_interference[4];
    uint32_t        num_threads;
} DRAMController;

/* L1 API */
void dram_init(DRAMController *ctrl, uint32_t num_banks, uint32_t num_ranks,
               const DRAMTiming *timing, SchedulingPolicy policy);
bool dram_enqueue(DRAMController *ctrl, uint64_t addr, bool is_write, uint32_t id);
void dram_cycle(DRAMController *ctrl);  /* Advance 1 cycle */
void dram_run(DRAMController *ctrl, uint64_t cycles);
void dram_refresh(DRAMController *ctrl);

/* L2: Address mapping (RoBaCoCh — Row:Bank:Column:Channel) */
void dram_decode_address(uint64_t addr, uint32_t *bank, uint32_t *row, uint32_t *col,
                          uint32_t *rank);

/* L3: Command scheduling */
DRAMCommand *dram_schedule_command(DRAMController *ctrl);

/* L3: Bank management */
bool dram_bank_ready(const DRAMBank *bank, uint64_t current_cycle);
void dram_bank_activate(DRAMBank *bank, uint32_t row, uint64_t cycle,
                        const DRAMTiming *t);
void dram_bank_precharge(DRAMBank *bank, uint64_t cycle, const DRAMTiming *t);

/* L3: Timing constraint checking */
bool dram_check_tFAW(const DRAMController *ctrl);
bool dram_check_tRRD(const DRAMController *ctrl, uint32_t bank);
uint64_t dram_row_buffer_hit_latency(const DRAMController *ctrl);
uint64_t dram_row_buffer_miss_latency(const DRAMController *ctrl);

/* L5: FR-FCFS scheduler (Rixner et al. 2000 — ISCA) */
int32_t dram_frfcfs_rank(const MemRequest *a, const MemRequest *b,
                          const DRAMBank *banks);

/* L5: PAR-BS scheduler (Mutlu & Moscibroda 2008 — MICRO) */
void dram_parbs_batch(DRAMController *ctrl);
void dram_parbs_rank(DRAMController *ctrl);

/* L8: Thread interference */
void dram_interference_start(DRAMController *ctrl, uint32_t thread_id);
void dram_interference_stop(DRAMController *ctrl, uint32_t thread_id);
double dram_slowdown(const DRAMController *ctrl, uint32_t thread_id);

/* Stats */
void dram_print_stats(const DRAMController *ctrl);
void dram_print_bank_state(const DRAMController *ctrl);
double dram_utilization(const DRAMController *ctrl);

/* L7: Address hashing for load balancing (Zhang 2000) */
uint64_t dram_permutation_hash(uint64_t addr, uint32_t hash_bits);
uint64_t dram_xor_hash(uint64_t addr, uint32_t hash_mask);

#endif
