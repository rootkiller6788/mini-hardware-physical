#ifndef PREFETCH_H
#define PREFETCH_H

#include <stdbool.h>
#include <stdint.h>

/* L1: Core Definitions — Hardware Data Prefetching */

#define PREFETCH_QUEUE_SIZE    16
#define PREFETCH_DEGREE         4
#define PREFETCH_DISTANCE       8
#define PREFETCH_RPT_SIZE     256
#define PREFETCH_SMS_REGIONS   64
#define PREFETCH_GHB_SIZE     256
#define PREFETCH_IT_CACHE    1024

/* L1: Prefetch request type */
typedef enum {
    PREFETCH_L1_ONLY,
    PREFETCH_L2_ONLY,
    PREFETCH_ALL_LEVELS
} PrefetchLevel;

typedef struct {
    uint32_t    address;
    bool        issued;
    bool        used;       /* Was this prefetch actually consumed? */
    uint64_t    timestamp;
} PrefetchRequest;

typedef struct {
    PrefetchRequest queue[PREFETCH_QUEUE_SIZE];
    uint32_t    head;
    uint32_t    tail;
    uint32_t    count;
    uint64_t    total_issued;
    uint64_t    total_used;
    uint64_t    total_wasted;   /* Issued but never used = bandwidth waste */
} PrefetchQueue;

/* L2: Next-line prefetcher (simplest hardware prefetcher) */
typedef struct {
    uint32_t    last_miss_addr;
    uint64_t    prefetches_issued;
    uint64_t    prefetches_used;
    double      accuracy;
    uint32_t    degree;      /* How many cache lines to prefetch ahead */
} NextLinePrefetcher;

/* L2: Stride prefetcher (Baer & Chen 1991 — detects constant stride patterns) */
typedef struct {
    uint32_t    last_addr;
    int32_t     last_stride;
    int32_t     confidence;      /* Requires 2 consecutive same strides */
    uint64_t    prefetches_issued;
    uint64_t    prefetches_used;
    double      accuracy;
} StridePrefetcher;

/* L3: Markov prefetcher (Joseph & Grunwald 1997 — correlation-based) */
typedef struct {
    uint32_t    key;             /* Current miss address for correlation */
    uint32_t    successors[4];   /* Top-4 successor addresses */
    uint8_t     successor_count;
    uint8_t     frequencies[4];
    uint64_t    prefetches_issued;
    uint64_t    prefetches_used;
} MarkovPrefetcher;

/* L3: Reference Prediction Table (RPT) — Chen & Baer 1995 */
typedef struct {
    uint32_t    tag;
    uint32_t    last_addr;
    int32_t     stride;
    uint8_t     state;       /* 2-bit saturating state machine */
    bool        valid;
} RPTEntry;

typedef struct {
    RPTEntry    entries[PREFETCH_RPT_SIZE];
    uint64_t    accesses;
    uint64_t    hits;
    uint64_t    prefetches_issued;
} RPTPrefetcher;

/* L8: GHB-based prefetcher (Global History Buffer — Nesbit & Smith 2004) */
typedef struct {
    uint32_t    address;
    int32_t     delta;       /* Delta from previous address */
    uint32_t    next_idx;    /* Linked list pointer */
} GHBEntry;

typedef enum {
    GHB_DELTA_CORRELATION,
    GHB_DELTA_PATH,
    GHB_STRIDE_PATH
} GHBMethod;

typedef struct {
    GHBEntry    ghb[PREFETCH_GHB_SIZE];
    uint32_t    head;
    uint32_t    tail;
    uint32_t    count;
    GHBMethod   method;
    uint64_t    prefetches_issued;
    uint64_t    prefetches_used;
    double      coverage;
    double      accuracy;
} GHBPrefetcher;

/* L8: SMS (Spatial Memory Streaming — Somogyi et al. 2006) */
typedef struct {
    uint32_t    trigger_pc;      /* PC that triggers this spatial pattern */
    uint32_t    trigger_offset;
    int32_t     deltas[8];       /* Spatial pattern as delta offsets */
    uint8_t     delta_count;
    uint64_t    frequency;
} SpatialPattern;

typedef struct {
    SpatialPattern patterns[PREFETCH_SMS_REGIONS];
    uint32_t    active_region;
    uint64_t    prefetches_issued;
    uint64_t    prefetches_used;
    double      coverage;
} SMSPrefetcher;

/* L5: Unified prefetch controller */
typedef enum {
    PREF_NONE,
    PREF_NEXT_LINE,
    PREF_STRIDE,
    PREF_MARKOV,
    PREF_RPT,
    PREF_GHB,
    PREF_SMS
} PrefetcherType;

typedef struct {
    PrefetcherType      type;
    NextLinePrefetcher  next_line;
    StridePrefetcher    stride;
    MarkovPrefetcher    markov;
    RPTPrefetcher       rpt;
    GHBPrefetcher       ghb;
    SMSPrefetcher       sms;
    PrefetchQueue       queue;
    uint64_t            total_demand_misses;
    uint64_t            total_prefetch_hits;
} PrefetchController;

/* L1 API */
void pf_init(PrefetchController *pf, PrefetcherType type);
bool pf_request_prefetch(PrefetchController *pf, uint32_t miss_addr, uint32_t miss_pc);
bool pf_get_prefetch(PrefetchController *pf, uint32_t *addr);
void pf_notify_use(PrefetchController *pf, uint32_t addr);

/* L2: Next-line */
void nextline_record_miss(NextLinePrefetcher *nl, uint32_t miss_addr);
bool nextline_get_prefetch(NextLinePrefetcher *nl, uint32_t *addr);

/* L2: Stride */
void stride_record_access(StridePrefetcher *sp, uint32_t addr);
bool stride_get_prefetch(StridePrefetcher *sp, uint32_t *addr);

/* L3: Markov */
void markov_record_miss(MarkovPrefetcher *mp, uint32_t prev_addr, uint32_t curr_addr);
uint32_t markov_get_prefetch(MarkovPrefetcher *mp, uint32_t *addrs, uint32_t max_count);

/* L3: RPT */
void rpt_record_access(RPTPrefetcher *rpt, uint32_t pc, uint32_t addr);
bool rpt_get_prefetch(RPTPrefetcher *rpt, uint32_t pc, uint32_t *addr);

/* L8: GHB */
void ghb_record_access(GHBPrefetcher *ghb, uint32_t addr);
bool ghb_get_prefetch(GHBPrefetcher *ghb, uint32_t *addr);

/* L8: SMS */
void sms_record_access(SMSPrefetcher *sms, uint32_t pc, uint32_t addr);
bool sms_get_prefetch(SMSPrefetcher *sms, uint32_t pc, uint32_t *addr);

/* Stats */
void pf_print_stats(const PrefetchController *pf);
double pf_coverage(const PrefetchController *pf);
double pf_accuracy(const PrefetchController *pf);
double pf_timeliness(const PrefetchController *pf);

#endif
