#include "ssd_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static bool cq_is_full(const CommandQueue *cq) {
    return cq->count >= SSDC_QUEUE_DEPTH;
}

static bool cq_is_empty(const CommandQueue *cq) {
    return cq->count == 0;
}

static int cq_enqueue(CommandQueue *cq, const IOCommand *cmd) {
    if (cq_is_full(cq)) return -1;
    cq->queue[cq->tail] = *cmd;
    cq->tail = (cq->tail + 1) % SSDC_QUEUE_DEPTH;
    cq->count++;
    return 0;
}

static int cq_dequeue(CommandQueue *cq, IOCommand *out_cmd) {
    if (cq_is_empty(cq)) return -1;
    *out_cmd = cq->queue[cq->head];
    cq->head = (cq->head + 1) % SSDC_QUEUE_DEPTH;
    cq->count--;
    return 0;
}

void ssdc_init(SSDController *ctrl) {
    uint32_t i;

    memset(ctrl, 0, sizeof(SSDController));
    ctrl->current_cycle = 0;
    ctrl->busy_channels = 0;

    for (i = 0; i < SSDC_CHANNELS; i++) {
        ctrl->channels[i].busy_cycles   = 0;
        ctrl->channels[i].transfer_rate = SSDC_TRANSFER_RATE;
    }

    ftl_init(&ctrl->ftl, FTL_MAPPING_PAGE_LEVEL);
}

int ssdc_submit_io(SSDController *ctrl, IOType type, uint32_t lba,
                   const uint8_t *data) {
    IOCommand cmd;
    cmd.type   = type;
    cmd.lba    = lba;
    cmd.length = 1;
    cmd.status = IO_PENDING;
    cmd.cmd_id = ctrl->issue_queue.count;

    if (data && (type == IO_WRITE)) {
        memcpy(cmd.data, data, 4096);
    } else {
        memset(cmd.data, 0, 4096);
    }

    return cq_enqueue(&ctrl->issue_queue, &cmd);
}

void ssdc_process(SSDController *ctrl, uint64_t cycles) {
    uint64_t target = ctrl->current_cycle + cycles;

    while (ctrl->current_cycle < target) {
        while (!cq_is_empty(&ctrl->issue_queue) && ctrl->busy_channels < SSDC_CHANNELS) {
            IOCommand cmd;
            if (cq_dequeue(&ctrl->issue_queue, &cmd) == 0) {
                cmd.status = IO_ISSUED;
                ctrl->busy_channels++;

                switch (cmd.type) {
                case IO_READ: {
                    int rc = ftl_read(&ctrl->ftl, cmd.lba, cmd.data);
                    cmd.status = (rc == 0) ? IO_COMPLETE : IO_ERROR;
                    break;
                }
                case IO_WRITE: {
                    int rc = ftl_write(&ctrl->ftl, cmd.lba, cmd.data);
                    cmd.status = (rc == 0) ? IO_COMPLETE : IO_ERROR;
                    break;
                }
                case IO_ERASE:
                    cmd.status = IO_COMPLETE;
                    break;
                }

                cq_enqueue(&ctrl->completion_queue, &cmd);
                ctrl->busy_channels--;
            }
        }

        ctrl->current_cycle++;
    }
}

int ssdc_complete(SSDController *ctrl, IOCommand *out_cmd) {
    return cq_dequeue(&ctrl->completion_queue, out_cmd);
}

/* ── Channel Interleaving and Die-Level Parallelism ──
 *
 * L3: Modern SSDs use multiple channels (independent NAND buses)
 * and multiple dies per channel (chip enable selects).
 * Interleaving allows concurrent operations:
 *   While die 0 is busy programming, die 1 can receive a read command.
 *
 * Effective bandwidth = channels * dies_per_channel * NAND_IO_rate
 * Typical: 8ch x 4die x 400MT/s = 12.8 GB/s theoretical.
 */
