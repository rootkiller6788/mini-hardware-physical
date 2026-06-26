#include "prefetch.h"
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== L2: Next-Line Prefetcher =====
 * Simplest hardware prefetcher: on a miss to address A,
 * prefetch A + line_size. Also known as adjacent-line or
 * sequential prefetching. Smith 1982, "Cache Memories".
 */

void nextline_record_miss(NextLinePrefetcher *nl, uint32_t miss_addr)
{
    nl->last_miss_addr = miss_addr;
    nl->prefetches_issued++;
}

bool nextline_get_prefetch(NextLinePrefetcher *nl, uint32_t *addr)
{
    if (addr) *addr = nl->last_miss_addr + CACHE_LINE_DATA_SIZE;
    return true;
}

/* ===== L2: Stride Prefetcher (Baer & Chen 1991) =====
 * Detects constant stride access patterns. Requires two
 * consecutive accesses with the same stride to build confidence.
 * Used in Intel Core microarchitecture (IP prefetcher).
 * Baer & Chen, "An effective on-chip preloading scheme
 * to reduce data access penalty", SC 1991.
 */

void stride_record_access(StridePrefetcher *sp, uint32_t addr)
{
    int32_t current_stride = (int32_t)(addr - sp->last_addr);
    sp->last_addr = addr;

    if (current_stride == sp->last_stride && current_stride != 0) {
        sp->confidence++;
    } else {
        sp->last_stride = current_stride;
        sp->confidence = 0;
    }
}

bool stride_get_prefetch(StridePrefetcher *sp, uint32_t *addr)
{
    if (sp->confidence >= 1) {
        sp->prefetches_issued++;
        if (addr) *addr = (uint32_t)(sp->last_addr + sp->last_stride);
        return true;
    }
    return false;
}

/* ===== L3: Markov Prefetcher (Joseph & Grunwald 1997) =====
 * Builds a correlation graph of miss addresses. When address A
 * misses, prefetch the most frequent successor addresses.
 * Joseph & Grunwald, "Prefetching using Markov predictors",
 * ISCA 1997.
 */

void markov_record_miss(MarkovPrefetcher *mp, uint32_t prev_addr,
                        uint32_t curr_addr)
{
    (void)prev_addr;
    mp->key = curr_addr;

    /* Check if curr_addr is already a successor */
    for (int i = 0; i < mp->successor_count && i < 4; i++) {
        if (mp->successors[i] == curr_addr) {
            mp->frequencies[i]++;
            return;
        }
    }

    /* Find insertion point: maintain top-4 by frequency */
    if (mp->successor_count < 4) {
        mp->successors[mp->successor_count] = curr_addr;
        mp->frequencies[mp->successor_count] = 1;
        mp->successor_count++;
    } else {
        /* Replace least frequent successor */
        uint8_t min_freq = mp->frequencies[0];
        int min_idx = 0;
        for (int i = 1; i < 4; i++) {
            if (mp->frequencies[i] < min_freq) {
                min_freq = mp->frequencies[i];
                min_idx = i;
            }
        }
        mp->successors[min_idx] = curr_addr;
        mp->frequencies[min_idx] = 1;
    }
}

uint32_t markov_get_prefetch(MarkovPrefetcher *mp, uint32_t *addrs,
                              uint32_t max_count)
{
    uint32_t count = 0;
    for (int i = 0; i < mp->successor_count && count < max_count; i++) {
        if (mp->frequencies[i] > 0) {
            addrs[count++] = mp->successors[i];
            mp->prefetches_issued++;
        }
    }
    return count;
}

/* ===== L3: Reference Prediction Table (RPT) (Chen & Baer 1995) =====
 * Correlates PC with stride. On repeated patterns, predicts
 * the next address by adding the learned stride.
 * Chen & Baer, "Effective hardware-based data prefetching
 * for high-performance processors", IEEE TC 1995.
 */

static uint32_t rpt_hash(uint32_t pc)
{
    return (pc >> 2) & (PREFETCH_RPT_SIZE - 1);
}

void rpt_record_access(RPTPrefetcher *rpt, uint32_t pc, uint32_t addr)
{
    uint32_t idx = rpt_hash(pc);
    RPTEntry *e = &rpt->entries[idx];
    rpt->accesses++;

    if (!e->valid) {
        e->tag = pc >> 2;
        e->last_addr = addr;
        e->stride = 0;
        e->state = 0;
        e->valid = true;
        return;
    }

    if (e->tag != (pc >> 2)) {
        e->tag = pc >> 2;
        e->last_addr = addr;
        e->stride = 0;
        e->state = 0;
        return;
    }

    rpt->hits++;

    int32_t new_stride = (int32_t)(addr - e->last_addr);
    if (new_stride == e->stride) {
        /* Same stride: increment confidence state */
        if (e->state < 3) e->state++;
    } else {
        e->stride = new_stride;
        e->state = 0;
    }

    e->last_addr = addr;
}

