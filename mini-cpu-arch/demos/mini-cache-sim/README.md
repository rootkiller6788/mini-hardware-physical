# mini-cache-sim — CPU 缓存层次模拟

> Cache hierarchy for CPUs: L1 I-cache, L1 D-cache, L2 unified cache, associativity, replacement policies, write policies, and multi-level interactions.

---

## Overview / 概览

Caches bridge the growing gap between processor speed and memory latency. Modern CPUs have 3-4 levels of cache hierarchy with increasing size and latency at each level. This document explains cache design principles and presents simulation techniques.

### The Memory Wall / 内存墙

| Metric | CPU Register | L1 Cache | L2 Cache | L3 Cache | DRAM |
|--------|-------------|----------|----------|----------|------|
| Latency | 1 cycle | 3-5 cycles | 10-20 cycles | 30-50 cycles | 200-400 cycles |
| Size | ~1 KB | 32-64 KB | 256-512 KB | 8-32 MB | 8-64 GB |
| Bandwidth | ~100 GB/s | ~50 GB/s | ~25 GB/s | ~12 GB/s | ~10 GB/s |

---

## Architecture / 架构

### Cache Organization

```
Memory Address (32-bit):
+-----------+--------+-------+
|    Tag    | Index  | Offset|
+-----------+--------+-------+
   22 bits    8 bits   2 bits
```

Given:
- Block size = 4 bytes (2 bits offset)
- 256 sets (8 bits index)
- Remaining 22 bits = tag

### Cache Block Structure

```
+-------+----+----+----+----+
| Valid | Tag| Data (4 bytes) |
+-------+----+----+----+----+
  1 bit  22b  32 bits
```

### Associativity Types

```
Direct-Mapped (1-way):
   Set 0: [ V | Tag | Data ]
   Set 1: [ V | Tag | Data ]
   ...
   Set 255: [ V | Tag | Data ]

4-way Set-Associative:
   Set 0: Way 0 [ V | Tag | Data ]
          Way 1 [ V | Tag | Data ]
          Way 2 [ V | Tag | Data ]
          Way 3 [ V | Tag | Data ]
   Set 1: ...

Fully Associative:
   Single set, N ways, any block can go anywhere
```

### Cache Hierarchy Architecture

```
CPU Core
   |
   +-- L1 I-Cache (32KB, 8-way, 64B line, 4 cycle)
   |
   +-- L1 D-Cache (32KB, 8-way, 64B line, 4 cycle)
        |
        +-- L2 Unified Cache (256KB, 8-way, 64B line, 12 cycle)
             |
             +-- L3 Unified Cache (8MB, 16-way, 64B line, 40 cycle)
                  |
                  +-- DRAM (GBs, 200+ cycles)
```

---

## Implementation Steps / 实现步骤

### Step 1: Cache Line Structure

```c
#define CACHE_LINE_SIZE 64
#define NUM_SETS        256
#define NUM_WAYS        4

typedef struct {
    bool     valid;
    bool     dirty;
    uint32_t tag;
    uint8_t  data[CACHE_LINE_SIZE];
    uint32_t last_access;  // for LRU
} CacheLine;

typedef struct {
    CacheLine ways[NUM_WAYS];
} CacheSet;

typedef struct {
    CacheSet  sets[NUM_SETS];
    uint32_t  accesses;
    uint32_t  hits;
    uint32_t  misses;
    uint32_t  writebacks;
    uint8_t   associativity;
    uint8_t   line_size;
    uint8_t   index_bits;
} Cache;
```

### Step 2: Address Decomposition

```c
typedef struct {
    uint32_t tag;
    uint32_t index;
    uint32_t offset;
} CacheAddr;

CacheAddr cache_decode_addr(const Cache* cache, uint32_t addr) {
    CacheAddr ca;
    int offset_bits = cache->line_size == 64 ? 6 :
                      cache->line_size == 32 ? 5 : 4;
    int index_bits  = cache->index_bits;

    ca.offset = addr & ((1 << offset_bits) - 1);
    ca.index  = (addr >> offset_bits) & ((1 << index_bits) - 1);
    ca.tag    = addr >> (offset_bits + index_bits);
    return ca;
}
```

### Step 3: Cache Access (Read)