int ssdc_interleaved_submit(SSDController *ctrl, IOType type,
                            uint32_t lba, const uint8_t *data) {
    uint32_t ch, target_ch;
    NANDChannel *chan;

    /* Round-robin channel assignment */
    target_ch = lba % SSDC_CHANNELS;

    /* Check if target channel is busy; if so, try next */
    for (ch = 0; ch < SSDC_CHANNELS; ch++) {
        uint32_t try_ch = (target_ch + ch) % SSDC_CHANNELS;
        chan = &ctrl->channels[try_ch];
        if (chan->busy_cycles == 0) {
            IOCommand cmd;
            cmd.type   = type;
            cmd.lba    = lba;
            cmd.length = 1;
            cmd.status = IO_PENDING;
            cmd.cmd_id = ctrl->issue_queue.count;
            if (data && type == IO_WRITE) {
                memcpy(cmd.data, data, 4096);
            } else {
                memset(cmd.data, 0, 4096);
            }

            chan->busy_cycles = (type == IO_READ)  ? SSDC_NAND_READ_NS :
                                (type == IO_WRITE) ? SSDC_NAND_WRITE_NS :
                                                     SSDC_NAND_ERASE_NS;
            chan->busy_cycles /= 1000; /* ns -> us for cycle-based sim */
            if (chan->busy_cycles == 0) chan->busy_cycles = 1;

            return cq_enqueue(&ctrl->issue_queue, &cmd);
        }
    }
    return -1; /* all channels busy */
}

/* ── Power State Management (PS0-PS4) ──
 *
 * L7: NVMe defines power states PS0 (full) through PS4 (deepest sleep).
 * Each state trades latency for power:
 *   PS0: Full power, ~0 entry/exit latency, ~8W
 *   PS1: Light sleep, ~100us entry, 1ms exit, ~4W
 *   PS2: Medium sleep, ~500us entry, 10ms exit, ~2W
 *   PS3: Deep sleep, ~5ms entry, 50ms exit, ~0.05W
 *   PS4: Off/inaccessible, controller reset required, ~0.005W
 *
 * Real SSDs transition between power states based on idle time.
 */
typedef struct {
    SSDPowerState current_state;
    double        power_watts[5];
    uint64_t      entry_latency_us[5];
    uint64_t      exit_latency_us[5];
    uint64_t      idle_time_us;
    uint64_t      total_energy_uj;
} SSDPowerManager;

static SSDPowerManager ssd_pm;

void ssdc_power_init(void) {
    memset(&ssd_pm, 0, sizeof(ssd_pm));
    ssd_pm.current_state = SSDC_PS0_FULL;
    ssd_pm.power_watts[0] = 8.0;
    ssd_pm.power_watts[1] = 4.0;
    ssd_pm.power_watts[2] = 2.0;
    ssd_pm.power_watts[3] = 0.05;
    ssd_pm.power_watts[4] = 0.005;
    ssd_pm.entry_latency_us[1] = 100;
    ssd_pm.exit_latency_us[1]  = 1000;
    ssd_pm.entry_latency_us[2] = 500;
    ssd_pm.exit_latency_us[2]  = 10000;
    ssd_pm.entry_latency_us[3] = 5000;
    ssd_pm.exit_latency_us[3]  = 50000;
    ssd_pm.entry_latency_us[4] = 10000;
    ssd_pm.exit_latency_us[4]  = 100000;
}

int ssdc_power_transition(SSDPowerState target) {
    uint64_t latency;

    if (target == ssd_pm.current_state) return 0;

    /* Exit current state latency */
    if (ssd_pm.current_state > SSDC_PS0_FULL) {
        latency = ssd_pm.exit_latency_us[ssd_pm.current_state];
    } else {
        latency = 0;
    }

    /* Entry to target state */
    if (target > SSDC_PS0_FULL) {
        latency += ssd_pm.entry_latency_us[target];
    }

    ssd_pm.current_state = target;
    return (int)latency;
}

/* Auto power state transition based on idle time */
int ssdc_power_auto_transition(uint64_t idle_us) {
    ssd_pm.idle_time_us = idle_us;

    if (idle_us > 10000000) {     /* 10s idle -> PS3 */
        return ssdc_power_transition(SSDC_PS3_DEEP);
    } else if (idle_us > 1000000) { /* 1s idle -> PS2 */
        return ssdc_power_transition(SSDC_PS2_MEDIUM);
    } else if (idle_us > 100000) { /* 100ms idle -> PS1 */
        return ssdc_power_transition(SSDC_PS1_LIGHT);
    }
    return ssdc_power_transition(SSDC_PS0_FULL);
}

double ssdc_power_get_watts(void) {
    return ssd_pm.power_watts[ssd_pm.current_state];
}

/* ── Thermal Throttling Model ──
 *
 * L8: SSD controllers throttle performance when temperature exceeds
 * thresholds to prevent NAND damage (typically >70C for consumer,
 * >85C for industrial). Throttling reduces IOPS by reducing
 * channel utilization.
 *
 * Thermal model: T(t) = T_ambient + P * R_theta * (1 - exp(-t/tau))
 * where R_theta is thermal resistance and tau is thermal time constant.
 */
