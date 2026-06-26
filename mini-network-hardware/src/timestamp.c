#include "timestamp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* PTP Timestamp operations.
 * Normalize: ensures nanoseconds in [0, 999999999] range.
 * This is essential because PTP uses split 48-bit seconds
 * and 32-bit nanoseconds fields. */
void ptp_timestamp_set(PTPTimestamp *ts, uint64_t sec, uint32_t ns) {
    if (!ts) return;
    ts->seconds = sec;
    ts->nanoseconds = ns;
    ptp_timestamp_normalize(ts);
}

void ptp_timestamp_normalize(PTPTimestamp *ts) {
    if (!ts) return;
    if (ts->nanoseconds >= 1000000000UL) {
        ts->seconds += ts->nanoseconds / 1000000000UL;
        ts->nanoseconds %= 1000000000UL;
    }
}

/* Difference: (a - b) in nanoseconds, signed.
 * Handles wrap-around within reasonable bounds.
 * L5: This is the core arithmetic for PTP delay calculation. */
int64_t ptp_timestamp_diff_ns(const PTPTimestamp *a, const PTPTimestamp *b) {
    if (!a || !b) return 0;
    int64_t sec_diff = (int64_t)(a->seconds - b->seconds);
    int64_t ns_diff  = (int64_t)a->nanoseconds - (int64_t)b->nanoseconds;
    return sec_diff * 1000000000LL + ns_diff;
}

void ptp_timestamp_print(const PTPTimestamp *ts, const char *label) {
    if (!ts) return;
    printf("%s: %llu.%09u s\n", label ? label : "TS",
           (unsigned long long)ts->seconds, ts->nanoseconds);
}

/* PTP Clock initialization and operations.
 * L2: A PTP clock can be Grandmaster (GM), Boundary Clock (BC),
 * or Ordinary Clock (OC). The Best Master Clock Algorithm (BMCA)
 * determines which clock becomes the GM. */
void ptp_clock_init(PTPClock *clock, const uint8_t *clock_id) {
    if (!clock) return;
    memset(clock, 0, sizeof(*clock));
    if (clock_id) memcpy(clock->clock_id.id, clock_id, 8);
    clock->domain_number = PTP_DOMAIN_DEFAULT;
    clock->priority1 = PTP_PRIORITY1_DEFAULT;
    clock->priority2 = PTP_PRIORITY2_DEFAULT;
    clock->port_state = PTP_PORT_LISTENING;
    clock->clock_quality.clock_class = 248; /* Default */
    clock->clock_quality.clock_accuracy = 0xFE; /* Unknown */
    clock->frequency_adjustment = 0.0;
}

void ptp_clock_set_time(PTPClock *clock, uint64_t sec, uint32_t ns) {
    if (!clock) return;
    ptp_timestamp_set(&clock->current_time, sec, ns);
}

/* Frequency adjustment in parts-per-million (ppm).
 * Positive = speed up, negative = slow down.
 * Real PTP clocks use a frequency-locked loop (FLL)
 * or phase-locked loop (PLL) for disciplining.
 * L8: Hardware PTP clocks achieve sub-ns accuracy
 * by timestamping at the PHY level. */
void ptp_clock_adjust_freq(PTPClock *clock, double ppm) {
    if (!clock) return;
    clock->frequency_adjustment = ppm;
}

/* Advance clock by elapsed nanoseconds, applying frequency correction.
 * L5: The clock is disciplined by a PI (Proportional-Integral)
 * controller that adjusts frequency to minimize offset. */
void ptp_clock_step(PTPClock *clock, uint64_t elapsed_ns) {
    if (!clock || elapsed_ns == 0) return;
    double adjusted_ns = (double)elapsed_ns *
        (1.0 + clock->frequency_adjustment * 1e-6);
    uint64_t ns_to_add = (uint64_t)adjusted_ns;
    clock->current_time.nanoseconds += (uint32_t)(ns_to_add % 1000000000UL);
    clock->current_time.seconds += ns_to_add / 1000000000UL;
    ptp_timestamp_normalize(&clock->current_time);
}

/* PTP Delay Measurement
 * L5 - Two-step clock synchronization algorithm:
 *
 * Master  ----[t1: Sync]---->  Slave
 * Master  <---[t3: DelayReq]-- Slave
 * Master  ----[t4: DelayResp]-> Slave
 *
 * Follow_Up message carries t1 (for two-step clocks).
 *
 * L4 - The fundamental PTP equations:
 *   offset = ((t2 - t1) - (t4 - t3)) / 2
 *   delay  = ((t2 - t1) + (t4 - t3)) / 2
 *
 * Assumption: symmetric path delay (forward == reverse).
 * This is the key limitation of PTP - asymmetric delays
 * cause systematic offset errors.
 *
 * Reference: IEEE 1588-2008, Section 11.2
 */
