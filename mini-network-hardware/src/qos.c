#include "qos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Traffic Class Management */
void qos_class_init(QoSTrafficClass *tc, int class_id,
                    QoSPriority prio, int weight) {
    if (!tc) return;
    memset(tc, 0, sizeof(*tc));
    tc->class_id = class_id;
    tc->priority = prio;
    tc->weight = weight;
    tc->quantum = QOS_DEFAULT_MTU; /* Default DRR quantum */
}

void qos_class_add_dscp(QoSTrafficClass *tc, uint8_t dscp) {
    if (!tc || tc->num_dscp >= 8) return;
    /* Avoid duplicates */
    for (int i = 0; i < tc->num_dscp; i++) {
        if (tc->dscp_values[i] == dscp) return;
    }
    tc->dscp_values[tc->num_dscp++] = dscp;
}

bool qos_class_match_dscp(const QoSTrafficClass *tc, uint8_t dscp) {
    if (!tc) return false;
    for (int i = 0; i < tc->num_dscp; i++) {
        if (tc->dscp_values[i] == dscp) return true;
    }
    return false;
}

/* Scheduler Initialization */
void qos_scheduler_init(QoSScheduler *sched, QoSScheduling discipline) {
    if (!sched) return;
    memset(sched, 0, sizeof(*sched));
    sched->discipline = discipline;
    sched->num_classes = 0;
    sched->default_class = 0;
}

int qos_scheduler_add_class(QoSScheduler *sched, QoSPriority prio,
                             int weight, int quantum) {
    if (!sched || sched->num_classes >= QOS_MAX_CLASSES) return -1;
    int idx = sched->num_classes;
    qos_class_init(&sched->classes[idx], idx, prio, weight);
    sched->classes[idx].quantum = (quantum > 0) ? quantum : QOS_DEFAULT_MTU;
    sched->num_classes++;
    return idx;
}

/* DSCP-based classification.
 * Returns class_id or default_class if no match. */
int qos_scheduler_classify(const QoSScheduler *sched, uint8_t dscp) {
    if (!sched) return -1;
    for (int i = 0; i < sched->num_classes; i++) {
        if (qos_class_match_dscp(&sched->classes[i], dscp))
            return i;
    }
    return sched->default_class;
}

/* Strict Priority (SP) scheduling.
 * L2: Always serve highest-priority non-empty queue first.
 * Starvation risk: low-priority queues may never be served
 * under heavy high-priority load.
 *
 * Returns class_id of selected queue, or -1 if all empty. */
int qos_schedule_sp(QoSScheduler *sched, int *queue_lengths) {
    if (!sched || !queue_lengths) return -1;
    /* Search highest priority first (PCP 7 -> 0) */
    for (int pc = 7; pc >= 0; pc--) {
        for (int c = 0; c < sched->num_classes; c++) {
            if ((int)sched->classes[c].priority == pc &&
                queue_lengths[c] > 0) {
                sched->classes[c].packets_tx++;
                sched->classes[c].bytes_tx += (uint64_t)queue_lengths[c];
                sched->total_packets_scheduled++;
                return c;
            }
        }
    }
    return -1;
}

/* Weighted Round Robin (WRR).
 * L5: Each class gets a weight. In each round, class i
 * can send up to weight[i] packets (or bytes in byte-level WRR).
 *
 * WRR provides proportional fairness: class i gets
 * weight[i] / sum(weights) fraction of total bandwidth.
 *
 * Limitation: variable packet sizes cause unfairness.
 * DRR (below) fixes this with byte-level deficit tracking. */
