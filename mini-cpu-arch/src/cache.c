#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

const char* replace_policy_name(ReplacePolicy rp) {
    static const char* names[] = {"LRU","FIFO","Random","PLRU"};
    return (rp < REPLACE_COUNT) ? names[rp] : "Unknown";
}

const char* write_policy_name(WritePolicy wp) {
    static const char* names[] = {"WT-NoAlloc","WT-Alloc","WB-Alloc"};
    return (wp < WRITE_POLICY_COUNT) ? names[wp] : "Unknown";
}

const char* mesi_state_name(MESIState s) {
    static const char* names[] = {"M","E","S","I"};
    return (s < MESI_STATE_COUNT) ? names[s] : "?";
}

void cache_addr_split(uint32_t addr, uint32_t line_size, uint32_t num_sets,
                      uint32_t* tag, uint32_t* set_index, uint32_t* offset) {
    uint32_t offset_bits = (uint32_t)(log2(line_size));
    uint32_t set_bits    = (uint32_t)(log2(num_sets));
    *offset     = addr & (line_size - 1);
    *set_index  = (addr >> offset_bits) & (num_sets - 1);
    *tag        = addr >> (offset_bits + set_bits);
}

int cache_init(Cache* c, CacheGeometry geom, ReplacePolicy rp, WritePolicy wp) {
    if (!c) return -1;
    if (geom.line_size < 4 || geom.line_size > CACHE_LINE_SIZE_MAX) return -1;
    if (geom.associativity == 0 || geom.associativity > CACHE_MAX_WAYS) return -1;
    if (geom.num_sets == 0 || geom.num_sets > CACHE_MAX_SETS) return -1;
    if (geom.size_bytes != geom.line_size * geom.associativity * geom.num_sets) return -1;
    memset(c, 0, sizeof(Cache));
    c->geom = geom; c->replace_policy = rp; c->write_policy = wp;
    size_t total = (size_t)geom.num_sets * geom.associativity;
    c->lines = (CacheLine*)calloc(total, sizeof(CacheLine));
    if (!c->lines) return -1;
    for (size_t i = 0; i < total; i++) c->lines[i].mesi_state = MESI_INVALID;
    if (rp == REPLACE_PLRU) {
        c->plru_bits = (uint32_t*)calloc(geom.num_sets * (geom.associativity - 1), sizeof(uint32_t));
        if (!c->plru_bits) { free(c->lines); return -1; }
    }
    return 0;
}

void cache_destroy(Cache* c) {
    if (!c) return;
    free(c->lines); free(c->plru_bits);
    memset(c, 0, sizeof(Cache));
}

void cache_reset(Cache* c) {
    if (!c) return;
    size_t total = (size_t)c->geom.num_sets * c->geom.associativity;
    for (size_t i = 0; i < total; i++) {
        c->lines[i].valid = false; c->lines[i].dirty = false;
        c->lines[i].mesi_state = MESI_INVALID; c->lines[i].access_order = 0;
    }
    if (c->plru_bits) memset(c->plru_bits, 0, c->geom.num_sets * (c->geom.associativity - 1) * sizeof(uint32_t));
    cache_stats_reset(c); c->clock = 0;
}

void cache_stats_reset(Cache* c) {
    if (c) memset(&c->stats, 0, sizeof(CacheStats));
}

double cache_miss_rate(const Cache* c) {
    if (!c || c->stats.accesses == 0) return 0.0;
    return (double)c->stats.misses / (double)c->stats.accesses;
}

double cache_hit_rate(const Cache* c) {
    if (!c || c->stats.accesses == 0) return 0.0;
    return (double)c->stats.hits / (double)c->stats.accesses;
}