void ptp_delay_measurement_init(PTPDelayMeasurement *dm) {
    if (!dm) return;
    memset(dm, 0, sizeof(*dm));
    dm->valid = false;
}

int ptp_calculate_offset(PTPDelayMeasurement *dm,
                          double *offset_ns, double *delay_ns) {
    if (!dm || !offset_ns || !delay_ns) return -1;
    if (!dm->valid) return -1;

    int64_t t2_minus_t1 = ptp_timestamp_diff_ns(&dm->t2, &dm->t1);
    int64_t t4_minus_t3 = ptp_timestamp_diff_ns(&dm->t4, &dm->t3);

    *offset_ns = (double)(t2_minus_t1 - t4_minus_t3) / 2.0;
    *delay_ns  = (double)(t2_minus_t1 + t4_minus_t3) / 2.0;

    return 0;
}

/* Hardware timestamp capture.
 * L3: In modern NICs, timestamps are captured at the MAC-PHY
 * interface when the Start Frame Delimiter (SFD) is detected.
 * This eliminates OS and software stack jitter.
 *
 * Accuracy: typically < 100 ns with hardware timestamping,
 * vs. 1-100 ms with software timestamping.
 */
void hw_timestamp_init(HWTimestampCapture *hwts) {
    if (!hwts) return;
    memset(hwts, 0, sizeof(*hwts));
}

void hw_timestamp_capture_tx(HWTimestampCapture *hwts,
                             const PTPTimestamp *ts, uint64_t seq) {
    if (!hwts || !ts) return;
    hwts->tx_timestamp = *ts;
    hwts->tx_sequence_id = seq;
    hwts->tx_valid = true;
}

void hw_timestamp_capture_rx(HWTimestampCapture *hwts,
                             const PTPTimestamp *ts, uint64_t seq) {
    if (!hwts || !ts) return;
    hwts->rx_timestamp = *ts;
    hwts->rx_sequence_id = seq;
    hwts->rx_valid = true;
}

void hw_timestamp_print(const HWTimestampCapture *hwts) {
    if (!hwts) return;
    printf("=== HW Timestamp Capture ===\n");
    printf("TX: seq=%llu valid=%d\n",
           (unsigned long long)hwts->tx_sequence_id,
           hwts->tx_valid);
    if (hwts->tx_valid)
        ptp_timestamp_print(&hwts->tx_timestamp, "  TX");
    printf("RX: seq=%llu valid=%d\n",
           (unsigned long long)hwts->rx_sequence_id,
           hwts->rx_valid);
    if (hwts->rx_valid)
        ptp_timestamp_print(&hwts->rx_timestamp, "  RX");
}

/* TSN Time-Aware Shaper (IEEE 802.1Qbv)
 * L7: TSN enables deterministic Ethernet for industrial
 * control, automotive, and 5G fronthaul.
 *
 * The gate control list (GCL) opens/closes queues on a
 * cyclic schedule with nanosecond precision.
 *
 * Example: 802.1Qbv cycle = 100 us
 *   Queue 0 (high priority): open [0, 20us]
 *   Queue 1 (best effort):   open [20us, 100us]
 *   Guard band before cycle end for frame completion.
 */
void tsn_gate_init(TSNGateControl *gate, uint64_t cycle_ns) {
    if (!gate) return;
    memset(gate, 0, sizeof(*gate));
    gate->cycle_time_ns = cycle_ns;
    gate->time_remaining_ns = cycle_ns;
    gate->gate_state = 0xFF; /* All queues open initially */
}

bool tsn_gate_is_open(const TSNGateControl *gate, int queue) {
    if (!gate || queue < 0 || queue > 7) return false;
    return (gate->gate_state >> queue) & 1;
}

void tsn_gate_advance(TSNGateControl *gate, uint64_t elapsed_ns) {
    if (!gate) return;
    if (elapsed_ns >= gate->time_remaining_ns) {
        /* Cycle complete - reset or load next GCL entry */
        gate->time_remaining_ns = gate->cycle_time_ns;
    } else {
        gate->time_remaining_ns -= elapsed_ns;
    }
}
