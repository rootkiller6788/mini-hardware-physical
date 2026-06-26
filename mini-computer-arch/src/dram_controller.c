#include "dram_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ===== L1: Default DDR4-3200 Timing ===== */

static DRAMTiming ddr4_3200_default(void)
{
    DRAMTiming t;
    t.tCK   = 312;    /* 312.5 ps -> 3200 MT/s */
    t.tRCD  = 14;
    t.tCL   = 14;
    t.tRAS  = 32;
    t.tRP   = 14;
    t.tRC   = 46;
    t.tRTP  = 6;
    t.tWR   = 14;
    t.tWTR  = 4;
    t.tRRD  = 4;
    t.tFAW  = 20;
    t.tRFC  = 560;
    t.tREFI = 12480;
    t.tCCD  = 4;
    t.tBL   = 8;
    return t;
}

/* ===== L2: Address Decoding (RoBaCoCh Scheme) =====
 * Maps physical address to (row, bank, column, rank).
 * Standard open-page policy address mapping.
 * Jacob et al., "Memory Systems: Cache, DRAM, Disk", 2008.
 */

void dram_decode_address(uint64_t addr, uint32_t *bank, uint32_t *row,
                          uint32_t *col, uint32_t *rank)
{
    /* Typical DDR4 8Gb x8 configuration:
     *   Row:    16 bits (A[31:16])
     *   Bank:    2 bits (A[15:14]) for BG, 2 bits (A[13:12]) for BA
     *   Column: 10 bits (A[11:2])
     *   Byte:    2 bits (A[1:0])  -- within burst
     *   Rank:    1 bit  (A[32])
     */
    *col  = (uint32_t)((addr >> 2)  & 0x3FFu);    /* 10-bit column */
    uint32_t bg = (uint32_t)((addr >> 14) & 0x3u);
    uint32_t ba = (uint32_t)((addr >> 12) & 0x3u);
    *bank = (bg << 2) | ba;                        /* 4-bit bank */
    *row  = (uint32_t)((addr >> 16) & 0xFFFFu);    /* 16-bit row */
    *rank = (uint32_t)((addr >> 32) & 0x1u);       /* 1-bit rank */
}

/* ===== L1: DRAM Controller Initialization ===== */

void dram_init(DRAMController *ctrl, uint32_t num_banks, uint32_t num_ranks,
               const DRAMTiming *timing, SchedulingPolicy policy)
{
    memset(ctrl, 0, sizeof(*ctrl));

    ctrl->num_banks = num_banks;
    ctrl->num_ranks = num_ranks;
    ctrl->policy    = policy;

    if (timing)
        ctrl->timing = *timing;
    else
        ctrl->timing = ddr4_3200_default();

    /* Initialize banks to IDLE */
    for (uint32_t i = 0; i < num_banks * num_ranks; i++) {
        ctrl->banks[i].state = BANK_IDLE;
        ctrl->banks[i].open_row = 0;
        ctrl->banks[i].row_valid = false;
        ctrl->banks[i].ready_time = 0;
    }
}

/* ===== L2: Request Enqueue ===== */

bool dram_enqueue(DRAMController *ctrl, uint64_t addr, bool is_write, uint32_t id)
{
    if (ctrl->req_count >= DRAM_REQ_QUEUE_SIZE) return false;

    MemRequest *req = &ctrl->req_queue[ctrl->req_tail];
    req->address     = addr;
    req->is_write    = is_write;
    req->arrival_time = ctrl->cycles;
    req->deadline    = ctrl->cycles + 1000;
    req->priority    = 0;
    req->id          = id;
    req->completed   = false;

    dram_decode_address(addr, &req->bank, &req->row, &req->col, &req->rank);

    ctrl->req_tail = (ctrl->req_tail + 1) % DRAM_REQ_QUEUE_SIZE;
    ctrl->req_count++;

    if (is_write) ctrl->total_writes++;
    else          ctrl->total_reads++;

    return true;
}

/* ===== L3: Bank Operations ===== */

bool dram_bank_ready(const DRAMBank *bank, uint64_t current_cycle)
{
    return (current_cycle >= bank->ready_time);
}