int cache_lookup(const Cache* c, uint32_t addr) {
    if (!c) return -1;
    uint32_t tag, set_index, offset;
    cache_addr_split(addr, c->geom.line_size, c->geom.num_sets, &tag, &set_index, &offset);
    (void)offset;
    uint32_t ways = c->geom.associativity;
    for (uint32_t w = 0; w < ways; w++) {
        int idx = (int)(set_index * ways + w);
        if (c->lines[idx].valid && c->lines[idx].tag == tag) return idx;
    }
    return -1;
}

/* ---- PLRU Tree Update (L5: Algorithm) ----
 * Binary tree of internal-node bits. Bit[i]=0 means go left next victim.
 * On access to way k, walk root->leaf, set each node to point AWAY from k. */
static void plru_update(Cache* c, uint32_t set_index, uint32_t way) {
    uint32_t assoc = c->geom.associativity;
    if (assoc <= 1 || !c->plru_bits) return;
    uint32_t base = set_index * (assoc - 1);
    uint32_t rs = 0, re = assoc, node = 0;
    while (re - rs > 1) {
        uint32_t mid = (rs + re) / 2;
        if (way < mid) {
            c->plru_bits[base + node] = 1;
            re = mid; node = 2 * node + 1;
        } else {
            c->plru_bits[base + node] = 0;
            rs = mid; node = 2 * node + 2;
        }
    }
}

/* Find victim way for eviction.  First check for invalid slot, else
 * apply replacement policy.  O(ways) complexity per eviction.
 * Theorem (L4): Belady's OPT is optimal but requires future knowledge;
 * LRU approximates it with O(1) per access using the access-order clock. */
static uint32_t find_victim(Cache* c, uint32_t set_index) {
    uint32_t ways = c->geom.associativity;
    uint32_t base = set_index * ways;

    for (uint32_t w = 0; w < ways; w++)
        if (!c->lines[base + w].valid) return w;

    switch (c->replace_policy) {
        case REPLACE_RANDOM:
            return (uint32_t)(rand() % ways);

        case REPLACE_FIFO: {
            uint32_t oldest = UINT32_MAX, victim = 0;
            for (uint32_t w = 0; w < ways; w++) {
                if (c->lines[base + w].access_order < oldest) {
                    oldest = c->lines[base + w].access_order; victim = w;
                }
            }
            return victim;
        }

        case REPLACE_LRU: {
            uint32_t lru_order = UINT32_MAX, victim = 0;
            for (uint32_t w = 0; w < ways; w++) {
                if (c->lines[base + w].access_order < lru_order) {
                    lru_order = c->lines[base + w].access_order; victim = w;
                }
            }
            return victim;
        }

        case REPLACE_PLRU: {
            if (!c->plru_bits) return 0;
            uint32_t bits_base = set_index * (ways - 1);
            uint32_t node = 0, rs = 0, re = ways;
            while (re - rs > 1) {
                uint32_t mid = (rs + re) / 2;
                if (c->plru_bits[bits_base + node] == 0) {
                    re = mid; node = 2 * node + 1;
                } else {
                    rs = mid; node = 2 * node + 2;
                }
            }
            return (rs < ways) ? rs : 0;
        }

        default: return 0;
    }
}

/* Cache read: returns 0 (L1 hit) or main-memory latency on miss.
 * Miss path: evict victim line (writeback if dirty), fill new line.
 * MESI: new line starts in EXCLUSIVE state. */
uint32_t cache_read(Cache* c, uint32_t addr) {
    if (!c) return 0;
    c->stats.accesses++; c->stats.reads++; c->clock++;

    int line_idx = cache_lookup(c, addr);
    if (line_idx >= 0) {
        c->stats.hits++;
        c->lines[line_idx].access_order = c->clock;
        uint32_t tag, si, off;
        cache_addr_split(addr, c->geom.line_size, c->geom.num_sets, &tag, &si, &off);
        plru_update(c, si, (uint32_t)line_idx % c->geom.associativity);
        return 0;
    }

    /* MISS */
    c->stats.misses++;
    uint32_t tag, si, off;
    cache_addr_split(addr, c->geom.line_size, c->geom.num_sets, &tag, &si, &off);
    uint32_t vw = find_victim(c, si);
    uint32_t base = si * c->geom.associativity;
    CacheLine* vl = &c->lines[base + vw];
    if (vl->valid && vl->dirty) c->stats.writebacks++;
    vl->valid = true; vl->dirty = false; vl->tag = tag;
    vl->access_order = c->clock; vl->mesi_state = MESI_EXCLUSIVE;
    plru_update(c, si, vw);
    return 0;
}

