#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Memory memory_create(uint32_t size) {
    Memory mem; memset(&mem, 0, sizeof(mem));
    mem.size = size; mem.read_count = 0; mem.write_count = 0; return mem;
}
void memory_write(Memory* mem, uint32_t addr, uint32_t value) {
    if (!mem || addr >= mem->size) return;
    uint32_t idx = addr / MEM_WORD_SIZE;
    if (idx < MEM_MAX_ADDR / MEM_WORD_SIZE) { mem->data[idx] = value; mem->write_count++; }
}
uint32_t memory_read(const Memory* mem, uint32_t addr) {
    if (!mem || addr >= mem->size) return 0;
    uint32_t idx = addr / MEM_WORD_SIZE;
    if (idx < MEM_MAX_ADDR / MEM_WORD_SIZE) {
        ((Memory*)mem)->read_count++;
        return mem->data[idx];
    }
    return 0;
}
void memory_reset(Memory* mem) { if (mem) { memset(mem->data, 0, sizeof(mem->data)); mem->read_count = 0; mem->write_count = 0; } }
void memory_dump(const Memory* mem, uint32_t start, uint32_t count) {
    if (!mem) return;
    printf("Memory dump [0x%x - 0x%x]:\n", start, start + count * MEM_WORD_SIZE);
    for (uint32_t i = 0; i < count && (start + i * MEM_WORD_SIZE) < mem->size; i++)
        printf("  0x%08x: 0x%08x\n", start + i * MEM_WORD_SIZE, memory_read(mem, start + i * MEM_WORD_SIZE));
}

Cache cache_create(CacheType type, WritePolicy wp, int sets, int ways, int line_size) {
    Cache c; memset(&c, 0, sizeof(c)); c.type = type; c.write_policy = wp;
    c.num_sets = (sets > CACHE_MAX_SETS) ? CACHE_MAX_SETS : sets;
    if (c.num_sets < 1) c.num_sets = 1;
    c.num_ways = (ways > CACHE_MAX_WAYS) ? CACHE_MAX_WAYS : ways;
    if (c.num_ways < 1) c.num_ways = 1;
    c.line_size = line_size;
    c.sets = (CacheSet*)calloc(c.num_sets, sizeof(CacheSet));
    if (!c.sets) { c.num_sets = 0; return c; }
    for (int s = 0; s < c.num_sets; s++)
        for (int w = 0; w < c.num_ways; w++) {
            c.sets[s].lines[w].valid = false; c.sets[s].lines[w].dirty = false;
            c.sets[s].lines[w].tag = 0; c.sets[s].lines[w].last_access = 0;
        }
    return c;
}
bool cache_read(Cache* c, uint32_t addr, uint8_t* data_out) {
    if (!c || !data_out) return false;
    c->accesses++;
    uint32_t tag = addr_tag(addr, c->num_sets, c->line_size);
    uint32_t set = addr_set(addr, c->num_sets, c->line_size);
    uint32_t off = addr_offset(addr, c->line_size);
    if (set >= (uint32_t)c->num_sets) return false;
    for (int w = 0; w < c->num_ways; w++) {
        if (c->sets[set].lines[w].valid && c->sets[set].lines[w].tag == tag) {
            c->hits++; c->sets[set].lines[w].last_access = c->accesses;
            memcpy(data_out, &c->sets[set].lines[w].data[off], MEM_WORD_SIZE); return true;
        }
    }
    c->misses++; return false;
}
bool cache_write(Cache* c, uint32_t addr, const uint8_t* data_in) {
    if (!c || !data_in) return false;
    c->accesses++;
    uint32_t tag = addr_tag(addr, c->num_sets, c->line_size);
    uint32_t set = addr_set(addr, c->num_sets, c->line_size);
    uint32_t off = addr_offset(addr, c->line_size);
    if (set >= (uint32_t)c->num_sets) return false;
    for (int w = 0; w < c->num_ways; w++) {
        if (c->sets[set].lines[w].valid && c->sets[set].lines[w].tag == tag) {
            c->hits++;
            memcpy(&c->sets[set].lines[w].data[off], data_in, MEM_WORD_SIZE);
            c->sets[set].lines[w].dirty = (c->write_policy == WRITE_BACK);
            c->sets[set].lines[w].last_access = c->accesses;
            if (c->write_policy == WRITE_BACK) c->write_backs++;
            return true;
        }
    }
    c->misses++;
    int lru_way = 0; int oldest = c->sets[set].lines[0].last_access;
    for (int w = 1; w < c->num_ways; w++) {
        if (!c->sets[set].lines[w].valid) { lru_way = w; break; }
        if (c->sets[set].lines[w].last_access < oldest) { oldest = c->sets[set].lines[w].last_access; lru_way = w; }
    }
    if (c->sets[set].lines[lru_way].valid && c->sets[set].lines[lru_way].dirty) { c->evictions++; c->write_backs++; }
    c->sets[set].lines[lru_way].valid = true;
    c->sets[set].lines[lru_way].dirty = (c->write_policy == WRITE_BACK);
    c->sets[set].lines[lru_way].tag = tag;
    c->sets[set].lines[lru_way].last_access = c->accesses;
    memcpy(&c->sets[set].lines[lru_way].data[off], data_in, MEM_WORD_SIZE);
    return true;
}
double cache_hit_rate(const Cache* c) { return (c && c->accesses > 0) ? (double)c->hits / c->accesses : 0.0; }
void cache_stats(const Cache* c, int* hits, int* misses, int* evictions, int* wb) {
    if (!c) return;
    if (hits) *hits = c->hits;
    if (misses) *misses = c->misses;
    if (evictions) *evictions = c->evictions;
    if (wb) *wb = c->write_backs;
}
void cache_invalidate(Cache* c, uint32_t addr) {
    if (!c) return;
    uint32_t tag = addr_tag(addr, c->num_sets, c->line_size);
    uint32_t set = addr_set(addr, c->num_sets, c->line_size);
    if (set >= (uint32_t)c->num_sets) return;
    for (int w = 0; w < c->num_ways; w++)
        if (c->sets[set].lines[w].valid && c->sets[set].lines[w].tag == tag) {
            if (c->sets[set].lines[w].dirty) c->write_backs++;
            c->sets[set].lines[w].valid = false; c->sets[set].lines[w].dirty = false;
        }
}
void cache_flush(Cache* c) {
    if (!c) return;
    for (int s = 0; s < c->num_sets; s++)
        for (int w = 0; w < c->num_ways; w++) {
            if (c->sets[s].lines[w].dirty) c->write_backs++;
            c->sets[s].lines[w].valid = false; c->sets[s].lines[w].dirty = false;
        }
}
void cache_free(Cache* c) { if (c) { free(c->sets); c->sets = NULL; } }

