# FTL Internals — Flash Translation Layer Design

## Overview

The Flash Translation Layer (FTL) is the most critical firmware component in NAND flash storage. It provides a block device abstraction on top of flash memory, which cannot be written in-place and must be erased at block granularity before programming.

## Address Mapping Granularity

### Page-Level Mapping

Every logical page maps independently to any physical page.

```
Mapping Table: LBA → Physical Page Number
  LBA 0    → PPN 127
  LBA 1    → PPN 45
  LBA 2    → PPN 890
  ...
  LBA 16383 → PPN 12340
```

| Aspect | Value |
|--------|-------|
| Table size | 16384 entries × 4 bytes = 64 KB |
| Flexibility | Maximum — any LBA to any physical page |
| Random write performance | Excellent — no merge overhead |
| GC complexity | Low — just invalidate old page |
| RAM requirement | Must be in RAM (or cached) for performance |

### Block-Level Mapping

Logical block number maps to physical block number. Offset within block is fixed.

```
Mapping Table: Logical Block → Physical Block
  LBN 0 → PBN 3  (pages 0-127 at physical block 3)
  LBN 1 → PBN 7  (pages 128-255 at physical block 7)
  ...

Addressing: physical_page = mapping[LBA / 128] * 128 + (LBA % 128)
```

| Aspect | Value |
|--------|-------|
| Table size | 128 entries × 4 bytes = 512 B (much smaller!) |
| Flexibility | Low — pages within block must stay contiguous |
| Random write performance | Poor — small overwrite requires entire block copy |
| GC complexity | High — must merge new data with old valid pages |

### Hybrid Mapping (BAST)

A small pool of log blocks (page-mapped) absorb random writes. Data blocks use block-level mapping.

```
Data Blocks  (block-mapped):   LBN → PBN
Log Blocks   (page-mapped):    LBA → PPN  (within log block pool)

Write path:
  1. Write to log block (page-mapped → fast)
  2. When log block fills → merge into data block (block-mapped)
  3. Erase now-empty log block for reuse
```

| Aspect | Value |
|--------|-------|
| Table size | Data: 512 B + Log: ~2-5% of total pages |
| Random write performance | Good while log blocks available |
| Merge cost | Periodic full-merge operations needed |

### Hybrid Mapping (FAST)

All log blocks form a shared pool. Any write goes to any available log block.

| Aspect | Value |
|--------|-------|
| Log utilization | Better than BAST (any page can go anywhere) |
| Merge complexity | Higher — must track which data block each log page belongs to |
| Typical ratio | 3-5% of blocks as log blocks |

## Log-Structured FTL

The FTL operates like a log-structured file system:

1. **Incoming writes** append to the current write pointer (next free physical page)
2. **Old page** is invalidated (stale data remains until GC)
3. **Write pointer** advances sequentially through physical pages
4. **When free pages run low** → garbage collection kicks in

```
Physical Pages (sequential view):

[V][V][I][I][V][V][F][F][F][V][I][V][F]...
 ↑                  ↑
 valid        invalid     free

Write pointer advances → writes consume free pages
GC erases blocks with many invalid pages → creates more free pages
```

## Mapping Table Caching

For large SSDs (1TB+), the full page-level mapping table can be gigabytes:

- 1TB SSD, 4KB pages → 256M entries × 4 bytes = 1 GB mapping table
- DRAM cost: ~$5-10 for 1GB DDR4
- Can't fit entire table in SRAM (1-2 MB typical)

### Demand-Based Caching (DFTL)

Only cache frequently-accessed mapping entries in SRAM:
- **Cached Mapping Table (CMT)**: Small SRAM cache of hot entries
- **Global Translation Directory (GTD)**: Persistent in NAND, maps LBA ranges to NAND pages containing the mapping entries
- On cache miss: read GTD → issue read for mapping page → load into CMT

### Cache Hit Ratio

For typical workloads:
- Sequential I/O: Very high hit ratio (mapping entries prefetched)
- Random I/O with locality: >95% with 1MB cache
- Pure random across full LBA range: Depends on cache size / total mapping size

## Wear Leveling Integration

The FTL's block allocation interacts with wear leveling:

1. **Free block selection**: FTL asks wear leveler for a block with low erase count
2. **Cold/hot separation**: FTL may route hot writes to high-wear blocks to slow further wear
3. **Static wear leveling**: FTL cooperates by migrating data between blocks when the wear leveler requests it

## Write Amplification Analysis

Write Amplification (WA) depends on:

### Factors Increasing WA

| Factor | Impact | Explanation |
|--------|--------|-------------|
| Random writes | 2-10x | Pages in block have mixed validity |
| Low over-provisioning | Higher WA | Fewer free blocks means GC runs more |
| Small writes | Higher WA | More partial-block invalidations |
| High write rate | Higher WA | GC can't keep up, more fragmented |

### Factors Decreasing WA

| Factor | Impact | Explanation |
|--------|--------|-------------|
| Sequential writes | ~1x | All pages in block invalidated together |
| TRIM/Discard | Lower WA | Controller can treat trimmed pages as invalid |
| High over-provisioning | Lower WA | More headroom for write pointer |
| Write coalescing | Lower WA | Merge small writes into full-page writes |

### WA Formula

```
WA = Total NAND Writes / Host Writes
   = (Host Writes + GC Copy Writes) / Host Writes
   = 1 + (GC Copy Writes / Host Writes)
```

For a drive with no GC overhead: WA = 1.0
For a drive constantly running GC: WA = 2.0 to 10.0+

## Implementation Notes (this module)

Our FTL implementation (`src/ftl.c`) strikes a balance between realism and simplicity:

- **Full page-level mapping** as default (simplest to reason about)
- **Write pointer** loops around to simulate real behavior
- **No GC integration** in FTL itself — GC is a separate module that calls `ftl_write` and `ftl_trim`
- **Statistics tracking** for reads, writes, erases, GC moves, and WA computation

## References

- Gupta et al., "DFTL: A Flash Translation Layer Employing Demand-based Selective Caching of Page-level Address Mappings", ASPLOS 2009
- Kim et al., "A Space-Efficient Flash Translation Layer for CompactFlash Systems", IEEE Trans. Consumer Electronics, 2002
- Lee et al., "LAST: Locality-Aware Sector Translation for NAND Flash Memory-Based Storage Systems", ACM SIGOPS 2008
- Park et al., "A Reconfigurable FTL Architecture for NAND Flash-Based Applications", ACM TECS 2008