```c
bool cache_read(Cache* cache, uint32_t addr, uint32_t* data) {
    cache->accesses++;
    CacheAddr ca = cache_decode_addr(cache, addr);
    CacheSet* set = &cache->sets[ca.index];

    // Check for hit
    for (int w = 0; w < cache->associativity; w++) {
        CacheLine* line = &set->ways[w];
        if (line->valid && line->tag == ca.tag) {
            cache->hits++;
            line->last_access = cache->accesses;
            memcpy(data, &line->data[ca.offset], 4);
            return true;  // HIT
        }
    }

    // MISS: find victim
    cache->misses++;
    int victim = find_victim_way(set, cache->associativity);

    // Evict if dirty
    if (set->ways[victim].valid && set->ways[victim].dirty) {
        cache->writebacks++;
        writeback_line(&set->ways[victim], ca);
    }

    // Fetch from next level
    fetch_line(cache, addr, &set->ways[victim]);
    set->ways[victim].valid = true;
    set->ways[victim].tag = ca.tag;
    set->ways[victim].dirty = false;
    set->ways[victim].last_access = cache->accesses;

    memcpy(data, &set->ways[victim].data[ca.offset], 4);
    return false;  // MISS
}
```

### Step 4: Replacement Policies

```c
// LRU (Least Recently Used)
int find_victim_lru(CacheSet* set, int num_ways) {
    int victim = 0;
    uint32_t oldest = set->ways[0].last_access;
    for (int i = 1; i < num_ways; i++) {
        if (!set->ways[i].valid) return i;  // empty way
        if (set->ways[i].last_access < oldest) {
            oldest = set->ways[i].last_access;
            victim = i;
        }
    }
    return victim;
}

// Random replacement
int find_victim_random(CacheSet* set, int num_ways) {
    for (int i = 0; i < num_ways; i++) {
        if (!set->ways[i].valid) return i;
    }
    return rand() % num_ways;
}

// FIFO (First-In First-Out)
int find_victim_fifo(CacheSet* set, int num_ways, int* fifo_ptr) {
    for (int i = 0; i < num_ways; i++) {
        if (!set->ways[i].valid) return i;
    }
    int victim = *fifo_ptr;
    *fifo_ptr = (*fifo_ptr + 1) % num_ways;
    return victim;
}

// Pseudo-LRU (Tree-based)
// Uses a binary tree of bits to approximate LRU
int find_victim_plru(CacheSet* set, int num_ways, uint8_t* plru_bits) {
    // 4-way example: 3 plru bits
    // bit[0]=0 -> go left (way 0,1); bit[0]=1 -> go right (way 2,3)
    // bit[1]=0 -> way 0; bit[1]=1 -> way 1
    // bit[2]=0 -> way 2; bit[2]=1 -> way 3
    int idx = 0;
    int level = 0;
    while (level < 2) {  // log2(4) = 2 levels
        idx = (idx << 1) | 1;
        if (plru_bits[idx - 1] == 0) {
            // go left
        } else {
            idx++;
        }
        level++;
    }
    return idx - 3;  // way number
}
```

### Step 5: Write Policies

