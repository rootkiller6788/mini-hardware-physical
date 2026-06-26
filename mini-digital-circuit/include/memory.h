#ifndef MEMORY_H
#define MEMORY_H
#include <stdbool.h>
#include <stdint.h>

#define MEM_MAX_ADDR 65536
#define MEM_WORD_SIZE 4
#define CACHE_LINE_SIZE 64
#define CACHE_MAX_SETS 64
#define CACHE_MAX_WAYS 4
#define TLB_MAX_ENTRIES 32

typedef struct {
    uint32_t data[MEM_MAX_ADDR / MEM_WORD_SIZE];
    uint32_t size;
    int read_count;
    int write_count;
} Memory;

typedef enum { CACHE_DIRECT, CACHE_SET_ASSOC, CACHE_FULLY_ASSOC } CacheType;
typedef enum { WRITE_THROUGH, WRITE_BACK } WritePolicy;

typedef struct {
    bool valid;
    bool dirty;
    uint32_t tag;
    uint8_t data[CACHE_LINE_SIZE];
    int last_access;
} CacheLine;

typedef struct { CacheLine lines[CACHE_LINE_SIZE]; } CacheSet;

typedef struct {
    CacheType type;
    WritePolicy write_policy;
    int num_sets;
    int num_ways;
    int line_size;
    CacheSet* sets;
    int hits;
    int misses;
    int accesses;
    int evictions;
    int write_backs;
} Cache;

typedef enum { TLB_INVALID, TLB_VALID, TLB_DIRTY } TLBEntryState;

typedef struct {
    uint32_t vpn;
    uint32_t ppn;
    TLBEntryState state;
    int last_access;
} TLBEntry;

typedef struct {
    TLBEntry entries[TLB_MAX_ENTRIES];
    int num_entries;
    int hits;
    int misses;
} TLB;

Memory memory_create(uint32_t size);
void memory_write(Memory* mem, uint32_t addr, uint32_t value);
uint32_t memory_read(const Memory* mem, uint32_t addr);
void memory_reset(Memory* mem);
void memory_dump(const Memory* mem, uint32_t start, uint32_t count);

Cache cache_create(CacheType type, WritePolicy wp, int sets, int ways, int line_size);
bool cache_read(Cache* c, uint32_t addr, uint8_t* data_out);
bool cache_write(Cache* c, uint32_t addr, const uint8_t* data_in);
double cache_hit_rate(const Cache* c);
void cache_stats(const Cache* c, int* hits, int* misses, int* evictions, int* wb);
void cache_invalidate(Cache* c, uint32_t addr);
void cache_flush(Cache* c);
void cache_free(Cache* c);

TLB tlb_create(int num_entries);
bool tlb_lookup(TLB* tlb, uint32_t vpn, uint32_t* ppn);
void tlb_insert(TLB* tlb, uint32_t vpn, uint32_t ppn);
void tlb_invalidate(TLB* tlb, uint32_t vpn);
void tlb_flush(TLB* tlb);
double tlb_hit_rate(const TLB* tlb);

uint32_t addr_tag(uint32_t addr, int num_sets, int line_size);
uint32_t addr_set(uint32_t addr, int num_sets, int line_size);
uint32_t addr_offset(uint32_t addr, int line_size);

#endif