/* Cache write: for write-back, mark dirty.  For write-through,
 * data propagates to next level immediately (handled by caller).
 * On miss with write-allocate: fill line then mark dirty.
 * On miss with no-allocate: bypass cache entirely. */
void cache_write(Cache* c, uint32_t addr, uint32_t value) {
    if (!c) return;
    (void)value;
    c->stats.accesses++; c->stats.writes++; c->clock++;

    int line_idx = cache_lookup(c, addr);
    if (line_idx >= 0) {
        /* WRITE HIT */
        c->stats.hits++;
        switch (c->write_policy) {
            case WRITE_BACK_ALLOC:
                c->lines[line_idx].dirty = true;
                c->lines[line_idx].mesi_state = MESI_MODIFIED;
                break;
            case WRITE_THROUGH_ALLOC:
            case WRITE_THROUGH_NO_ALLOC:
                c->lines[line_idx].mesi_state = MESI_MODIFIED;
                break;
            default: break;
        }
        c->lines[line_idx].access_order = c->clock;
        return;
    }

    /* WRITE MISS */
    c->stats.misses++;
    uint32_t tag, si, off;
    cache_addr_split(addr, c->geom.line_size, c->geom.num_sets, &tag, &si, &off);

    if (c->write_policy == WRITE_BACK_ALLOC ||
        c->write_policy == WRITE_THROUGH_ALLOC) {
        uint32_t vw = find_victim(c, si);
        uint32_t base = si * c->geom.associativity;
        CacheLine* vl = &c->lines[base + vw];
        if (vl->valid && vl->dirty) c->stats.writebacks++;
        vl->valid = true;
        vl->dirty = (c->write_policy == WRITE_BACK_ALLOC);
        vl->tag = tag; vl->access_order = c->clock;
        vl->mesi_state = MESI_MODIFIED;
        plru_update(c, si, vw);
    }
    /* WRITE_THROUGH_NO_ALLOC: bypass, no cache line allocated */
}

void cache_set_mesi(Cache* c, uint32_t addr, MESIState s) {
    if (!c) return;
    int idx = cache_lookup(c, addr);
    if (idx >= 0) c->lines[idx].mesi_state = (uint8_t)s;
}


/* ================================================================
 * Cache Hierarchy (L3: Engineering Structures) ¡ª Inclusive L1->L2->L3
 *
 * On L1 data read miss: probe L2. On L2 miss: probe L3. On L3 miss:
 * go to main memory.  Each level fills the one above it.
 * Write propagation: L1 write-through updates L2; L1 write-back
 * defers until eviction.  L2 handles coherence with L1.
 *
 * Hierarchy AMAT (L4): AMAT = L1_hit_time + L1_miss_rate *
 *   (L2_hit_time + L2_miss_rate * (L3_hit_time + L3_miss_rate * memory_latency))
 *
 * This formula generalizes to N levels by recursion.
 * ================================================================ */

