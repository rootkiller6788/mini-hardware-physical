#ifndef IO_PROFILE_H
#define IO_PROFILE_H

/**
 * io_profile.h — I/O Subsystem Profiling for Data Lake Workloads
 *
 * L3: Engineering Structure — I/O profiling models the complete I/O stack
 *     from application-level block reads through OS page cache, filesystem,
 *     block layer, device driver, to NVMe/SSD hardware. Data lake systems
 *     (Apache Iceberg, Delta Lake, Hudi) rely heavily on efficient I/O for
 *     Parquet/ORC file access.
 *
 * L5: Algorithm — Queue-depth scaling analysis. NVMe drives expose multiple
 *     hardware queues; increasing queue depth yields diminishing returns that
 *     follow a saturation curve: IOPS = IOPS_max * (1 - e^(-k*qd)).
 *
 * L6: Canonical Problem — I/O scheduler simulation for data lake queries:
 *     merge-on-read, copy-on-write, time-travel queries.
 *
 * Universities: CMU 18-746, Stanford CS240, MIT 6.5830
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lake_test_core.h"

/* ============================================================================
 * L1: I/O Profiling Types
 * ============================================================================ */

/** I/O operation type */
typedef enum {
    IO_OP_READ = 0,
    IO_OP_WRITE = 1,
    IO_OP_FLUSH = 2,
    IO_OP_TRIM = 3
} IoOpType;

/** I/O queue depth test point */
typedef struct {
    uint32_t queue_depth;
    uint64_t total_iops;
    uint64_t read_iops;
    uint64_t write_iops;
    double   avg_latency_us;
    double   p99_latency_us;
    double   throughput_mbps;
    double   cpu_utilization;  /* fraction 0.0-1.0 */
} IoQueueDepthPoint;

/** I/O benchmark result */
typedef struct {
    IoQueueDepthPoint *points;
    size_t             num_points;
    uint32_t           optimal_queue_depth;
    uint64_t           peak_iops;
    double             peak_throughput_mbps;
    double             min_latency_us;
    double             saturation_point;  /* Queue depth at which IOPS saturates */
} IoBenchResult;

/* ============================================================================
 * L2: Data Lake I/O Pattern Simulator
 *
 * Simulates I/O patterns specific to data lake formats:
 * - Parquet: column chunks (strided reads within row groups)
 * - ORC: stripe-based reads with indexes
 * - Iceberg: manifest file reads + data file scans
 * - Delta Lake: transaction log replay + Parquet data files
 * ============================================================================ */

/** Data lake file format for I/O simulation */
typedef enum {
    LAKE_FORMAT_PARQUET = 0,
    LAKE_FORMAT_ORC = 1,
    LAKE_FORMAT_AVRO = 2,
    LAKE_FORMAT_ICEBERG = 3,
    LAKE_FORMAT_DELTA = 4,
    LAKE_FORMAT_HUDI = 5
} LakeFileFormat;

/** Simulated I/O request for a lakehouse query */
typedef struct {
    uint64_t        file_id;
    uint64_t        offset;
    uint64_t        length;
    IoOpType        op_type;
    double          timestamp_ms;
    uint32_t        priority;
    LakeFileFormat  format;
} IoRequest;

/** I/O scheduler algorithm (L5) */
typedef enum {
    IO_SCHED_NOOP = 0,       /* FCFS — no reordering */
    IO_SCHED_DEADLINE = 1,   /* Deadline-based (read deadline < write deadline) */
    IO_SCHED_CFQ = 2,        /* Completely Fair Queuing */
    IO_SCHED_MQ_DEADLINE = 3 /* Multi-queue deadline (default for NVMe) */
} IoSchedulerType;

/** I/O scheduler state */
typedef struct {
    IoSchedulerType type;
    IoRequest       *pending;
    size_t           pending_count;
    size_t           pending_capacity;
    uint64_t         total_io_completed;
    uint64_t         total_bytes_read;
    uint64_t         total_bytes_written;
    double           avg_queue_latency_us;
    double           total_busy_time_us;
} IoScheduler;

