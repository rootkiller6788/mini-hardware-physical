# FTL Simulator — Flash Translation Layer Simulator

## Overview

The Flash Translation Layer (FTL) is the core firmware component inside every SSD that makes NAND flash appear as a block device. NAND flash has fundamentally different characteristics from HDDs: pages must be erased before rewriting, erase operates at block granularity (much larger than write), and cells wear out after limited program/erase cycles. The FTL hides these complexities behind a standard logical block addressing interface.

This simulator implements a complete FTL in C99 with configurable address mapping, write pointer management, and performance statistics tracking. It models the full hierarchy: FlashPlane → FlashBlock → FlashPage, supporting MLC/TLC page types and all page states (FREE, VALID, INVALID, BAD).

## Theory

### NAND Flash Organization

```
FlashPlane[0]  FlashPlane[1]  FlashPlane[2]  FlashPlane[3]
  |
  +-- FlashBlock[0..63]       (64 blocks per plane)
        |
        +-- FlashPage[0..127]  (128 pages per block, 4096 bytes each)

Total: 4 planes × 64 blocks × 128 pages = 32,768 physical pages
```

Each page has:
- **Type**: LOWER, MIDDLE, UPPER (for TLC NAND where each cell stores 3 bits)
- **State**: FREE → VALID → INVALID (no in-place update, must write to new page)
- **Data**: 4096-byte payload

### Address Mapping

The core problem FTL solves: **Logical Block Address (LBA) → Physical Page translation**.

#### Page-Level Mapping

```
LBA ──→ mapping_table[lba] ──→ physical_page_number
                                    │
                                    └──→ FlashPlane[plane].blocks[blk].pages[pg]
```

- **Advantage**: Most flexible, any LBA can map to any physical page
- **Disadvantage**: Mapping table size = LBAs × 4 bytes = 64KB for 16K LBAs
- **Use case**: Modern high-end SSDs with large DRAM cache

#### Block-Level Mapping

```
LBA ──→ logical_block = lba / pages_per_block
        page_offset   = lba % pages_per_block
        physical_block = mapping_table[logical_block]
        physical_page  = physical_block * pages_per_block + page_offset
```

- **Advantage**: Tiny mapping table (only one entry per block)
- **Disadvantage**: Entire blocks must be contiguous; poor random write performance
- **Use case**: Early SSDs, low-cost embedded flash

#### Hybrid FTL

Combines page-level mapping for a small "log buffer" area with block-level mapping for the bulk of data. The log buffer absorbs random writes and asynchronously merges them back into block-mapped regions.

```
Host Write → Log Buffer (page-mapped, ~3% of total blocks)
                │
                └──→ Merge/Garbage Collection → Data Blocks (block-mapped)
```

### Log Buffer Approaches

#### BAST (Block Associative Sector Translation)

Each block-mapped data block has a dedicated log block. When a logical page is overwritten, the new data goes to the corresponding log block.

```
Data Block 0 ──→ Log Block 0  (8KB writes to block 0 go here)
Data Block 1 ──→ Log Block 1
...
```

**Pros**: Simple, predictable mapping
**Cons**: Poor utilization if only a few pages per block are updated

#### FAST (Fully Associative Sector Translation)

All log blocks are shared. Any overwrite goes to any available log block. A separate table tracks which log pages correspond to which data block pages.

```
Data Blocks ──→ Log Block Pool (fully associative)
  0..N            │
                  └──→ Each log page: { data_block, page_offset }
```

**Pros**: Much better log block utilization
**Cons**: Merge/switching operations more complex

### Program/Erase Asymmetry

| Operation | Granularity | Latency (approx) |
|-----------|------------|-------------------|
| Read      | Page       | 50 μs            |
| Program   | Page       | 900 μs           |
| Erase     | Block      | 3,000 μs         |

This asymmetry drives all SSD design decisions. The FTL must:
1. Never erase on the critical path of a host write
2. Maintain a pool of pre-erased free blocks
3. Defer erases to background garbage collection

### Write Amplification

```
Write Amplification (WA) = (NAND Writes + GC Writes) / Host Writes
```

Every host write causes at least 1 NAND write. But garbage collection adds additional writes when valid pages must be copied before erasing a block. If half of a victim block's pages are still valid, each erased block gives us 64 free pages at the cost of 64 GC page copies → WA = 2.0.

**Key insight**: WA decreases as over-provisioning increases and as write patterns become more sequential.

### Logical-to-Physical Mapping Evolution