int cache_hierarchy_init(CacheHierarchy* h,
                          CacheGeometry l1i_geo, CacheGeometry l1d_geo,
                          CacheGeometry l2_geo, CacheGeometry l3_geo,
                          WritePolicy l1_wp, WritePolicy l2_wp) {
    if (!h) return -1;

    h->L1I = (Cache*)calloc(1, sizeof(Cache));
    h->L1D = (Cache*)calloc(1, sizeof(Cache));
    h->L2  = (Cache*)calloc(1, sizeof(Cache));

    if (!h->L1I || !h->L1D || !h->L2) {
        free(h->L1I); free(h->L1D); free(h->L2); return -1;
    }

    if (cache_init(h->L1I, l1i_geo, REPLACE_LRU, WRITE_BACK_ALLOC) < 0) return -1;
    if (cache_init(h->L1D, l1d_geo, REPLACE_LRU, l1_wp) < 0) return -1;
    if (cache_init(h->L2,  l2_geo,  REPLACE_LRU, l2_wp) < 0) return -1;

    if (l3_geo.size_bytes > 0) {
        h->L3 = (Cache*)calloc(1, sizeof(Cache));
        if (!h->L3 || cache_init(h->L3, l3_geo, REPLACE_LRU, WRITE_BACK_ALLOC) < 0) {
            return -1;
        }
    } else {
        h->L3 = NULL;
    }

    h->main_memory_latency = 100;
    return 0;
}

void cache_hierarchy_destroy(CacheHierarchy* h) {
    if (!h) return;
    if (h->L1I) { cache_destroy(h->L1I); free(h->L1I); }
    if (h->L1D) { cache_destroy(h->L1D); free(h->L1D); }
    if (h->L2)  { cache_destroy(h->L2);  free(h->L2);  }
    if (h->L3)  { cache_destroy(h->L3);  free(h->L3);  }
    memset(h, 0, sizeof(CacheHierarchy));
}

/* Hierarchy read: check L1 first (I$ or D$ based on is_ifetch flag).
 * On miss, probe L2, then L3, then main memory.
 * Each miss fills the level above. */
uint32_t cache_hierarchy_read(CacheHierarchy* h, uint32_t addr, bool is_ifetch) {
    if (!h) return 0;
    Cache* l1 = is_ifetch ? h->L1I : h->L1D;

    /* L1 hit */
    if (cache_lookup(l1, addr) >= 0) {
        l1->stats.accesses++; l1->stats.hits++;
        return 0;
    }
    l1->stats.accesses++; l1->stats.misses++;

    /* L2 probe */
    if (cache_lookup(h->L2, addr) >= 0) {
        h->L2->stats.accesses++; h->L2->stats.hits++;
        cache_read(l1, addr);  /* fill L1 */
        return 0;
    }
    h->L2->stats.accesses++; h->L2->stats.misses++;

    /* L3 probe (if present) */
    if (h->L3) {
        if (cache_lookup(h->L3, addr) >= 0) {
            h->L3->stats.accesses++; h->L3->stats.hits++;
            cache_read(h->L2, addr);  /* fill L2 */
            cache_read(l1, addr);     /* fill L1 */
            return 0;
        }
        h->L3->stats.accesses++; h->L3->stats.misses++;
    }

    /* Main memory: all levels miss */
    return h->main_memory_latency;
}

/* Hierarchy write: delegates to L1D; if L1 is write-through, also write L2. */
void cache_hierarchy_write(CacheHierarchy* h, uint32_t addr, uint32_t value) {
    if (!h) return;
    cache_write(h->L1D, addr, value);
    if (h->L1D->write_policy == WRITE_THROUGH_ALLOC ||
        h->L1D->write_policy == WRITE_THROUGH_NO_ALLOC) {
        cache_write(h->L2, addr, value);
    }
}

/* AMAT (L4: Theorem with derivation)
 *
 * For N levels:
 *   AMAT_N = hit_1 + miss_1 * (hit_2 + miss_2 * (... + miss_N * mem_lat))
 *
 * Derivation: Let T_k = access time of level k.
 * E[T] = T_1 * P(hit in L1) + (T_1 + T_2) * P(miss L1, hit L2) + ...
 *      = T_1 + P(miss L1)*(T_2 + P(miss L2)*(T_3 + ... ))
 *
 * Amdahl's Law connection (L4): if we speed up caches (reduce hit time),
 * the overall speedup is bounded by 1/(1 - f_cache + f_cache/speedup).
 * Miss penalty is the serial fraction that Amdahl identifies. */
