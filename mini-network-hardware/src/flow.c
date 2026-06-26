#include "flow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 802.3x PAUSE frame - MAC-layer flow control.
 * At 10Gbps: 1 quanta = 512bits / 10e9bps = 51.2ns
 * Max pause: 65535 * 51.2ns = 3.355ms
 * Buffer needed for lossless: B >= RTT * C
 * L4: Buffering requirement = bandwidth-delay product */
__attribute__((unused))
static double bandwidth_delay_product(double rate_gbps, double rtt_us) {
    return (rate_gbps * 1e9 / 8.0) * (rtt_us * 1e-6);
}

/* Convert buffer deficit in bytes to PAUSE quanta.
 * 1 quanta = 512 bit times = 64 bytes at any speed.
 * Used for PAUSE frame tuning calculations. */
__attribute__((unused))
static uint16_t bytes_to_quanta(double deficit_bytes) {
    double quanta = (deficit_bytes * 8.0) / 512.0;
    if (quanta < 0.0) return 0;
    if (quanta > (double)FLOW_PAUSE_QUANTA_MAX) return FLOW_PAUSE_QUANTA_MAX;
    return (uint16_t)quanta;
}

void pause_frame_build(PauseFrame *pf, const uint8_t *src_mac,
                       uint16_t quanta) {
    if (!pf || !src_mac) return;
    memset(pf, 0, sizeof(*pf));
    pf->dst_mac[0] = 0x01; pf->dst_mac[1] = 0x80;
    pf->dst_mac[2] = 0xC2; pf->dst_mac[3] = 0x00;
    pf->dst_mac[4] = 0x00; pf->dst_mac[5] = 0x01;
    memcpy(pf->src_mac, src_mac, MAC_ADDR_LEN);
    pf->ethertype = PAUSE_OPCODE;
    pf->opcode = 0x0001;
    pf->pause_quanta = quanta;
}

int pause_frame_parse(const uint8_t *raw, int len, PauseFrame *pf) {
    if (!raw || !pf || len < 64) return -1;
    memcpy(pf->dst_mac, raw, MAC_ADDR_LEN);
    memcpy(pf->src_mac, raw + 6, MAC_ADDR_LEN);
    pf->ethertype = (uint16_t)((raw[12] << 8) | raw[13]);
    pf->opcode    = (uint16_t)((raw[14] << 8) | raw[15]);
    pf->pause_quanta = (uint16_t)((raw[16] << 8) | raw[17]);
    if (pf->ethertype != PAUSE_OPCODE || pf->opcode != 0x0001) return -1;
    return 0;
}

void pause_frame_print(const PauseFrame *pf) {
    if (!pf) return;
    printf("=== 802.3x PAUSE Frame ===\n");
    printf("Dst MAC:    %02x:%02x:%02x:%02x:%02x:%02x\n",
           pf->dst_mac[0], pf->dst_mac[1], pf->dst_mac[2],
           pf->dst_mac[3], pf->dst_mac[4], pf->dst_mac[5]);
    printf("Src MAC:    %02x:%02x:%02x:%02x:%02x:%02x\n",
           pf->src_mac[0], pf->src_mac[1], pf->src_mac[2],
           pf->src_mac[3], pf->src_mac[4], pf->src_mac[5]);
    printf("Quanta:     %u (%.2f us at 10G)\n",
           pf->pause_quanta, pf->pause_quanta * 0.0512);
}

/* Priority Flow Control (PFC) - IEEE 802.1Qbb */
void pfc_init(PFCState *pfc) {
    if (!pfc) return;
    memset(pfc, 0, sizeof(*pfc));
}

void pfc_set_pause(PFCState *pfc, int priority, uint16_t quanta) {
    if (!pfc || priority < 0 || priority >= FLOW_PFC_PRIORITIES) return;
    pfc->pause_quanta[priority] = quanta;
    pfc->enabled[priority] = true;
    pfc->pause_frames_tx[priority]++;
}

void pfc_clear_pause(PFCState *pfc, int priority) {
    if (!pfc || priority < 0 || priority >= FLOW_PFC_PRIORITIES) return;
    pfc->pause_quanta[priority] = 0;
    pfc->enabled[priority] = false;
}

bool pfc_is_paused(const PFCState *pfc, int priority) {
    if (!pfc || priority < 0 || priority >= FLOW_PFC_PRIORITIES) return false;
    return pfc->enabled[priority];
}

void pfc_print_state(const PFCState *pfc) {
    if (!pfc) return;
    printf("=== PFC State ===\n");
    for (int i = 0; i < FLOW_PFC_PRIORITIES; i++)
        printf("P%d: %s quanta=%u tx=%llu\n", i,
               pfc->enabled[i] ? "ON " : "OFF",
               pfc->pause_quanta[i],
               (unsigned long long)pfc->pause_frames_tx[i]);
}

/* Token Bucket (RFC 2697)
 * Long-term rate <= r, max burst = b */
void token_bucket_init(TokenBucket *tb, double rate, double max_bucket) {
    if (!tb) return;
    memset(tb, 0, sizeof(*tb));
    tb->rate = rate;
    tb->max_bucket = max_bucket;
    tb->current_tokens = max_bucket;
}

