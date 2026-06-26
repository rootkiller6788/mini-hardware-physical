# Wear Leveler — NAND Flash Wear Leveling Algorithms

## Overview

NAND flash cells have a finite lifespan measured in Program/Erase Cycles (PEC). SLC typically survives ~100K cycles, MLC ~3-10K, TLC ~1-3K, and QLC ~100-1000. Without wear leveling, a small set of frequently-written blocks (e.g., filesystem metadata, journal) would burn out long before the rest of the drive, reducing the effective device lifetime dramatically.

Wear leveling distributes writes and erases evenly across all blocks, ensuring uniform wear of the flash medium. This module implements three algorithms: Dynamic (moves cold data to high-wear blocks), Static (swaps hot and cold data placements), and Hybrid (combines both approaches).

## Theory

### The Wear Problem

```
Without Wear Leveling:
Block 0:  ████████████████████████████████████ 50,000 erases (near death)
Block 1:  █                                               100 erases
Block 2:  ██                                              250 erases
...
Block 63: █                                                50 erases

Drive dies when Block 0 fails, even though 98% of blocks are barely used.
```

### Dynamic Wear Leveling

**Principle**: When selecting a block for a new write, prefer blocks with lower erase counts.

```
┌──────────────────────────────────────┐
│  Free Block List (sorted by erase count) │
├──────────────────────────────────────┤
│  Erase count: 10  ←─── select this one  │
│  Erase count: 50                    │
│  Erase count: 100                   │
│  Erase count: 1000 ←─── skip this     │
└──────────────────────────────────────┘
```

Dynamic wear leveling only applies to free blocks being freshly allocated. It applies "the long way around" — it avoids high-wear blocks rather than actively fixing the imbalance. Over time, blocks converge toward similar erase counts because low-erase-count blocks get used more.

**Algorithm**: Every time a new block is needed:
1. Scan free block list
2. Pick the block with the lowest erase count
3. If a high-wear block has fewer invalid pages than a low-wear block, may still prefer the latter

### Static Wear Leveling

**Principle**: Periodically move cold data (rarely written) from low-wear blocks to high-wear blocks, then use the freed low-wear block for hot data.

```
Before:
  Block A (hot data):  5000 erases → wear it down
  Block B (cold data): 10 erases  → barely touched

Static WL:
  1. Move cold data from Block B to Block A
  2. Mark Block A as containing "cold" data (won't be rewritten)
  3. Block B becomes available for hot data writes
  4. Result: both blocks converge toward equilibrium
```

**Trigger**: When `max_erase_count - min_erase_count > threshold` (typically 100-500 cycles).

### Hybrid Wear Leveling

Combines both approaches:
1. Use dynamic wear leveling for normal block allocation
2. Trigger static wear leveling when spread exceeds threshold
3. Static WL only applies to blocks containing "cold" data (identified by low write frequency)

### Erase Count Distribution Statistics

The `WearLevelStats` structure tracks:

| Metric | Formula | Meaning |
|--------|---------|---------|
| min    | minimum erase count | Best-case block condition |
| max    | maximum erase count | Worst-case block condition |
| avg    | Σ ec / n | Center of distribution |
| stddev | √(E[X²] - E[X]²) | Spread; low = good leveling |
| spread | max - min | Direct wear imbalance measure |

**Ideal state**: All blocks have erase counts within 10% of each other. Standard deviation approaching zero.

### Cold/Hot Data Separation

Critical for static wear leveling:

- **Hot data**: Frequently overwritten (filesystem journal, metadata, temp files). Written to blocks that can tolerate more cycles.
- **Cold data**: Rarely modified (archives, media files, OS binaries). Sits in blocks, preventing their use for wear leveling.

Identifying hot/cold data:
1. Count write frequency per logical block
2. Age-based: data that hasn't been modified in N minutes is "cold"
3. Lifetime prediction using machine learning (modern approaches)

## Implementation

### Header: `include/wear_leveling.h`

```c
typedef struct {
    WearLevelAlgorithm algorithm;
    uint32_t           threshold;      // trigger when max-min > threshold
    WearLevelStats     stats;          // current distribution
    FTL               *ftl;            // reference to FTL for block access
} WearLeveler;
```

### Source: `src/wear_leveling.c`

Key functions:
- `wl_init()`: Sets algorithm, threshold, computes initial stats
- `wl_update_stats()`: Iterates all blocks, computes min/max/avg/stddev
- `wl_check_and_balance()`: Checks if wear leveling is needed and performs static wear leveling swap
- `wl_select_block()`: Returns the block index with lowest erase count (for new allocations)
- `wl_print_stats()`: Pretty-prints the wear distribution

**wear_level_demo.c simulation**:
1. Write repeatedly to a small set of LBAs (hot spot)
2. Observe erase counts becoming unbalanced
3. Trigger wear leveling when spread exceeds threshold
4. Show rebalanced counts

## Expected Output (wear_level_demo)

```
=== Wear Leveling Demo ===

[1] Wear Leveler initialized (DYNAMIC, threshold=100)

[2] Simulating uneven wear: writing LBAs 0-4 repeatedly
    Most LBAs never touched -> erase counts become uneven

[3] Stats after uneven writes:
Wear Leveling Statistics:
  Algorithm:       DYNAMIC
  Threshold:       100
  Min Erase Count: 0
  Max Erase Count: 15
  Avg Erase Count: 2.34
  Std Dev:         4.12
  Spread (max-min): 15

[4] Checking if wear leveling is needed...
    No wear leveling needed (spread within threshold).
```

## References

- Chang et al., "Real-time Garbage Collection for Flash-Memory Storage Systems", ACM TECS 2004
- Wu et al., "An Adaptive Two-Level Management for NAND Flash Memory", IEEE TCAD 2013
- Jimenez et al., "Wear Unleveling: Improving NAND Flash Lifetime by Minimizing Chip Write Variation", HPCA 2014
- CMU 18-746: Wear Leveling in Flash (Chapter 18)