bool rpt_get_prefetch(RPTPrefetcher *rpt, uint32_t pc, uint32_t *addr)
{
    uint32_t idx = rpt_hash(pc);
    RPTEntry *e = &rpt->entries[idx];

    if (e->valid && e->tag == (pc >> 2) && e->state >= 1) {
        rpt->prefetches_issued++;
        if (addr) *addr = (uint32_t)(e->last_addr + e->stride);
        return true;
    }
    return false;
}

/* ===== L8: GHB Prefetcher (Nesbit & Smith 2004) =====
 * Global History Buffer tracks delta correlations in a
 * circular buffer with linked-list pointers.
 * Nesbit & Smith, "Data Cache Prefetching Using a Global
 * History Buffer", IEEE Micro 2004.
 */

void ghb_record_access(GHBPrefetcher *ghb, uint32_t addr)
{
    uint32_t idx = ghb->tail;
    GHBEntry *e = &ghb->ghb[idx];

    if (ghb->count > 0) {
        GHBEntry *prev = &ghb->ghb[(idx - 1 + PREFETCH_GHB_SIZE) % PREFETCH_GHB_SIZE];
        e->delta = (int32_t)(addr - prev->address);
    } else {
        e->delta = 0;
    }

    e->address = addr;
    e->next_idx = (idx + 1) % PREFETCH_GHB_SIZE;

    ghb->tail = (ghb->tail + 1) % PREFETCH_GHB_SIZE;
    if (ghb->count < PREFETCH_GHB_SIZE)
        ghb->count++;
    else
        ghb->head = (ghb->head + 1) % PREFETCH_GHB_SIZE;
}

bool ghb_get_prefetch(GHBPrefetcher *ghb, uint32_t *addr)
{
    if (ghb->count < 2) return false;

    /* Simplest delta correlation: repeat the most recent delta */
    uint32_t last_idx = (ghb->tail - 1 + PREFETCH_GHB_SIZE) % PREFETCH_GHB_SIZE;
    int32_t last_delta = ghb->ghb[last_idx].delta;
    uint32_t last_addr  = ghb->ghb[last_idx].address;

    if (last_delta != 0) {
        ghb->prefetches_issued++;
        if (addr) *addr = (uint32_t)(last_addr + last_delta);
        return true;
    }
    return false;
}

/* ===== L8: SMS Prefetcher (Somogyi et al. 2006) =====
 * Spatial Memory Streaming: records spatial access patterns
 * triggered by a PC+offset. On subsequent triggers, replays
 * the pattern as prefetches.
 */

void sms_record_access(SMSPrefetcher *sms, uint32_t pc, uint32_t addr)
{
    uint32_t region_key = (pc >> 2) & (PREFETCH_SMS_REGIONS - 1);
    SpatialPattern *sp = &sms->patterns[region_key];

    if (sp->trigger_pc != pc) {
        /* New pattern: reset */
        sp->trigger_pc = pc;
        sp->trigger_offset = addr & 0x3F; /* Cache line offset */
        sp->delta_count = 0;
        sp->frequency = 0;
    }

    /* Record delta relative to trigger */
    if (sp->delta_count < 8) {
        int32_t delta = (int32_t)((addr & ~0x3F) - (sp->trigger_offset));
        /* Check if delta already recorded */
        bool found = false;
        for (int i = 0; i < sp->delta_count; i++) {
            if (sp->deltas[i] == delta) { found = true; break; }
        }
        if (!found) {
            sp->deltas[sp->delta_count++] = delta;
        }
    }
    sp->frequency++;
}

bool sms_get_prefetch(SMSPrefetcher *sms, uint32_t pc, uint32_t *addr)
{
    for (int i = 0; i < PREFETCH_SMS_REGIONS; i++) {
        SpatialPattern *sp = &sms->patterns[i];
        if (sp->trigger_pc == pc && sp->frequency > 0) {
            sms->prefetches_issued++;
            if (addr) *addr = pc + (uint32_t)sp->deltas[0];
            return true;
        }
    }
    return false;
}

/* ===== L5: Unified Prefetch Controller ===== */

void pf_init(PrefetchController *pf, PrefetcherType type)
{
    memset(pf, 0, sizeof(*pf));
    pf->type = type;
}

