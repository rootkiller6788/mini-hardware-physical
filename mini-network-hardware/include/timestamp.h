#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#include <stdbool.h>
#include <stdint.h>

/* ================================================================
 * Hardware Timestamping (IEEE 1588 PTP / IEEE 802.1AS)
 *
 * L1: Timestamp format, PTP message types
 * L2: Hardware timestamp capture in NIC
 * L3: PTP clock synchronization pipeline
 * L4: IEEE 1588 Precision Time Protocol
 * L5: Clock offset/delay calculation algorithms
 * L7: Time-sensitive networking (TSN), 5G fronthaul
 * ================================================================ */

#define PTP_DOMAIN_DEFAULT  0
#define PTP_PRIORITY1_DEFAULT 128
#define PTP_PRIORITY2_DEFAULT 128

/* PTP Clock Identity (8 bytes, typically EUI-64) */
typedef struct {
    uint8_t id[8];
} PTPClockIdentity;

/* PTP Timescale: 48-bit seconds + 32-bit nanoseconds
 * Epoch: PTP epoch (1970-01-01 00:00:00 TAI) */
typedef struct {
    uint64_t seconds;      /* 48-bit seconds field */
    uint32_t nanoseconds;  /* 0-999,999,999 */
} PTPTimestamp;

/* PTP Message Types */
typedef enum {
    PTP_SYNC              = 0,
    PTP_DELAY_REQ         = 1,
    PTP_DELAY_RESP        = 2,
    PTP_FOLLOW_UP         = 8,
    PTP_DELAY_RESP_FOLLOW = 9,
    PTP_ANNOUNCE          = 11
} PTPMessageType;

/* PTP Port State (Best Master Clock Algorithm states) */
typedef enum {
    PTP_PORT_INITIALIZING = 1,
    PTP_PORT_FAULTY       = 2,
    PTP_PORT_DISABLED     = 3,
    PTP_PORT_LISTENING    = 4,
    PTP_PORT_PRE_MASTER   = 5,
    PTP_PORT_MASTER       = 6,
    PTP_PORT_PASSIVE      = 7,
    PTP_PORT_UNCALIBRATED = 8,
    PTP_PORT_SLAVE        = 9
} PTPPortState;

/* PTP Clock Quality */
typedef struct {
    uint8_t  clock_class;
    uint8_t  clock_accuracy;   /* 0-255, lower = better */
    uint16_t offset_scaled_log_variance;
} PTPClockQuality;

/* PTP Message Header (simplified, 34 bytes) */
typedef struct {
    uint8_t  transport_specific:4;
    uint8_t  message_type:4;
    uint8_t  version_ptp;       /* 2 for IEEE 1588-2008 */
    uint16_t message_length;
    uint8_t  domain_number;
    uint8_t  flags[2];
    int64_t  correction_field;  /* nanoseconds * 2^16 */
    PTPClockIdentity source_port_id;
    uint16_t sequence_id;
    uint8_t  control_field;
    int8_t   log_message_interval;
} PTPHeader;

/* PTP Sync/Follow_Up message */
typedef struct {
    PTPHeader    header;
    PTPTimestamp origin_timestamp;
} PTPSyncMessage;

/* PTP Delay Request/Response */
typedef struct {
    PTPHeader    header;
    PTPTimestamp origin_timestamp;
    PTPClockIdentity requesting_port_id;
} PTPDelayMessage;

/* Hardware Timestamp Capture in NIC
 * Timestamps are captured at the MAC-PHY interface
 * for maximum accuracy (sub-nanosecond possible). */
typedef struct {
    PTPTimestamp tx_timestamp;
    PTPTimestamp rx_timestamp;
    bool         tx_valid;
    bool         rx_valid;
    uint64_t     tx_sequence_id;
    uint64_t     rx_sequence_id;
} HWTimestampCapture;

/* PTP Clock - software model of a PTP hardware clock (PHC) */
typedef struct {
    PTPClockIdentity clock_id;
    PTPTimestamp      current_time;
    double            frequency_adjustment; /* ppm offset from nominal */
    double            mean_path_delay;       /* nanoseconds */
    double            offset_from_master;    /* nanoseconds */
    PTPPortState      port_state;
    PTPClockQuality   clock_quality;
    uint8_t           domain_number;
    uint16_t          priority1;
    uint16_t          priority2;
    uint64_t          sync_count;
    uint64_t          delay_req_count;
} PTPClock;

/* Two-step clock: timestamps sent in Follow_Up messages */
typedef struct {
    PTPTimestamp t1;  /* Master: Sync departure time */
    PTPTimestamp t2;  /* Slave:  Sync arrival time */
    PTPTimestamp t3;  /* Slave:  Delay_Req departure time */
    PTPTimestamp t4;  /* Master: Delay_Req arrival time */
    bool         valid;
} PTPDelayMeasurement;

/* API */
/* Timestamp operations */
void      ptp_timestamp_set(PTPTimestamp *ts, uint64_t sec, uint32_t ns);
void      ptp_timestamp_normalize(PTPTimestamp *ts);
int64_t   ptp_timestamp_diff_ns(const PTPTimestamp *a, const PTPTimestamp *b);
void      ptp_timestamp_print(const PTPTimestamp *ts, const char *label);

/* PTP Clock */
void      ptp_clock_init(PTPClock *clock, const uint8_t *clock_id);
void      ptp_clock_set_time(PTPClock *clock, uint64_t sec, uint32_t ns);
void      ptp_clock_adjust_freq(PTPClock *clock, double ppm);
void      ptp_clock_step(PTPClock *clock, uint64_t elapsed_ns);

/* PTP Delay Measurement (L5 Algorithm) */
void      ptp_delay_measurement_init(PTPDelayMeasurement *dm);
int       ptp_calculate_offset(PTPDelayMeasurement *dm,
                               double *offset_ns, double *delay_ns);
/* L4: offset = ((t2 - t1) - (t4 - t3)) / 2
 *      delay  = ((t2 - t1) + (t4 - t3)) / 2 */

/* Hardware Timestamping */
void      hw_timestamp_init(HWTimestampCapture *hwts);
void      hw_timestamp_capture_tx(HWTimestampCapture *hwts,
                                  const PTPTimestamp *ts, uint64_t seq);
void      hw_timestamp_capture_rx(HWTimestampCapture *hwts,
                                  const PTPTimestamp *ts, uint64_t seq);
void      hw_timestamp_print(const HWTimestampCapture *hwts);

/* TSN Time-aware Shaper (802.1Qbv) - gate control */
typedef struct {
    uint8_t  gate_state;      /* Bitmask of open queues */
    uint64_t cycle_time_ns;
    uint64_t time_remaining_ns;
} TSNGateControl;

void      tsn_gate_init(TSNGateControl *gate, uint64_t cycle_ns);
bool      tsn_gate_is_open(const TSNGateControl *gate, int queue);
void      tsn_gate_advance(TSNGateControl *gate, uint64_t elapsed_ns);

#endif /* TIMESTAMP_H */
