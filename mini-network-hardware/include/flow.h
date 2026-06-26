#ifndef FLOW_H
#define FLOW_H

#include <stdbool.h>
#include <stdint.h>
#include "mac.h"

/* ================================================================
 * Flow Control & Rate Limiting
 *
 * L1: Pause frames, token bucket, PFC
 * L2: 802.3x PAUSE, Priority Flow Control (802.1Qbb)
 * L3: Token bucket / Leaky bucket rate limiter
 * L4: Little's Law: L = lambda * W
 * L5: Token bucket algorithm, Priority Flow Control
 * L7: DCB (Data Center Bridging) for lossless Ethernet
 * ================================================================ */

#define FLOW_PAUSE_QUANTA_MAX 65535
#define FLOW_PFC_PRIORITIES   8
#define FLOW_TOKEN_BUCKET_MAX_RATE (100.0)  /* Gbps */

/* 802.3x PAUSE frame opcode */
#define PAUSE_OPCODE 0x8808
#define PAUSE_MAC_DST_MULTICAST {0x01,0x80,0xC2,0x00,0x00,0x01}

/* PFC: Priority Flow Control (802.1Qbb)
 * Each priority can be independently paused */
typedef struct {
    uint16_t pause_quanta[FLOW_PFC_PRIORITIES];
    bool     enabled[FLOW_PFC_PRIORITIES];
    uint64_t pause_frames_tx[FLOW_PFC_PRIORITIES];
    uint64_t pause_frames_rx[FLOW_PFC_PRIORITIES];
} PFCState;

/* Standard 802.3x PAUSE frame */
typedef struct {
    uint8_t  dst_mac[MAC_ADDR_LEN];
    uint8_t  src_mac[MAC_ADDR_LEN];
    uint16_t ethertype;        /* 0x8808 */
    uint16_t opcode;           /* 0x0001 for PAUSE */
    uint16_t pause_quanta;     /* 0-65535, in 512-bit-time units */
    uint8_t  padding[42];      /* Pad to 64-byte minimum */
} PauseFrame;

/* Token Bucket Rate Limiter
 * Controls average rate while allowing bursts up to bucket size.
 *
 * L5 Algorithm:
 *   Tokens are added at constant rate (r tokens/sec)
 *   Bucket has max capacity (b tokens)
 *   Packet of size S can be sent if bucket has >= S tokens
 *   On send: remove S tokens. If idle: accumulate up to b.
 *
 * L4 - Little's Law: L = lambda * W
 *   Average queue length = arrival_rate * average_wait_time
 */
typedef struct {
    double   rate;             /* Token fill rate (bytes/sec) */
    double   max_bucket;       /* Maximum tokens (bytes) */
    double   current_tokens;   /* Available tokens now */
    uint64_t last_update_ns;   /* Last token update timestamp */
    uint64_t packets_passed;
    uint64_t packets_dropped;
    uint64_t bytes_passed;
    uint64_t bytes_dropped;
} TokenBucket;

/* Leaky Bucket (traffic shaping variant)
 * Smoothes bursts by enforcing constant output rate */
typedef struct {
    double   output_rate;      /* Constant output rate (bytes/sec) */
    double   max_queue;        /* Maximum queue depth (bytes) */
    double   current_queue;    /* Current queued bytes */
    uint64_t last_update_ns;
    uint64_t packets_shaped;
    uint64_t packets_dropped;
} LeakyBucket;

/* Congestion notification (ECN-style) */
typedef struct {
    bool     ecn_enabled;
    double   marking_threshold; /* Queue fraction to start marking */
    double   drop_threshold;    /* Queue fraction to start dropping */
    uint64_t ecn_marked;
    uint64_t packets_dropped_cong;
} CongestionControl;

/* Flow control statistics */
typedef struct {
    uint64_t pause_frames_tx;
    uint64_t pause_frames_rx;
    double   avg_queue_depth;
    double   max_queue_depth;
    double   queue_build_rate;  /* bytes/sec */
} FlowStats;

/* API */
/* PAUSE frames */
void      pause_frame_build(PauseFrame *pf, const uint8_t *src_mac,
                            uint16_t quanta);
int       pause_frame_parse(const uint8_t *raw, int len, PauseFrame *pf);
void      pause_frame_print(const PauseFrame *pf);

/* PFC */
void      pfc_init(PFCState *pfc);
void      pfc_set_pause(PFCState *pfc, int priority, uint16_t quanta);
void      pfc_clear_pause(PFCState *pfc, int priority);
bool      pfc_is_paused(const PFCState *pfc, int priority);
void      pfc_print_state(const PFCState *pfc);

/* Token Bucket */
void      token_bucket_init(TokenBucket *tb, double rate, double max_bucket);
bool      token_bucket_consume(TokenBucket *tb, double bytes, uint64_t now_ns);
void      token_bucket_update(TokenBucket *tb, uint64_t now_ns);
void      token_bucket_print_stats(const TokenBucket *tb);

/* Leaky Bucket */
void      leaky_bucket_init(LeakyBucket *lb, double rate, double max_queue);
bool      leaky_bucket_enqueue(LeakyBucket *lb, double bytes, uint64_t now_ns);
double    leaky_bucket_dequeue(LeakyBucket *lb, uint64_t now_ns);
void      leaky_bucket_print_stats(const LeakyBucket *lb);

/* Congestion Control */
void      congestion_init(CongestionControl *cc);
bool      congestion_check(CongestionControl *cc, double queue_fill,
                           uint64_t *action);

/* Little's Law verification */
double    littles_law_queue_length(double arrival_rate, double wait_time);
double    littles_law_wait_time(double queue_length, double arrival_rate);

/* Flow Stats */
void      flow_stats_init(FlowStats *stats);
void      flow_stats_print(const FlowStats *stats);

#endif /* FLOW_H */