bool pf_request_prefetch(PrefetchController *pf, uint32_t miss_addr,
                          uint32_t miss_pc)
{
    pf->total_demand_misses++;

    uint32_t pf_addr = 0;
    bool issued = false;

    switch (pf->type) {
    case PREF_NEXT_LINE:
        nextline_record_miss(&pf->next_line, miss_addr);
        issued = nextline_get_prefetch(&pf->next_line, &pf_addr);
        break;
    case PREF_STRIDE:
        stride_record_access(&pf->stride, miss_addr);
        issued = stride_get_prefetch(&pf->stride, &pf_addr);
        break;
    case PREF_RPT:
        rpt_record_access(&pf->rpt, miss_pc, miss_addr);
        issued = rpt_get_prefetch(&pf->rpt, miss_pc, &pf_addr);
        break;
    case PREF_GHB:
        ghb_record_access(&pf->ghb, miss_addr);
        issued = ghb_get_prefetch(&pf->ghb, &pf_addr);
        break;
    default: break;
    }

    if (issued && pf_addr != 0) {
        /* Enqueue prefetch */
        if (pf->queue.count < PREFETCH_QUEUE_SIZE) {
            PrefetchRequest *req = &pf->queue.queue[pf->queue.tail];
            req->address = pf_addr;
            req->issued = true;
            req->used = false;
            req->timestamp = pf->total_demand_misses;
            pf->queue.tail = (pf->queue.tail + 1) % PREFETCH_QUEUE_SIZE;
            pf->queue.count++;
            pf->queue.total_issued++;
        }
    }

    return issued;
}

bool pf_get_prefetch(PrefetchController *pf, uint32_t *addr)
{
    if (pf->queue.count == 0) return false;

    PrefetchRequest *req = &pf->queue.queue[pf->queue.head];
    *addr = req->address;
    pf->queue.head = (pf->queue.head + 1) % PREFETCH_QUEUE_SIZE;
    pf->queue.count--;
    return true;
}

void pf_notify_use(PrefetchController *pf, uint32_t addr)
{
    /* Mark matching prefetch as used */
    for (uint32_t i = 0; i < PREFETCH_QUEUE_SIZE; i++) {
        if (pf->queue.queue[i].address == addr && pf->queue.queue[i].issued) {
            if (!pf->queue.queue[i].used) {
                pf->queue.queue[i].used = true;
                pf->queue.total_used++;
                pf->total_prefetch_hits++;
            }
            return;
        }
    }
    /* If not found, this prefetch was never issued - count as wasted */
    (void)addr;
}

void pf_print_stats(const PrefetchController *pf)
{
    printf("=== Prefetch Controller Statistics ===\n");
    printf("  Type: ");
    switch (pf->type) {
    case PREF_NEXT_LINE: printf("Next-Line (Smith 1982)\n"); break;
    case PREF_STRIDE:    printf("Stride (Baer&Chen 1991)\n"); break;
    case PREF_MARKOV:    printf("Markov (Joseph&Grunwald 1997)\n"); break;
    case PREF_RPT:       printf("RPT (Chen&Baer 1995)\n"); break;
    case PREF_GHB:       printf("GHB (Nesbit&Smith 2004)\n"); break;
    case PREF_SMS:       printf("SMS (Somogyi et al. 2006)\n"); break;
    default: printf("None\n"); break;
    }
    printf("  Demand misses:      %llu\n", (unsigned long long)pf->total_demand_misses);
    printf("  Prefetches issued:  %llu\n", (unsigned long long)pf->queue.total_issued);
    printf("  Prefetches used:    %llu\n", (unsigned long long)pf->queue.total_used);
    printf("  Prefetch hits:      %llu\n", (unsigned long long)pf->total_prefetch_hits);
    if (pf->queue.total_issued > 0)
        printf("  Accuracy:           %.1f%%\n",
               pf_coverage(pf) * 100.0);
    printf("========================================\n");
}

double pf_coverage(const PrefetchController *pf)
{
    if (pf->total_demand_misses == 0) return 0.0;
    return (double)pf->total_prefetch_hits / (double)pf->total_demand_misses;
}

double pf_accuracy(const PrefetchController *pf)
{
    if (pf->queue.total_issued == 0) return 0.0;
    return (double)pf->queue.total_used / (double)pf->queue.total_issued;
}

double pf_timeliness(const PrefetchController *pf)
{
    if (pf->queue.total_issued == 0) return 0.0;
    return (double)pf->queue.total_used / (double)pf->queue.total_issued;
}