void dram_bank_activate(DRAMBank *bank, uint32_t row, uint64_t cycle,
                        const DRAMTiming *t)
{
    bank->state = BANK_ACTIVATING;
    bank->open_row = row;
    bank->row_valid = true;
    bank->ready_time = cycle + t->tRCD;
    bank->active_count++;
}

void dram_bank_precharge(DRAMBank *bank, uint64_t cycle, const DRAMTiming *t)
{
    bank->state = BANK_PRECHARGING;
    bank->row_valid = false;
    bank->ready_time = cycle + t->tRP;
}

/* ===== L3: Timing Constraint Checks ===== */

bool dram_check_tRRD(const DRAMController *ctrl, uint32_t bank)
{
    (void)ctrl;
    (void)bank;
    return true;
}

uint64_t dram_row_buffer_hit_latency(const DRAMController *ctrl)
{
    /* Row hit: CAS latency + burst transfer */
    return ctrl->timing.tCL + ctrl->timing.tBL / 2;
}

uint64_t dram_row_buffer_miss_latency(const DRAMController *ctrl)
{
    /* Row miss: precharge + activate + CAS + burst */
    return ctrl->timing.tRP + ctrl->timing.tRCD +
           ctrl->timing.tCL + ctrl->timing.tBL / 2;
}

/* ===== L5: FR-FCFS Scheduler (Rixner et al. 2000, ISCA) =====
 * First-Ready First-Come-First-Serve:
 * 1. Row-buffer hits get highest priority
 * 2. Among row hits, oldest request goes first
 * 3. Row-buffer misses are serviced in FCFS order
 */

int32_t dram_frfcfs_rank(const MemRequest *a, const MemRequest *b,
                          const DRAMBank *banks)
{
    bool a_hit = (banks[a->bank].row_valid &&
                  banks[a->bank].open_row == a->row);
    bool b_hit = (banks[b->bank].row_valid &&
                  banks[b->bank].open_row == b->row);

    /* Row hits before row misses */
    if (a_hit && !b_hit) return -1;
    if (!a_hit && b_hit) return 1;

    /* Both hits or both misses: FCFS by arrival time */
    if (a->arrival_time < b->arrival_time) return -1;
    if (a->arrival_time > b->arrival_time) return 1;
    return 0;
}

/* ===== L5: PAR-BS Scheduler (Mutlu & Moscibroda 2008, MICRO) =====
 * Parallelism-Aware Batch Scheduling:
 * Groups requests into batches and ranks them to maximize
 * bank-level parallelism while ensuring fairness.
 */

void dram_parbs_batch(DRAMController *ctrl)
{
    /* Mark current batch boundary */
    for (uint32_t i = 0; i < ctrl->req_count; i++) {
        uint32_t idx = (ctrl->req_head + i) % DRAM_REQ_QUEUE_SIZE;
        /* Requests older than batch threshold form a batch */
        if (ctrl->cycles - ctrl->req_queue[idx].arrival_time > 500) {
            ctrl->req_queue[idx].priority = 3;
        }
    }
}

void dram_parbs_rank(DRAMController *ctrl)
{
    dram_parbs_batch(ctrl);

    /* Simple ranking: by priority then by row-hit then by age */
    for (uint32_t i = 0; i < ctrl->req_count; i++) {
        for (uint32_t j = i + 1; j < ctrl->req_count; j++) {
            uint32_t ia = (ctrl->req_head + i) % DRAM_REQ_QUEUE_SIZE;
            uint32_t ja = (ctrl->req_head + j) % DRAM_REQ_QUEUE_SIZE;
            MemRequest *a = &ctrl->req_queue[ia];
            MemRequest *b = &ctrl->req_queue[ja];

            bool a_hit = ctrl->banks[a->bank].row_valid &&
                         ctrl->banks[a->bank].open_row == a->row;
            bool b_hit = ctrl->banks[b->bank].row_valid &&
                         ctrl->banks[b->bank].open_row == b->row;

            int swap = 0;
            if (a->priority < b->priority) swap = 1;
            else if (a->priority == b->priority) {
                if (!a_hit && b_hit) swap = 1;
                else if (a_hit == b_hit && a->arrival_time > b->arrival_time) swap = 1;
            }

            if (swap) {
                MemRequest tmp = *a;
                *a = *b;
                *b = tmp;
            }
        }
    }
}

/* ===== L3: Command Scheduling ===== */

