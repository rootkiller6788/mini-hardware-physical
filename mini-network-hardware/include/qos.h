#ifndef QOS_H
#define QOS_H

#include <stdbool.h>
#include <stdint.h>

/* ================================================================
 * Quality of Service (QoS) & Data Center Bridging (DCB)
 *
 * L1: QoS classes, scheduling policies, ETS
 * L2: Traffic classification, DiffServ, 802.1p
 * L3: WRR/DRR/SP scheduling, ETS bandwidth allocation
 * L4: 802.1Qbb (PFC), 802.1Qaz (ETS), 802.1Qau (QCN)
 * L5: Deficit Round Robin (DRR), Strict Priority + WRR
 * L7: Data Center Bridging for lossless Ethernet
 * ================================================================ */

#define QOS_MAX_CLASSES      8
#define QOS_MAX_QUEUES       8
#define QOS_DEFAULT_MTU      1500

/* Traffic class (802.1p priority code point) */
typedef enum {
    QOS_PCP_BK = 1,  /* Background */
    QOS_PCP_BE = 0,  /* Best Effort */
    QOS_PCP_EE = 2,  /* Excellent Effort */
    QOS_PCP_CA = 3,  /* Critical Applications */
    QOS_PCP_VI = 4,  /* Video (< 100ms latency) */
    QOS_PCP_VO = 5,  /* Voice (< 10ms latency) */
    QOS_PCP_IC = 6,  /* Internetwork Control */
    QOS_PCP_NC = 7   /* Network Control */
} QoSPriority;

/* DiffServ DSCP (Differentiated Services Code Point)
 * 6 bits in IP header TOS field */
typedef enum {
    DSCP_DF  = 0x00,  /* Default Forwarding (BE) */
    DSCP_CS1 = 0x08,  /* Class Selector 1 */
    DSCP_AF11= 0x0A,  /* Assured Forwarding 1,1 */
    DSCP_AF12= 0x0C,  /* Assured Forwarding 1,2 */
    DSCP_AF13= 0x0E,  /* Assured Forwarding 1,3 */
    DSCP_CS2 = 0x10,  /* Class Selector 2 */
    DSCP_AF21= 0x12,  /* Assured Forwarding 2,1 */
    DSCP_AF22= 0x14,  /* Assured Forwarding 2,2 */
    DSCP_AF23= 0x16,  /* Assured Forwarding 2,3 */
    DSCP_CS3 = 0x18,  /* Class Selector 3 */
    DSCP_AF31= 0x1A,  /* Assured Forwarding 3,1 */
    DSCP_AF32= 0x1C,  /* Assured Forwarding 3,2 */
    DSCP_AF33= 0x1E,  /* Assured Forwarding 3,3 */
    DSCP_CS4 = 0x20,  /* Class Selector 4 */
    DSCP_AF41= 0x22,  /* Assured Forwarding 4,1 */
    DSCP_AF42= 0x24,  /* Assured Forwarding 4,2 */
    DSCP_AF43= 0x26,  /* Assured Forwarding 4,3 */
    DSCP_CS5 = 0x28,  /* Class Selector 5 */
    DSCP_EF  = 0x2E,  /* Expedited Forwarding (Voice) */
    DSCP_CS6 = 0x30,  /* Class Selector 6 */
    DSCP_CS7 = 0x38   /* Class Selector 7 */
} DiffServDSCP;

/* Scheduling discipline */
typedef enum {
    QOS_SCHED_STRICT_PRIORITY = 0,
    QOS_SCHED_WRR             = 1,  /* Weighted Round Robin */
    QOS_SCHED_DRR             = 2,  /* Deficit Round Robin */
    QOS_SCHED_SP_WRR          = 3,  /* Strict Priority + WRR hybrid */
    QOS_SCHED_ETS             = 4   /* Enhanced Transmission Selection */
} QoSScheduling;

/* Traffic class configuration */
typedef struct {
    int         class_id;
    QoSPriority priority;
    uint8_t     dscp_values[8];   /* DSCP values mapped to this class */
    int         num_dscp;
    int         weight;            /* For WRR/ETS scheduling */
    int         quantum;           /* DRR quantum (bytes) */
    uint64_t    bytes_tx;
    uint64_t    packets_tx;
    uint64_t    bytes_dropped;
    uint64_t    packets_dropped;
} QoSTrafficClass;

/* ETS (Enhanced Transmission Selection) per-class config */
typedef struct {
    int    class_id;
    int    bandwidth_percent;   /* % of available bandwidth */
    bool   strict_priority;     /* SP class (no bandwidth limit) */
    int    min_bandwidth_mbps;  /* Guaranteed minimum */
} ETSClassConfig;

/* QoS Scheduler */
typedef struct {
    QoSScheduling        discipline;
    QoSTrafficClass      classes[QOS_MAX_CLASSES];
    int                  num_classes;
    int                  default_class;
    /* DRR state */
    int                  deficit_counters[QOS_MAX_CLASSES];
    /* ETS state */
    ETSClassConfig       ets_configs[QOS_MAX_CLASSES];
    bool                 ets_enabled;
    /* Statistics */
    uint64_t             total_bytes_scheduled;
    uint64_t             total_packets_scheduled;
} QoSScheduler;

/* DCB (Data Center Bridging) state
 * Combines PFC + ETS + QCN for lossless Ethernet */
typedef struct {
    bool     dcb_enabled;
    bool     pfc_enabled;
    bool     ets_enabled;
    bool     qcn_enabled;  /* Quantized Congestion Notification */
    int      pfc_priorities;
    int      ets_bandwidth_total; /* Should sum to 100% */
    uint64_t qcn_congestion_events;
} DCBState;

/* API */
/* Traffic class management */
void      qos_class_init(QoSTrafficClass *tc, int class_id,
                         QoSPriority prio, int weight);
void      qos_class_add_dscp(QoSTrafficClass *tc, uint8_t dscp);
bool      qos_class_match_dscp(const QoSTrafficClass *tc, uint8_t dscp);

/* Scheduler */
void      qos_scheduler_init(QoSScheduler *sched, QoSScheduling discipline);
int       qos_scheduler_add_class(QoSScheduler *sched, QoSPriority prio,
                                  int weight, int quantum);
int       qos_scheduler_classify(const QoSScheduler *sched, uint8_t dscp);
int       qos_schedule_sp(QoSScheduler *sched, int *queue_lengths);
int       qos_schedule_wrr(QoSScheduler *sched, int *queue_lengths,
                           int round_size);
int       qos_schedule_drr(QoSScheduler *sched, int *queue_lengths);

/* DRR Algorithm (L5 - Shreedhar & Varghese, 1995) */
void      drr_init_counters(QoSScheduler *sched);
int       drr_schedule_next(QoSScheduler *sched, int *queue_lengths);

/* ETS */
void      ets_init_class(ETSClassConfig *ets, int class_id,
                         int bw_percent, bool strict);
void      qos_ets_configure(QoSScheduler *sched,
                            const ETSClassConfig *configs, int n_configs);
double    ets_available_bw(const ETSClassConfig *ets, double total_bw);

/* DCB */
void      dcb_init(DCBState *dcb);
void      dcb_enable_pfc(DCBState *dcb, int priorities_bitmask);
void      dcb_enable_ets(DCBState *dcb, int total_bw_percent);
void      dcb_enable_qcn(DCBState *dcb);
void      dcb_print_state(const DCBState *dcb);

/* Utility */
const char *qos_priority_name(QoSPriority prio);
const char *dscp_name(uint8_t dscp);
void        qos_scheduler_print_stats(const QoSScheduler *sched);

#endif /* QOS_H */