double cache_hierarchy_amat(const CacheHierarchy* h,
                            uint32_t l1_lat, uint32_t l2_lat, uint32_t l3_lat) {
    if (!h) return 0.0;

    double l1_miss = cache_miss_rate(h->L1D);
    double l2_miss = cache_miss_rate(h->L2);
    double l3_miss = h->L3 ? cache_miss_rate(h->L3) : 1.0;

    double l1_term = (double)l1_lat + l1_miss * (double)l2_lat;

    double deeper;
    if (h->L3) {
        deeper = (double)l3_lat + l3_miss * (double)h->main_memory_latency;
    } else {
        deeper = (double)h->main_memory_latency;
    }
    double l2_term = l2_miss * deeper;

    return l1_term + l1_miss * l2_term;
}

/* ---- Cache Inspection (Debug/Trace) ---- */

void cache_dump(const Cache* c, const char* name) {
    if (!c) return;
    printf("=== Cache: %s ===\n", name ? name : "unknown");
    printf("  Geometry: %uB total, %uB line, %u-way, %u sets\n",
           c->geom.size_bytes, c->geom.line_size,
           c->geom.associativity, c->geom.num_sets);
    printf("  Policy: %s / %s\n",
           replace_policy_name(c->replace_policy),
           write_policy_name(c->write_policy));
    printf("  Stats: %llu accesses, %llu hits, %llu misses (%.2f%% hit rate)\n",
           (unsigned long long)c->stats.accesses,
           (unsigned long long)c->stats.hits,
           (unsigned long long)c->stats.misses,
           cache_hit_rate(c) * 100.0);
    printf("  Reads: %llu, Writes: %llu, Writebacks: %llu, Invalidations: %llu\n",
           (unsigned long long)c->stats.reads,
           (unsigned long long)c->stats.writes,
           (unsigned long long)c->stats.writebacks,
           (unsigned long long)c->stats.invalidations);

    /* Show valid lines for small caches */
    size_t total = (size_t)c->geom.num_sets * c->geom.associativity;
    int shown = 0;
    for (size_t i = 0; i < total && shown < 16; i++) {
        if (c->lines[i].valid) {
            printf("    [%zu] tag=0x%X dirty=%d mesi=%s\n",
                   i, c->lines[i].tag, c->lines[i].dirty,
                   mesi_state_name((MESIState)c->lines[i].mesi_state));
            shown++;
        }
    }
    if (shown == 0) printf("    (all lines invalid)\n");
}

void cache_hierarchy_dump(const CacheHierarchy* h) {
    if (!h) return;
    printf("\n===== Cache Hierarchy Summary =====\n");
    cache_dump(h->L1I, "L1I");
    cache_dump(h->L1D, "L1D");
    cache_dump(h->L2,  "L2");
    if (h->L3) cache_dump(h->L3, "L3");
    printf("  Main Memory Latency: %u cycles\n", h->main_memory_latency);
    printf("  AMAT (1-3-10-100): %.2f cycles\n",
           cache_hierarchy_amat(h, 1, 3, 10));
    printf("===================================\n");
}


/* ================================================================
 * MESI Cache Coherence Protocol (L4: Standards/Theorems)
 *
 * MESI states: Modified, Exclusive, Shared, Invalid
 *
 * State Machine (simplified):
 *
 *   I --[PrRd,no-sharers]--> E    (read miss, exclusive access)
 *   I --[PrRd,has-sharers]--> S    (read miss, shared)
 *   E --[PrWr]-------------> M    (write hit, no bus txn needed)
 *   E --[BusRd from other]--> S    (another core read, downgrade)
 *   S --[PrWr]-------------> M    (write hit, invalidate others via bus)
 *   S --[BusRdX from other]-> I    (another core wants exclusive, invalidate)
 *   M --[BusRd from other]--> S    (snoop read: write back, share)
 *   M --[BusRdX from other]-> I    (snoop write: write back, invalidate)
 *
 * FLP Impossibility (L4): In an asynchronous network with node failures,
 * cache coherence is equivalent to the consensus problem.  MESI guarantees
 * safety (no two cores have M/E for same line) but may require retry on
 * contention, which can lead to livelock without fair arbitration.
 *
 * MOESI extension (L8): Adds "Owned" state for write-back without
 * invalidating sharers ¡ª reduces bus traffic for migratory data.
 * ================================================================ */