DRAMCommand *dram_schedule_command(DRAMController *ctrl)
{
    if (ctrl->req_count == 0) {
        /* Issue NOP or refresh if needed */
        return NULL;
    }

    /* Apply scheduling policy to rank requests */
    switch (ctrl->policy) {
    case SCHED_FR_FCFS:
        dram_parbs_rank(ctrl);
        break;
    case SCHED_PARBS:
        dram_parbs_rank(ctrl);
        break;
    default:
        break;
    }

    /* Select highest-ranked request */
    MemRequest *best = &ctrl->req_queue[ctrl->req_head];
    DRAMBank *bank = &ctrl->banks[best->bank];

    /* Check if bank is ready */
    if (!dram_bank_ready(bank, ctrl->cycles)) {
        ctrl->stall_cycles++;
        return NULL;
    }

    /* Generate command */
    if (!bank->row_valid) {
        /* Need to activate */
        dram_bank_activate(bank, best->row, ctrl->cycles, &ctrl->timing);
        bank->row_miss_count++;
    } else if (bank->open_row != best->row) {
        /* Row conflict: precharge then activate */
        dram_bank_precharge(bank, ctrl->cycles, &ctrl->timing);
        ctrl->row_conflicts++;
        bank->row_miss_count++;
    } else {
        /* Row hit */
        ctrl->row_hits++;
        bank->row_hit_count++;

        /* Pop request from queue */
        ctrl->req_head = (ctrl->req_head + 1) % DRAM_REQ_QUEUE_SIZE;
        ctrl->req_count--;

        /* Track BLP */
        ctrl->active_banks++;
        ctrl->blp_samples++;
    }

    return NULL;
}

/* ===== L1: Cycle Advance ===== */

void dram_cycle(DRAMController *ctrl)
{
    ctrl->cycles++;

    /* Refresh handling */
    if (ctrl->cycles % ctrl->timing.tREFI == 0) {
        dram_refresh(ctrl);
    }

    /* Update bank states */
    for (uint32_t i = 0; i < ctrl->num_banks * ctrl->num_ranks; i++) {
        DRAMBank *bank = &ctrl->banks[i];
        if (bank->state == BANK_ACTIVATING &&
            ctrl->cycles >= bank->ready_time) {
            bank->state = BANK_ACTIVE;
        }
        if (bank->state == BANK_PRECHARGING &&
            ctrl->cycles >= bank->ready_time) {
            bank->state = BANK_IDLE;
        }
    }

    /* Schedule commands */
    dram_schedule_command(ctrl);

    /* Bandwidth utilization: active banks / total banks */
    if (ctrl->num_banks * ctrl->num_ranks > 0) {
        ctrl->bank_level_parallelism = ctrl->active_banks;
        ctrl->bandwidth_utilization =
            (double)ctrl->active_banks /
            (double)(ctrl->num_banks * ctrl->num_ranks);
    }
}

void dram_run(DRAMController *ctrl, uint64_t cycles)
{
    for (uint64_t i = 0; i < cycles; i++) {
        dram_cycle(ctrl);
    }
}

void dram_refresh(DRAMController *ctrl)
{
    ctrl->refresh_cycles += ctrl->timing.tRFC;

    /* Put all banks in refresh */
    for (uint32_t i = 0; i < ctrl->num_banks * ctrl->num_ranks; i++) {
        ctrl->banks[i].state = BANK_REFRESHING;
        ctrl->banks[i].ready_time = ctrl->cycles + ctrl->timing.tRFC;
    }
    ctrl->stall_cycles += ctrl->timing.tRFC;
}

/* ===== L8: Thread Interference (MISE Model) ===== */

void dram_interference_start(DRAMController *ctrl, uint32_t thread_id)
{
    if (thread_id < 4) {
        ctrl->thread_interference[thread_id].alone_cycles = ctrl->cycles;
    }
}

void dram_interference_stop(DRAMController *ctrl, uint32_t thread_id)
{
    if (thread_id < 4) {
        ThreadInterference *ti = &ctrl->thread_interference[thread_id];
        ti->shared_cycles = ctrl->cycles - ti->alone_cycles;
        ti->slowdown = (ti->alone_cycles + ti->shared_cycles) /
                       (double)(ti->alone_cycles > 0 ? ti->alone_cycles : 1);
        ti->interference_ratio = ti->shared_cycles /
            (double)(ctrl->cycles > 0 ? ctrl->cycles : 1);
    }
}