/** Data lake I/O trace replay statistics */
typedef struct {
    uint64_t        total_requests;
    uint64_t        total_bytes;
    uint64_t        sequential_requests;  /* merged into sequential runs */
    uint64_t        random_requests;
    double          avg_request_size;
    double          avg_gap_between_requests;  /* spatial locality metric */
    double          sequential_ratio;
    double          reuse_distance_avg;   /* temporal locality metric */
} IOTraceStats;

/* ============================================================================
 * L6: NVMe Queue Pair Simulation
 *
 * Models NVMe hardware queue behavior: submission queue (SQ) entries,
 * completion queue (CQ) entries, and the doorbell register mechanism.
 * Reference: NVM Express Base Specification 2.0.
 * ============================================================================ */

/** NVMe submission queue entry (simplified) */
typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t command_id;
    uint32_t namespace_id;
    uint64_t start_lba;
    uint16_t num_blocks;
    uint64_t data_pointer;  /* simulated */
} NvmeSQEntry;

/** NVMe completion queue entry (simplified) */
typedef struct {
    uint16_t command_id;
    uint16_t status;
    uint32_t result;
} NvmeCQEntry;

/** Simulated NVMe queue pair */
typedef struct {
    NvmeSQEntry *sq;
    NvmeCQEntry *cq;
    uint16_t     sq_head;
    uint16_t     sq_tail;
    uint16_t     cq_head;
    uint16_t     cq_tail;
    uint16_t     queue_size;
    double       avg_service_time_us;
    uint64_t     total_commands_processed;
    uint64_t     total_errors;
} NvmeQueuePair;

/* ============================================================================
 * L1: API Declarations
 * ============================================================================ */

/** Initialize I/O benchmark with defaults */
void io_bench_result_init(IoBenchResult *result);

/** 
 * L5: Run I/O queue depth scaling test.
 *     Measures IOPS vs. queue depth and fits saturation curve.
 */
IoBenchResult *io_run_queue_depth_test(uint32_t max_qd, uint64_t block_size,
                                        IoAccessPattern pattern, uint64_t total_bytes);

/** Free I/O benchmark result */
void io_bench_result_destroy(IoBenchResult *result);

/** Print I/O benchmark results */
void io_bench_result_print(const IoBenchResult *result);

/** Initialize I/O scheduler */
void io_scheduler_init(IoScheduler *sched, IoSchedulerType type);

/** Submit an I/O request to the scheduler */
bool io_scheduler_submit(IoScheduler *sched, const IoRequest *req);

/** Process the next I/O request from the scheduler */
bool io_scheduler_dispatch(IoScheduler *sched, IoRequest *dispatched);

/** Get scheduler statistics */
void io_scheduler_stats(const IoScheduler *sched, uint64_t *completed,
                        double *avg_latency_us);

/** Free scheduler resources */
void io_scheduler_destroy(IoScheduler *sched);

/** Generate a synthetic data lake I/O trace */
IoRequest *io_generate_lake_trace(LakeFileFormat format, uint64_t data_size_bytes,
                                   uint64_t *num_requests_out);

/** Analyze I/O trace for spatial and temporal locality */
IOTraceStats io_analyze_trace(const IoRequest *trace, size_t num_requests);

/** Print I/O trace statistics */
void io_trace_stats_print(const IOTraceStats *stats);

/** Initialize a simulated NVMe queue pair */
void nvme_qp_init(NvmeQueuePair *qp, uint16_t queue_size);

/** Submit a command to the NVMe submission queue */
bool nvme_qp_submit(NvmeQueuePair *qp, const NvmeSQEntry *cmd);

/** Process completions from the NVMe completion queue */
uint32_t nvme_qp_process_completions(NvmeQueuePair *qp, NvmeCQEntry *results,
                                      uint32_t max_results);

/** Free NVMe queue pair resources */
void nvme_qp_destroy(NvmeQueuePair *qp);

/** Print NVMe queue pair status */
void nvme_qp_print(const NvmeQueuePair *qp);

#endif /* IO_PROFILE_H */