void mesi_bus_init(CoherenceBus* bus, Cache** cores, int num_cores) {
    if (!bus) return;
    memset(bus, 0, sizeof(CoherenceBus));
    bus->num_cores = (num_cores > 4) ? 4 : num_cores;
    for (int i = 0; i < bus->num_cores; i++) {
        bus->caches[i] = cores[i];
    }
}

/* mesi_bus_write_snoop: When core_id writes to addr, snoop other caches.
 * Any S or E copy in another cache is invalidated.
 * Any M copy is written back to memory then invalidated.
 * Returns number of invalidations performed. */
int mesi_bus_write_snoop(CoherenceBus* bus, int core_id, uint32_t addr) {
    if (!bus || core_id < 0 || core_id >= bus->num_cores) return 0;

    int inv_count = 0;
    for (int i = 0; i < bus->num_cores; i++) {
        if (i == core_id || !bus->caches[i]) continue;

        int idx = cache_lookup(bus->caches[i], addr);
        if (idx < 0) continue;

        uint8_t st = bus->caches[i]->lines[idx].mesi_state;

        /* S or E: simply invalidate (no writeback needed, clean) */
        if (st == MESI_SHARED || st == MESI_EXCLUSIVE) {
            bus->caches[i]->lines[idx].mesi_state = MESI_INVALID;
            bus->caches[i]->stats.invalidations++;
            inv_count++;
        }
        /* M: must write back before invalidating */
        else if (st == MESI_MODIFIED) {
            bus->caches[i]->stats.writebacks++;
            bus->caches[i]->lines[idx].mesi_state = MESI_INVALID;
            bus->caches[i]->stats.invalidations++;
            inv_count++;
        }
        /* I: nothing to do */
    }

    bus->snoop_requests++;
    /* Bus cycle cost: 1 for no invalidation, 2+ for snoop responses */
    bus->bus_cycles += (uint32_t)(inv_count > 0 ? 2 : 1);
    return inv_count;
}

/* mesi_bus_read_snoop: When core_id reads addr, check if another core
 * has the line in M state.  If so, that core must write back and
 * demote to S.  Any E copies are demoted to S.
 * Returns true if modified data was found elsewhere (cache-to-cache transfer). */
bool mesi_bus_read_snoop(CoherenceBus* bus, int core_id, uint32_t addr) {
    if (!bus || core_id < 0 || core_id >= bus->num_cores) return false;

    bool found_modified = false;
    for (int i = 0; i < bus->num_cores; i++) {
        if (i == core_id || !bus->caches[i]) continue;

        int idx = cache_lookup(bus->caches[i], addr);
        if (idx < 0) continue;

        uint8_t st = bus->caches[i]->lines[idx].mesi_state;

        if (st == MESI_MODIFIED) {
            /* Cache-to-cache transfer: write back then share */
            bus->caches[i]->stats.writebacks++;
            bus->caches[i]->lines[idx].mesi_state = MESI_SHARED;
            found_modified = true;
        } else if (st == MESI_EXCLUSIVE) {
            /* Another core has it exclusively; downgrade to shared */
            bus->caches[i]->lines[idx].mesi_state = MESI_SHARED;
        }
        /* SHARED: remains shared, no state change needed */
    }

    bus->snoop_requests++;
    bus->bus_cycles++;
    return found_modified;
}