#define SSDC_TEMP_AMBIENT   35.0   /* degrees C */
#define SSDC_TEMP_THROTTLE  70.0   /* throttle starts */
#define SSDC_TEMP_CRITICAL  85.0   /* emergency shutdown */
#define SSDC_THERMAL_R      15.0   /* thermal resistance, C/W */
#define SSDC_THERMAL_TAU    120.0  /* time constant, seconds */

typedef struct {
    double   temperature;
    double   throttle_factor;  /* 1.0 = no throttle, 0.0 = stopped */
    bool     critical;
    uint64_t throttle_events;
} SSDThermal;

static SSDThermal ssd_thermal;

void ssdc_thermal_init(void) {
    memset(&ssd_thermal, 0, sizeof(ssd_thermal));
    ssd_thermal.temperature = SSDC_TEMP_AMBIENT;
    ssd_thermal.throttle_factor = 1.0;
    ssd_thermal.critical = false;
}

void ssdc_thermal_update(double power_watts, double dt_seconds) {
    /* Steady-state temperature */
    double t_steady = SSDC_TEMP_AMBIENT + power_watts * SSDC_THERMAL_R;
    /* Exponential approach */
    double alpha = 1.0 - exp(-dt_seconds / SSDC_THERMAL_TAU);
    ssd_thermal.temperature += alpha * (t_steady - ssd_thermal.temperature);

    /* Throttle control */
    if (ssd_thermal.temperature >= SSDC_TEMP_CRITICAL) {
        ssd_thermal.throttle_factor = 0.0;
        ssd_thermal.critical = true;
    } else if (ssd_thermal.temperature >= SSDC_TEMP_THROTTLE) {
        /* Linear throttle from 70C to 85C */
        double range = SSDC_TEMP_CRITICAL - SSDC_TEMP_THROTTLE;
        ssd_thermal.throttle_factor = 1.0 - (ssd_thermal.temperature - SSDC_TEMP_THROTTLE) / range;
        if (ssd_thermal.throttle_factor < 0.0) ssd_thermal.throttle_factor = 0.0;
        ssd_thermal.throttle_events++;
    } else {
        ssd_thermal.throttle_factor = 1.0;
    }
}

double ssdc_thermal_get_throttle(void) {
    return ssd_thermal.throttle_factor;
}

/* ── QoS-Weighted Scheduling ──
 *
 * L5: Different I/O priorities (high, medium, low) get different
 * portions of SSD bandwidth. Enterprise SSDs use this to meet
 * latency SLAs for critical workloads.
 */
#define SSDC_QOS_NUM 3

typedef struct {
    uint64_t credits[SSDC_QOS_NUM];
    uint64_t consumed[SSDC_QOS_NUM];
} SSDIOScheduler;

static SSDIOScheduler ssd_sched;

void ssdc_qos_init(void) {
    memset(&ssd_sched, 0, sizeof(ssd_sched));
    ssd_sched.credits[SSDC_QOS_HIGH]   = 100;
    ssd_sched.credits[SSDC_QOS_MEDIUM] = 50;
    ssd_sched.credits[SSDC_QOS_LOW]    = 10;
}

int ssdc_qos_select_queue(void) {
    /* Select highest priority queue with remaining credits */
    int i;
    for (i = 0; i < SSDC_QOS_NUM; i++) {
        if (ssd_sched.credits[i] > ssd_sched.consumed[i]) {
            ssd_sched.consumed[i]++;
            return i;
        }
    }
    /* Reset credits and restart */
    for (i = 0; i < SSDC_QOS_NUM; i++) {
        ssd_sched.consumed[i] = 0;
    }
    ssd_sched.consumed[SSDC_QOS_HIGH]++;
    return SSDC_QOS_HIGH;
}

void ssdc_print_queue(const SSDController *ctrl) {
    printf("SSD Controller State:\n");
    printf("  Current Cycle:      %llu\n",
           (unsigned long long)ctrl->current_cycle);
    printf("  Issue Queue Depth:  %u / %u\n",
           ctrl->issue_queue.count, SSDC_QUEUE_DEPTH);
    printf("  Completion Queue:   %u / %u\n",
           ctrl->completion_queue.count, SSDC_QUEUE_DEPTH);
    printf("  Busy Channels:      %llu / %u\n",
           (unsigned long long)ctrl->busy_channels, SSDC_CHANNELS);
    printf("  SRAM Used:          %u / %u bytes\n",
           ctrl->sram_used, SSDC_SRAM_SIZE);
    ftl_print_stats(&ctrl->ftl);
}