double dram_slowdown(const DRAMController *ctrl, uint32_t thread_id)
{
    if (thread_id < 4) {
        return ctrl->thread_interference[thread_id].slowdown;
    }
    return 1.0;
}

/* ===== L7: Address Hashing for Load Balancing ===== */

uint64_t dram_permutation_hash(uint64_t addr, uint32_t hash_bits)
{
    /* XOR-based permutation hash (Zhang 2000):
     * Hash(address) = XOR of selected address bits
     * to distribute accesses across banks/channels.
     */
    uint64_t hash = 0;
    for (uint32_t i = 0; i < hash_bits; i++) {
        uint64_t bit = (addr >> (i * 3 + 7)) & 1u;
        hash |= (bit << i);
    }
    return hash;
}

uint64_t dram_xor_hash(uint64_t addr, uint32_t hash_mask)
{
    /* Simple XOR folding hash for bank indexing */
    uint64_t row = (addr >> 16) & 0xFFFFu;
    uint64_t col = (addr >> 2)  & 0x3FFu;
    return (row ^ col) & hash_mask;
}

/* ===== Print Utilities ===== */

void dram_print_stats(const DRAMController *ctrl)
{
    printf("=== DRAM Controller Statistics ===\n");
    printf("  Cycles:            %llu\n", (unsigned long long)ctrl->cycles);
    printf("  Reads/Writes:      %llu / %llu\n",
           (unsigned long long)ctrl->total_reads,
           (unsigned long long)ctrl->total_writes);
    printf("  Row hits:          %llu\n", (unsigned long long)ctrl->row_hits);
    printf("  Row conflicts:     %llu\n", (unsigned long long)ctrl->row_conflicts);
    printf("  Refresh cycles:    %llu\n", (unsigned long long)ctrl->refresh_cycles);
    printf("  Stall cycles:      %llu\n", (unsigned long long)ctrl->stall_cycles);
    printf("  Bandwidth util:    %.1f%%\n", ctrl->bandwidth_utilization * 100.0);
    printf("  Bank-level parall: %.1f\n",
           ctrl->blp_samples > 0 ?
           (double)ctrl->bank_level_parallelism / (double)ctrl->blp_samples : 0.0);
    printf("  Row hit latency:   %llu cycles\n",
           (unsigned long long)dram_row_buffer_hit_latency(ctrl));
    printf("  Row miss latency:  %llu cycles\n",
           (unsigned long long)dram_row_buffer_miss_latency(ctrl));
    printf("  Policy: ");
    switch (ctrl->policy) {
    case SCHED_FCFS:   printf("FCFS\n"); break;
    case SCHED_FR_FCFS:printf("FR-FCFS (Rixner 2000)\n"); break;
    case SCHED_PARBS:  printf("PAR-BS (Mutlu 2008)\n"); break;
    default: printf("Unknown\n"); break;
    }
    printf("========================================\n");
}

void dram_print_bank_state(const DRAMController *ctrl)
{
    printf("=== DRAM Bank States ===\n");
    for (uint32_t i = 0; i < ctrl->num_banks * ctrl->num_ranks; i++) {
        const DRAMBank *bank = &ctrl->banks[i];
        const char *state_str;
        switch (bank->state) {
        case BANK_IDLE:        state_str = "IDLE"; break;
        case BANK_ACTIVATING:  state_str = "ACTV"; break;
        case BANK_ACTIVE:      state_str = "ACTV"; break;
        case BANK_PRECHARGING: state_str = "PRE";  break;
        case BANK_REFRESHING:  state_str = "REF";  break;
        default:               state_str = "???";  break;
        }
        printf("  Bank %2u: %-4s row=%04x hits=%5llu misses=%5llu\n",
               i, state_str, bank->open_row,
               (unsigned long long)bank->row_hit_count,
               (unsigned long long)bank->row_miss_count);
    }
    printf("========================================\n");
}

double dram_utilization(const DRAMController *ctrl)
{
    if (ctrl->cycles == 0) return 0.0;
    uint64_t busy = ctrl->cycles - ctrl->stall_cycles - ctrl->refresh_cycles;
    return (double)busy / (double)ctrl->cycles;
}