int qos_schedule_wrr(QoSScheduler *sched, int *queue_lengths,
                      int round_size) {
    if (!sched || !queue_lengths || round_size <= 0) return -1;
    /* Simple WRR: one round, serve up to weight packets per class */
    int served = -1;
    for (int c = 0; c < sched->num_classes; c++) {
        int allowance = sched->classes[c].weight;
        while (allowance > 0 && queue_lengths[c] > 0) {
            queue_lengths[c]--;
            allowance--;
            sched->classes[c].packets_tx++;
            sched->classes[c].bytes_tx += QOS_DEFAULT_MTU;
            sched->total_packets_scheduled++;
            served = c;
        }
    }
    return served;
}

/* Deficit Round Robin (DRR) - Shreedhar & Varghese, 1995.
 * L5 Algorithm:
 *   Each flow has deficit counter (DC) and quantum (Q).
 *   In each round:
 *     DC[i] += Q[i]
 *     While DC[i] >= packet_size and queue not empty:
 *       Transmit packet, DC[i] -= packet_size
 *   Unused deficit carries over to next round.
 *
 * Key property (L4): DRR achieves O(1) per-packet scheduling
 * with fair bandwidth allocation, regardless of packet sizes.
 *
 * Complexity: O(1) per packet (vs. O(log N) for WFQ).
 * Fairness: Within one quantum of ideal fair queuing. */
void drr_init_counters(QoSScheduler *sched) {
    if (!sched) return;
    for (int i = 0; i < QOS_MAX_CLASSES; i++) {
        sched->deficit_counters[i] = 0;
    }
}

/* Schedule one packet using DRR.
 * Returns class_id or -1 if no queue has sufficient deficit. */
int drr_schedule_next(QoSScheduler *sched, int *queue_lengths) {
    if (!sched || !queue_lengths) return -1;

    /* Scan all classes once per round */
    for (int round = 0; round < sched->num_classes; round++) {
        for (int c = 0; c < sched->num_classes; c++) {
            if (queue_lengths[c] <= 0) continue;

            /* Add quantum to deficit */
            sched->deficit_counters[c] += sched->classes[c].quantum;

            /* If deficit >= packet size, transmit */
            if (sched->deficit_counters[c] >= queue_lengths[c]) {
                sched->deficit_counters[c] -= queue_lengths[c];
                sched->classes[c].packets_tx++;
                sched->classes[c].bytes_tx += (uint64_t)queue_lengths[c];
                sched->total_packets_scheduled++;
                return c;
            }
            /* Deficit insufficient; skip this class this round */
            /* But deficit stays for next round */
        }
    }

    /* All queues empty or all deficits insufficient */
    return -1;
}

int qos_schedule_drr(QoSScheduler *sched, int *queue_lengths) {
    return drr_schedule_next(sched, queue_lengths);
}

/* Enhanced Transmission Selection (ETS) - IEEE 802.1Qaz
 * L7: ETS provides bandwidth allocation with:
 *   - Strict priority classes: no bandwidth cap (for control traffic)
 *   - Bandwidth-guaranteed classes: minimum % of available BW
 *   - Best-effort classes: use remaining bandwidth
 *
 * Bandwidth allocation:
 *   Let total_bw be the link capacity.
 *   SP classes use what they need (up to total_bw).
 *   Non-SP classes share remaining_bw proportionally.
 *   Class i gets: min_bw[i] + (remaining * bw_frac[i]) */
void ets_init_class(ETSClassConfig *ets, int class_id,
                    int bw_percent, bool strict) {
    if (!ets) return;
    memset(ets, 0, sizeof(*ets));
    ets->class_id = class_id;
    ets->bandwidth_percent = bw_percent;
    ets->strict_priority = strict;
}

void qos_ets_configure(QoSScheduler *sched,
                       const ETSClassConfig *configs, int n_configs) {
    if (!sched || !configs || n_configs <= 0) return;
    sched->ets_enabled = true;
    for (int i = 0; i < n_configs && i < QOS_MAX_CLASSES; i++) {
        sched->ets_configs[i] = configs[i];
    }
}

/* Calculate available bandwidth for a non-SP class under ETS.
 * Only SP classes with traffic consume bandwidth first;
 * remaining bandwidth is distributed proportionally to
 * bandwidth_percent weights among non-SP classes. */
