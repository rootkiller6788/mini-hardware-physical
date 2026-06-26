#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ================================================================
 * cache.h — Cache Hierarchy Simulation (L1-L5)
 *
 * Covers: Direct-mapped, set-associative, fully-associative caches;
 * LRU/FIFO/Random/PLRU replacement; write-through/write-back policies;
 * multi-level hierarchy; MESI coherence protocol; AMAT analysis.
 *
 * Reference: MIT 6.004 Ch 14, Stanford EE282 Lec 12-15,
 * Berkeley CS 152, CMU 15-418 Lec 6-8.
 *
 * Theorem (L4): AMAT = hit_time + miss_rate * miss_penalty
 * Multi-level: AMAT = L1h + L1m*(L2h + L2m*(L3h + L3m*mem))
 * MESI: S -> E on exclusive read; E -> M on write; S -> I on snoop.
 * ================================================================ */

#define CACHE_MAX_WAYS      16
#define CACHE_MAX_SETS      1024
#define CACHE_LINE_SIZE_MAX 256

/* L1: Core Definitions */
typedef struct {
    uint32_t size_bytes;
    uint32_t line_size;
    uint32_t associativity;
    uint32_t num_sets;
} CacheGeometry;

/* L2: Core Concepts — Replacement policies */
typedef enum {
    REPLACE_LRU,
    REPLACE_FIFO,
    REPLACE_RANDOM,
    REPLACE_PLRU,
    REPLACE_COUNT
} ReplacePolicy;

/* Write policies */
typedef enum {
    WRITE_THROUGH_NO_ALLOC,
    WRITE_THROUGH_ALLOC,
    WRITE_BACK_ALLOC,
    WRITE_POLICY_COUNT
} WritePolicy;

/* L4: MESI coherence states */
typedef enum {
    MESI_MODIFIED,
    MESI_EXCLUSIVE,
    MESI_SHARED,
    MESI_INVALID,
    MESI_STATE_COUNT
} MESIState;

/* Cache line structure */
typedef struct {
    bool      valid;
    bool      dirty;
    uint32_t  tag;
    uint32_t  access_order;
    uint8_t   mesi_state;
} CacheLine;

/* Statistics (L3: Engineering Structures) */
typedef struct {
    uint64_t accesses;
    uint64_t hits;
    uint64_t misses;
    uint64_t reads;
    uint64_t writes;
    uint64_t writebacks;
    uint64_t invalidations;
} CacheStats;

/* Single cache level */
typedef struct {
    CacheGeometry geom;
    CacheLine*    lines;
    ReplacePolicy replace_policy;
    WritePolicy   write_policy;
    CacheStats    stats;
    uint32_t      clock;
    uint32_t*     plru_bits;
} Cache;

/* Multi-level cache hierarchy */
typedef struct {
    Cache*  L1I;
    Cache*  L1D;
    Cache*  L2;
    Cache*  L3;
    uint32_t main_memory_latency;
} CacheHierarchy;

/* ---- Lifecycle ---- */
int      cache_init(Cache* c, CacheGeometry geom, ReplacePolicy rp, WritePolicy wp);
void     cache_destroy(Cache* c);
void     cache_reset(Cache* c);

/* ---- Read/Write ---- */
uint32_t cache_read(Cache* c, uint32_t addr);
void     cache_write(Cache* c, uint32_t addr, uint32_t value);
int      cache_lookup(const Cache* c, uint32_t addr);
void     cache_set_mesi(Cache* c, uint32_t addr, MESIState s);

/* ---- Statistics ---- */
void     cache_stats_reset(Cache* c);
double   cache_miss_rate(const Cache* c);
double   cache_hit_rate(const Cache* c);

/* ---- Hierarchy ---- */
int      cache_hierarchy_init(CacheHierarchy* h,
                              CacheGeometry l1i_geo, CacheGeometry l1d_geo,
                              CacheGeometry l2_geo, CacheGeometry l3_geo,
                              WritePolicy l1_wp, WritePolicy l2_wp);
void     cache_hierarchy_destroy(CacheHierarchy* h);
uint32_t cache_hierarchy_read(CacheHierarchy* h, uint32_t addr, bool is_ifetch);
void     cache_hierarchy_write(CacheHierarchy* h, uint32_t addr, uint32_t value);
double   cache_hierarchy_amat(const CacheHierarchy* h,
                              uint32_t l1_lat, uint32_t l2_lat, uint32_t l3_lat);
void     cache_dump(const Cache* c, const char* name);
void     cache_hierarchy_dump(const CacheHierarchy* h);

/* ---- MESI Coherence (L4) ---- */
typedef struct {
    Cache*    caches[4];
    int       num_cores;
    uint32_t  bus_cycles;
    uint64_t  snoop_requests;
} CoherenceBus;

void     mesi_bus_init(CoherenceBus* bus, Cache** cores, int num_cores);
int      mesi_bus_write_snoop(CoherenceBus* bus, int core_id, uint32_t addr);
bool     mesi_bus_read_snoop(CoherenceBus* bus, int core_id, uint32_t addr);

/* ---- Utilities ---- */
void     cache_addr_split(uint32_t addr, uint32_t line_size, uint32_t num_sets,
                          uint32_t* tag, uint32_t* set_index, uint32_t* offset);
const char* replace_policy_name(ReplacePolicy rp);
const char* write_policy_name(WritePolicy wp);
const char* mesi_state_name(MESIState s);

#endif /* CACHE_H */