```c
// Write-Through: write to cache AND memory immediately
void cache_write_through(Cache* cache, uint32_t addr, uint32_t data) {
    CacheAddr ca = cache_decode_addr(cache, addr);
    CacheSet* set = &cache->sets[ca.index];

    // Write to cache (if present)
    for (int w = 0; w < cache->associativity; w++) {
        CacheLine* line = &set->ways[w];
        if (line->valid && line->tag == ca.tag) {
            memcpy(&line->data[ca.offset], &data, 4);
            cache->hits++;
            break;
        }
    }
    // Always write to next level memory
    write_memory(addr, data);
}

// Write-Back: write to cache only, mark dirty
void cache_write_back(Cache* cache, uint32_t addr, uint32_t data) {
    CacheAddr ca = cache_decode_addr(cache, addr);
    CacheSet* set = &cache->sets[ca.index];

    for (int w = 0; w < cache->associativity; w++) {
        CacheLine* line = &set->ways[w];
        if (line->valid && line->tag == ca.tag) {
            memcpy(&line->data[ca.offset], &data, 4);
            line->dirty = true;
            cache->hits++;
            return;
        }
    }

    // Write-allocate on miss
    cache->misses++;
    int victim = find_victim_lru(set, cache->associativity);
    if (set->ways[victim].valid && set->ways[victim].dirty) {
        cache->writebacks++;
        writeback_line(&set->ways[victim], ca);
    }
    fetch_line(cache, addr, &set->ways[victim]);
    memcpy(&set->ways[victim].data[ca.offset], &data, 4);
    set->ways[victim].valid = true;
    set->ways[victim].tag = ca.tag;
    set->ways[victim].dirty = true;
}

// Write-No-Allocate: on write miss, bypass cache
void cache_write_no_alloc(Cache* cache, uint32_t addr, uint32_t data) {
    CacheAddr ca = cache_decode_addr(cache, addr);
    CacheSet* set = &cache->sets[ca.index];

    for (int w = 0; w < cache->associativity; w++) {
        CacheLine* line = &set->ways[w];
        if (line->valid && line->tag == ca.tag) {
            memcpy(&line->data[ca.offset], &data, 4);
            line->dirty = true;
            cache->hits++;
            write_memory(addr, data);  // write-through
            return;
        }
    }
    cache->misses++;
    write_memory(addr, data);  // bypass cache
}
```

### Step 6: Performance Metrics

```c
void cache_print_stats(const Cache* cache) {
    double hit_rate = (cache->accesses > 0)
        ? 100.0 * (double)cache->hits / (double)cache->accesses
        : 0.0;
    double miss_rate = 100.0 - hit_rate;

    printf("--- Cache Statistics ---\n");
    printf("  Accesses:    %u\n", cache->accesses);
    printf("  Hits:        %u (%.2f%%)\n", cache->hits, hit_rate);
    printf("  Misses:      %u (%.2f%%)\n", cache->misses, miss_rate);
    printf("  Writebacks:  %u\n", cache->writebacks);

    // AMAT = HitTime + MissRate * MissPenalty
    double miss_penalty = 100.0;  // cycles to next level
    double hit_time = 2.0;
    double amat = hit_time + (miss_rate / 100.0) * miss_penalty;
    printf("  AMAT:        %.2f cycles\n", amat);
    printf("------------------------\n");
}
```

---

## Advanced Topics / 高级主题

### Multi-Level Cache Interactions

- **Inclusive**: L2 contains all L1 data (simpler coherence, wastes space)
- **Exclusive**: L2 contains only evicted L1 data (more efficient, complex)
- **NINE** (Non-Inclusive Non-Exclusive): Most common in modern CPUs

### Cache Coherence (Multi-Core)

- **MESI Protocol**: Modified, Exclusive, Shared, Invalid states
- **MOESI**: Adds Owned state for shared-dirty sharing
- **Directory-based**: Scales better than snooping for large core counts

### Prefetching

- **Next-line**: Always fetch adjacent cache line on miss
- **Stride**: Detect pattern (addr[n] - addr[n-1]) and prefetch ahead
- **Instruction-based**: Compiler inserts prefetch instructions

### Non-Blocking Caches

Allow multiple outstanding misses via Miss Status Holding Registers (MSHRs).
Critical for hiding memory latency in OOO processors.

---

## References / 参考

- Smith, A.J. (1982). "Cache Memories." ACM Computing Surveys.
- Hennessy & Patterson, "Computer Architecture: A Quantitative Approach", Chapter 2
- Hill & Smith (1989). "Evaluating Associativity in CPU Caches." IEEE TC
- Przybylski, "Cache and Memory Hierarchy Design" (1990)
- MIT 6.175 Cache Design Lab
- Stanford EE282 Memory Hierarchy Lecture

---

## Build & Run / 构建与运行

```bash
# Compile a simple cache simulator
gcc -Wall -Wextra -O2 -o bin/cache_sim src/cache_sim.c

# Run with a memory trace
./bin/cache_sim < trace.txt

# Expected output:
# Cache Simulation Results
# L1 I-Cache: 32KB, 8-way, hit_rate=99.1%
# L1 D-Cache: 32KB, 8-way, hit_rate=95.7%
# L2 Unified: 256KB, 8-way, hit_rate=88.2%
# AMAT: 5.2 cycles
```