TLB tlb_create(int num_entries) {
    TLB t; memset(&t, 0, sizeof(t));
    t.num_entries = (num_entries > TLB_MAX_ENTRIES) ? TLB_MAX_ENTRIES : num_entries;
    if (t.num_entries < 1) t.num_entries = 1;
    for (int i = 0; i < t.num_entries; i++) t.entries[i].state = TLB_INVALID;
    return t;
}
bool tlb_lookup(TLB* tlb, uint32_t vpn, uint32_t* ppn) {
    if (!tlb) return false;
    for (int i = 0; i < tlb->num_entries; i++) {
        if (tlb->entries[i].state != TLB_INVALID && tlb->entries[i].vpn == vpn) {
            tlb->hits++; tlb->entries[i].last_access = tlb->hits + tlb->misses;
            if (ppn) *ppn = tlb->entries[i].ppn;
            return true;
        }
    }
    tlb->misses++; return false;
}
void tlb_insert(TLB* tlb, uint32_t vpn, uint32_t ppn) {
    if (!tlb) return;
    int lru = 0; int oldest = tlb->entries[0].last_access;
    for (int i = 1; i < tlb->num_entries; i++) {
        if (tlb->entries[i].state == TLB_INVALID) { lru = i; break; }
        if (tlb->entries[i].last_access < oldest) { oldest = tlb->entries[i].last_access; lru = i; }
    }
    tlb->entries[lru].vpn = vpn; tlb->entries[lru].ppn = ppn;
    tlb->entries[lru].state = TLB_VALID;
    tlb->entries[lru].last_access = tlb->hits + tlb->misses;
}
void tlb_invalidate(TLB* tlb, uint32_t vpn) {
    if (!tlb) return;
    for (int i = 0; i < tlb->num_entries; i++)
        if (tlb->entries[i].vpn == vpn) tlb->entries[i].state = TLB_INVALID;
}
void tlb_flush(TLB* tlb) { if (tlb) for (int i = 0; i < tlb->num_entries; i++) tlb->entries[i].state = TLB_INVALID; }
double tlb_hit_rate(const TLB* tlb) {
    if (!tlb) return 0.0;
    int total = tlb->hits + tlb->misses;
    return total > 0 ? (double)tlb->hits / total : 0.0;
}

uint32_t addr_tag(uint32_t addr, int num_sets, int line_size) {
    int set_bits = 0, n = num_sets - 1; while (n > 0) { set_bits++; n >>= 1; }
    int off_bits = 0; n = line_size - 1; while (n > 0) { off_bits++; n >>= 1; }
    return addr >> (set_bits + off_bits);
}
uint32_t addr_set(uint32_t addr, int num_sets, int line_size) {
    int off_bits = 0, n = line_size - 1; while (n > 0) { off_bits++; n >>= 1; }
    int set_bits = 0; int m = num_sets - 1; while (m > 0) { set_bits++; m >>= 1; }
    return (addr >> off_bits) & ((1u << set_bits) - 1);
}
uint32_t addr_offset(uint32_t addr, int line_size) { return addr & (line_size - 1); }