void token_bucket_update(TokenBucket *tb, uint64_t now_ns) {
    if (!tb || now_ns < tb->last_update_ns) return;
    double elapsed = (double)(now_ns - tb->last_update_ns) * 1e-9;
    double added = elapsed * tb->rate;
    tb->current_tokens += added;
    if (tb->current_tokens > tb->max_bucket)
        tb->current_tokens = tb->max_bucket;
    tb->last_update_ns = now_ns;
}

bool token_bucket_consume(TokenBucket *tb, double bytes, uint64_t now_ns) {
    if (!tb || bytes <= 0.0) return false;
    token_bucket_update(tb, now_ns);
    if (tb->current_tokens >= bytes) {
        tb->current_tokens -= bytes;
        tb->packets_passed++;
        tb->bytes_passed += (uint64_t)bytes;
        return true;
    }
    tb->packets_dropped++;
    tb->bytes_dropped += (uint64_t)bytes;
    return false;
}

void token_bucket_print_stats(const TokenBucket *tb) {
    if (!tb) return;
    printf("=== Token Bucket ===\n");
    printf("Rate: %.2f B/s  Max: %.2f B  Current: %.2f B\n",
           tb->rate, tb->max_bucket, tb->current_tokens);
    printf("Passed: %llu pkts (%llu B)  Dropped: %llu pkts (%llu B)\n",
           (unsigned long long)tb->packets_passed,
           (unsigned long long)tb->bytes_passed,
           (unsigned long long)tb->packets_dropped,
           (unsigned long long)tb->bytes_dropped);
}

/* Leaky Bucket - traffic shaping with constant output rate
 * Q(t+dt) = max(0, Q(t) + arrivals - C*dt) */
void leaky_bucket_init(LeakyBucket *lb, double rate, double max_queue) {
    if (!lb) return;
    memset(lb, 0, sizeof(*lb));
    lb->output_rate = rate;
    lb->max_queue = max_queue;
}

bool leaky_bucket_enqueue(LeakyBucket *lb, double bytes, uint64_t now_ns) {
    if (!lb || bytes <= 0.0) return false;
    if (now_ns >= lb->last_update_ns) {
        double elapsed = (double)(now_ns - lb->last_update_ns) * 1e-9;
        double drained = elapsed * lb->output_rate;
        lb->current_queue -= drained;
        if (lb->current_queue < 0.0) lb->current_queue = 0.0;
    }
    lb->last_update_ns = now_ns;
    if (lb->current_queue + bytes <= lb->max_queue) {
        lb->current_queue += bytes;
        lb->packets_shaped++;
        return true;
    }
    lb->packets_dropped++;
    return false;
}

double leaky_bucket_dequeue(LeakyBucket *lb, uint64_t now_ns) {
    if (!lb) return 0.0;
    if (now_ns >= lb->last_update_ns) {
        double elapsed = (double)(now_ns - lb->last_update_ns) * 1e-9;
        double drained = elapsed * lb->output_rate;
        if (drained > lb->current_queue) drained = lb->current_queue;
        lb->current_queue -= drained;
        lb->last_update_ns = now_ns;
    }
    return lb->current_queue;
}

void leaky_bucket_print_stats(const LeakyBucket *lb) {
    if (!lb) return;
    printf("=== Leaky Bucket ===\n");
    printf("Rate: %.2f B/s  Max: %.2f B  Current: %.2f B\n",
           lb->output_rate, lb->max_queue, lb->current_queue);
    printf("Shaped: %llu pkts  Dropped: %llu pkts\n",
           (unsigned long long)lb->packets_shaped,
           (unsigned long long)lb->packets_dropped);
}

/* Congestion Control with RED (Random Early Detection)
 * ECN marks instead of dropping (RFC 3168) */
void congestion_init(CongestionControl *cc) {
    if (!cc) return;
    memset(cc, 0, sizeof(*cc));
    cc->ecn_enabled = true;
    cc->marking_threshold = 0.5;
    cc->drop_threshold = 0.9;
}

bool congestion_check(CongestionControl *cc, double queue_fill,
                       uint64_t *action) {
    if (!cc || !action) return false;
    if (queue_fill < cc->marking_threshold) {
        *action = 0; return true;
    } else if (queue_fill < cc->drop_threshold) {
        *action = 1; cc->ecn_marked++; return true;
    }
    *action = 2; cc->packets_dropped_cong++; return false;
}

/* Little's Law: L = lambda * W (John Little, 1961)
 * Universal queueing theorem - applies to ANY stable system */
double littles_law_queue_length(double arrival_rate, double wait_time) {
    return arrival_rate * wait_time;
}

double littles_law_wait_time(double queue_length, double arrival_rate) {
    if (arrival_rate <= 0.0) return 0.0;
    return queue_length / arrival_rate;
}

void flow_stats_init(FlowStats *stats) {
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
}

void flow_stats_print(const FlowStats *stats) {
    if (!stats) return;
    printf("=== Flow Stats ===\n");
    printf("PAUSE TX: %llu  RX: %llu\n",
           (unsigned long long)stats->pause_frames_tx,
           (unsigned long long)stats->pause_frames_rx);
    printf("Avg queue: %.2f B  Max queue: %.2f B\n",
           stats->avg_queue_depth, stats->max_queue_depth);
}
