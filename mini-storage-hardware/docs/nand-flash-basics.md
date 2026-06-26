# NAND Flash Basics — NAND 闪存基础

## Overview

NAND flash memory is the dominant non-volatile storage technology, used in SSDs, USB drives, SD cards, and mobile devices. Unlike DRAM (volatile, fast, byte-addressable) and HDDs (non-volatile, slow, mechanical), NAND occupies a unique position: non-volatile, moderate speed, page-addressable, and block-erasable.

## Floating Gate Transistor

The fundamental storage element is the **floating gate transistor** (FGT):

```
┌─────────── Floating Gate (stores charge = data)
│  ┌──────── Control Gate (word line)
│  │  ┌───── Source
│  │  │  ┌── Drain
│  │  │  │
▼  ▼  ▼  ▼
┌──┐ ┌──┐
│FG│ │CG│
└──┘ └──┘
├──────────┤
│  Tunnel  │
│  Oxide   │
├──────────┤
│  Substrate (P-type)
└──────────┘
```

**Programming** (writing): High voltage on control gate pulls electrons through tunnel oxide into floating gate via Fowler-Nordheim tunneling.

**Erasing**: High reverse voltage pushes electrons back out of the floating gate into the substrate.

**Reading**: Apply voltage to control gate. If floating gate has electrons (programmed = 0 for SLC), the transistor doesn't conduct → current detected as "0". If empty (erased = 1), transistor conducts → current detected as "1".

## Cell Types: SLC, MLC, TLC, QLC

As demand for density grows, manufacturers store multiple bits per cell by distinguishing finer charge levels:

| Type | Bits/Cell | Charge Levels | Endurance (PEC) | Retention | Read Latency | Relative Cost/GB |
|------|-----------|---------------|-----------------|-----------|-------------|------------------|
| SLC  | 1         | 2 (0,1)       | 50K-100K        | 10+ years | 25 μs       | 4x |
| MLC  | 2         | 4 (00,01,10,11) | 3K-10K       | 3-5 years | 50 μs       | 2x |
| TLC  | 3         | 8 levels      | 1K-3K           | 1-3 years | 75-100 μs   | 1.3x |
| QLC  | 4         | 16 levels     | 100-1000        | <1 year   | 120-150 μs  | 1x (baseline) |

### Voltage Threshold Distributions

```
SLC:
  [Erased=1]          [Programmed=0]
       │                    │
  ─────┼────────────────────┼─────→ Voltage
       0V                  Vt

MLC:
  [11=Erased]   [10]    [00]    [01]
       │          │       │       │
  ─────┼──────────┼───────┼───────┼──→ Voltage
       0V        Vt1     Vt2     Vt3
```

Each additional bit halves the voltage margin between states, making reads slower (more sense time) and errors more likely (smaller signal-to-noise ratio).

### Page Types in TLC/QLC

In our `PageType` enum (`LOWER`, `MIDDLE`, `UPPER`), each represents one logical page within a shared set of physical cells:

```
TLC Cell (3 bits):
  ┌──────────────────────┐
  │  LOWER page (LSB)    │  ← fastest read, highest endurance
  │  MIDDLE page (CSB)   │
  │  UPPER page (MSB)    │  ← slowest read, highest error rate
  └──────────────────────┘
```

## Program/Erase Cycle (PEC)

A single Program/Erase Cycle:

1. **Erase block** (entire block, all pages → 1): ~3 ms, 3M ns
2. **Program page** (one page, 1s → 0s): ~900 μs, 900K ns
3. **Read page**: ~50 μs, 50K ns

### Asymmetry

```
                    Read    Program    Erase
Time per page:      50μs    900μs      N/A
Time per block:     6.4ms   115ms      3ms
Relative speed:     1x      18x        60x (normalized per-bit)
```

Key takeaway: **Program is ~18x slower than read; erase is even slower when amortized per page.**

## Endurance

Endurance is the number of P/E cycles before the cell can no longer reliably store data.

### Wear Mechanisms

1. **Tunnel oxide degradation**: Each P/E cycle damages the oxide layer, creating traps that leak charge
2. **Electron trapping**: Electrons get stuck in the oxide, shifting threshold voltages
3. **Interface state generation**: Si-SiO2 interface degrades, affecting carrier mobility

### Endurance Enhancement Techniques

- **Wear leveling**: Evenly distribute writes
- **ECC**: Correct errors from worn cells (see `ecc.h`)
- **DSP/Read retry**: Adjust read reference voltages as cells age
- **Over-provisioning**: Extra cells replace worn ones
- **Adaptive programming**: Use gentler program pulse as cells age

## Retention

Data retention is how long programmed data remains readable after power-off.

| Cell Type | Retention (typical) | Retention (end of life) |
|-----------|--------------------|------------------------|
| SLC | 10+ years | 1 year |
| MLC | 3-5 years | 3 months |
| TLC | 1-3 years | 1 month |
| QLC | <1 year | <1 week |

Retention degrades with:
- Higher temperature (Arrhenius law: ~2x faster per 10°C)
- More P/E cycles (oxide traps leak charge)
- Higher bits per cell (tighter margins)

## Read Disturb

Reading a page applies a voltage to the word line of the page being read AND to all other pages in the same block. This weak voltage can slowly program (add electrons to) cells that are not being read.

```
Read Block 0, Page 5:
  ┌── Reading Page 5 ──→ V_read (moderate)
  │  ┌── All OTHER pages in Block 0 ──→ V_pass (lower, but still stresses)
  │  │
  ▼  ▼
  [Page0][Page1]...[Page5]...[Page127]
```

**Mitigation**: Read disturb counters track how many times each block has been read. When a threshold is exceeded, the controller moves data to a fresh block.

## Program Disturb

When programming a page, high voltage on the selected word line can accidentally program adjacent cells sharing the same bit line. Modern NAND uses **incremental step pulse programming (ISPP)** with verify to minimize disturb.

## 3D NAND

All the above applies to 2D (planar) NAND. Modern SSDs use 3D NAND:
- Vertically stacked layers (32-232+ layers in 2024)
- Larger cell size → better endurance
- Charge trap flash (CTF) replaces floating gate
- CMOS-under-array (CuA) puts logic beneath the memory array

## References

- Micheloni et al., "Inside NAND Flash Memories", Springer 2010
- Cai et al., "Errors in Flash-Memory Based Solid-State Drives", USENIX ;login:, 2017
- JEDEC JESD218: Solid-State Drive (SSD) Requirements and Endurance Test Method
- CMU 18-746: NAND Flash Memory (Chapters 13-15)