```
State 1: After sequential writes to LBA 0-3
  LBA 0 → PhysPage 0  [VALID]
  LBA 1 → PhysPage 1  [VALID]
  LBA 2 → PhysPage 2  [VALID]
  LBA 3 → PhysPage 3  [VALID]

State 2: After overwriting LBA 1
  LBA 1 → PhysPage 4  [VALID]     <-- new mapping
  PhysPage 1 → INVALID            <-- stale data
  LBA 0 → PhysPage 0  [VALID]
  LBA 2 → PhysPage 2  [VALID]
  LBA 3 → PhysPage 3  [VALID]

State 3: After TRIM LBA 2
  LBA 2 → UNMAPPED
  PhysPage 2 → INVALID

State 4: After GC on block 0 (containing pages 0-127)
  Pages 0 and 3 are VALID → copied to new block
  Block 0 erased → all pages become FREE
  mapping_table updated for LBAs 0 and 3
```

## Implementation

### Header: `include/ftl.h`

```c
typedef struct {
    uint64_t reads;
    uint64_t writes;
    uint64_t erases;
    uint64_t gc_moves;
    uint64_t host_writes;
} FTLStats;

typedef struct {
    FlashPlane     planes[FTL_PLANES];         // 4 planes
    int32_t        mapping_table[FTL_MAX_LBAS]; // logical→physical
    int32_t        reverse_map[FTL_MAX_PHYSICAL_PAGES]; // physical→logical
    uint32_t       write_pointer;
    uint32_t       free_pages;
    FTLMappingMode mapping_mode;
    FTLStats       stats;
} FTL;
```

### Source: `src/ftl.c`

The implementation provides:
- `ftl_init()`: Initializes all mapping arrays to -1 (unmapped)
- `ftl_read()`: Follows LBA → physical page → reads data, returns zeros for unmapped
- `ftl_write()`: Invalidates old mapping, finds next free page, writes data, updates mapping
- `ftl_trim()`: Marks page as INVALID, removes mapping entry
- `ftl_print_stats()`: Reports reads/writes/erases/GC moves and computed write amplification

### Key Helper: Physical Page → Hardware Coordinates

```c
static uint32_t block_index_from_page(uint32_t physical_page) {
    return physical_page / FTL_PAGES_PER_BLOCK;
}
static uint32_t plane_index_from_page(uint32_t physical_page) {
    return block_index_from_page(physical_page) / FTL_BLOCKS_PER_PLANE;
}
static uint32_t block_in_plane(uint32_t physical_page) {
    return block_index_from_page(physical_page) % FTL_BLOCKS_PER_PLANE;
}
static uint32_t page_in_block(uint32_t physical_page) {
    return physical_page % FTL_PAGES_PER_BLOCK;
}
```

## Demo: `examples/ftl_demo.c`

### Expected Output

```
=== FTL Demo: Flash Translation Layer Simulator ===

[1] FTL initialized with page-level mapping
    Max LBAs: 16384, Max Physical Pages: 65536

[2] Sequential Write: Writing LBAs 0-9
    Written LBA 0
    ...
    Written LBA 9

[3] Sequential Read: Reading LBAs 0-9
    LBA 0: first byte = 0xA0 (expected 0xA0)  OK
    ...

[4] Overwrite: Rewrite LBAs 0-4 with new data
    Overwritten LBA 0 (old page now INVALID)
    ...

[5] TRIM Operation: Trim LBAs 5-9
    Trimmed LBA 5
    ...

[6] Write Amplification Demo: Random writes to LBAs 0-9
    100 random writes: 100 physical writes (WA=1.00)

FTL Statistics:
  Mode:             PAGE_LEVEL
  Host Reads:       10
  Host Writes:      115
  Total Writes:     115
  Erases:           0
  GC Moves:         0
  Valid Pages:      10
  Free Pages:       65526
  Invalid Pages:    110
  Write Amplification: 1.00
```

## Building

```bash
make ftl_demo
./bin/ftl_demo
```

## References

- Gupta et al., "DFTL: A Flash Translation Layer Employing Demand-based Selective Caching", ASPLOS 2009
- Lee et al., "LAST: Locality-Aware Sector Translation for NAND Flash Memory-Based Storage Systems", ACM SIGOPS 2008
- Kim et al., "A Space-Efficient Flash Translation Layer for CompactFlash Systems", IEEE TCE 2002
- CMU 18-746: Flash Memory and FTL Design (Chapters 13-17)