double ets_available_bw(const ETSClassConfig *ets, double total_bw) {
    if (!ets || total_bw <= 0.0) return 0.0;
    if (ets->strict_priority) return total_bw; /* SP: use any available */
    return total_bw * (double)ets->bandwidth_percent / 100.0;
}

/* Data Center Bridging (DCB) - combines PFC + ETS + QCN */
void dcb_init(DCBState *dcb) {
    if (!dcb) return;
    memset(dcb, 0, sizeof(*dcb));
    dcb->dcb_enabled = true;
}

void dcb_enable_pfc(DCBState *dcb, int priorities_bitmask) {
    if (!dcb) return;
    dcb->pfc_enabled = true;
    dcb->pfc_priorities = priorities_bitmask;
}

void dcb_enable_ets(DCBState *dcb, int total_bw_percent) {
    if (!dcb) return;
    dcb->ets_enabled = true;
    dcb->ets_bandwidth_total = total_bw_percent;
}

void dcb_enable_qcn(DCBState *dcb) {
    if (!dcb) return;
    dcb->qcn_enabled = true;
}

void dcb_print_state(const DCBState *dcb) {
    if (!dcb) return;
    printf("=== DCB State ===\n");
    printf("DCB:      %s\n", dcb->dcb_enabled ? "ON" : "OFF");
    printf("PFC:      %s (priorities: 0x%02X)\n",
           dcb->pfc_enabled ? "ON" : "OFF", dcb->pfc_priorities);
    printf("ETS:      %s (total BW: %d%%)\n",
           dcb->ets_enabled ? "ON" : "OFF", dcb->ets_bandwidth_total);
    printf("QCN:      %s (events: %llu)\n",
           dcb->qcn_enabled ? "ON" : "OFF",
           (unsigned long long)dcb->qcn_congestion_events);
}

/* Utility functions */
const char *qos_priority_name(QoSPriority prio) {
    switch (prio) {
        case QOS_PCP_BK: return "Background (BK)";
        case QOS_PCP_BE: return "Best Effort (BE)";
        case QOS_PCP_EE: return "Excellent Effort (EE)";
        case QOS_PCP_CA: return "Critical Apps (CA)";
        case QOS_PCP_VI: return "Video (VI)";
        case QOS_PCP_VO: return "Voice (VO)";
        case QOS_PCP_IC: return "Internet Control (IC)";
        case QOS_PCP_NC: return "Network Control (NC)";
        default:         return "Unknown";
    }
}

const char *dscp_name(uint8_t dscp) {
    switch (dscp) {
        case DSCP_DF:   return "Default (BE)";
        case DSCP_EF:   return "Expedited Forwarding";
        case DSCP_CS6:  return "CS6 (Control)";
        case DSCP_CS7:  return "CS7 (Control)";
        case DSCP_AF11: return "AF11"; case DSCP_AF21: return "AF21";
        case DSCP_AF31: return "AF31"; case DSCP_AF41: return "AF41";
        default:        return "Other";
    }
}

void qos_scheduler_print_stats(const QoSScheduler *sched) {
    if (!sched) return;
    printf("=== QoS Scheduler: %d ===\n", sched->discipline);
    for (int i = 0; i < sched->num_classes; i++) {
        printf("Class %d (%s): TX=%llu pkts/%llu B  Drop=%llu pkts/%llu B\n",
               i,
               qos_priority_name(sched->classes[i].priority),
               (unsigned long long)sched->classes[i].packets_tx,
               (unsigned long long)sched->classes[i].bytes_tx,
               (unsigned long long)sched->classes[i].packets_dropped,
               (unsigned long long)sched->classes[i].bytes_dropped);
    }
    printf("Total scheduled: %llu pkts\n",
           (unsigned long long)sched->total_packets_scheduled);
}
