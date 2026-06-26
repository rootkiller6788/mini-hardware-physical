# SSD Controller — Solid State Drive Controller Architecture

## Overview

The SSD controller is the brain of a solid-state drive. It sits between the host interface (SATA/PCIe) and the raw NAND flash chips, managing command queuing, address translation (FTL), wear leveling, garbage collection, ECC, and data buffering. This module models a multi-channel SSD controller with an issue queue, completion queue, SRAM buffer, and per-channel NAND busy cycle tracking.

The controller simulation processes commands cycle-by-cycle, modeling NAND read/write/erase latencies and channel parallelism. Commands flow from the host interface through the issue queue, get dispatched to available NAND channels, and report completion through the completion queue back to the host.

## Theory

### Multi-Channel Architecture

```
Host Interface (PCIe Gen3 x4 / SATA III)
              │
    ┌─────────▼──────────────┐
    │   SSD Controller ASIC   │
    │  ┌──────────────────┐   │
    │  │  Embedded CPU(s)  │   │  ARM Cortex-R / custom RISC
    │  │  (FTL firmware)   │   │
    │  └──────────────────┘   │
    │  ┌──────────────────┐   │
    │  │  SRAM / DRAM      │   │  1MB - 1GB buffer
    │  │  (data cache)     │   │
    │  └──────────────────┘   │
    │  ┌──────────────────┐   │
    │  │  ECC Engine       │   │  BCH / LDPC encode/decode
    │  └──────────────────┘   │
    │  ┌──────────────────┐   │
    │  │  Channel Ctrl[0]  │───┼──→ NAND Ch0
    │  │  Channel Ctrl[1]  │───┼──→ NAND Ch1
    │  │  Channel Ctrl[2]  │───┼──→ NAND Ch2
    │  │  Channel Ctrl[3]  │───┼──→ NAND Ch3
    │  └──────────────────┘   │
    └─────────────────────────┘
```

### Channel Parallelism

Each channel is an independent data path:

```
Channel 0:
  CE0 ──→ Die 0 (Plane 0, Plane 1)
  CE1 ──→ Die 1 (Plane 0, Plane 1)

Channel 1:
  CE0 ──→ Die 2 (Plane 0, Plane 1)
  CE1 ──→ Die 3 (Plane 0, Plane 1)
```

With 4 channels and 2 chip enables per channel, the controller can have 8 NAND dies operating simultaneously. This is the primary source of SSD parallelism.

### Command Flow

```
Host → SSD Controller:
  1. Host sends Read/Write command via NVMe/SATA
  2. Controller enqueues command in issue queue
  3. Controller processor dequeues, translates LBA (via FTL)
  4. Dispatches to appropriate NAND channel
  5. NAND channel issues command to flash die
  6. After NAND latency, data returned/acknowledged
  7. Controller enqueues completion
  8. Host polls or receives interrupt for completion

Detailed timing:
  Read:  LBA→FTL→physical page → NAND read (50μs) → transfer data (400MB/s)
  Write: data in SRAM → FTL mapping → NAND program (900μs) → completion
  Erase: background GC → select block → NAND erase (3ms) → free block pool
```

### NAND Latencies

| Operation | Typical Latency | Model Value |
|-----------|----------------|-------------|
| Read Page | 20-100 μs      | 50 μs       |
| Program Page | 200-2000 μs | 900 μs      |
| Erase Block | 1-10 ms      | 3 ms        |
| Data Transfer | 200-800 MB/s per ch | 400 MB/s |

### Command Queue

The controller maintains two circular queues:

**Issue Queue**: Commands from host waiting to be processed
```
Head ─→ [CMD_READ_LBA100] [CMD_WRITE_LBA200] [CMD_READ_LBA50] [empty...]
                                                                    Tail ─→
```

**Completion Queue**: Processed commands ready for host consumption
```
Head ─→ [COMPLETE_LBA50] [COMPLETE_LBA100] [empty...]
                                                Tail ─→
```

### SRAM / DRAM Buffer

SSD controllers typically have:
- **Internal SRAM**: 1-2 MB for FTL mapping table cache, command structures
- **External DRAM**: 256 MB - 2 GB for full mapping table, write buffer, read cache

The write buffer absorbs bursts and allows the controller to coalesce writes. The mapping table cache keeps frequently-accessed LBA→physical mappings in fast SRAM rather than slow NAND.

### Processor Architecture

Modern SSD controllers use multi-core processors:

```
┌─────────────────────────────────────┐
│  Core 0: Host Interface & NVMe      │
│  Core 1: FTL & Address Mapping      │
│  Core 2: Garbage Collection         │
│  Core 3: Wear Leveling & ECC        │
└─────────────────────────────────────┘
```

Each core may have dedicated SRAM and access to shared DRAM through an interconnect.

## Implementation

### Header: `include/ssd_controller.h`

```c
#define SSDC_CHANNELS        4
#define SSDC_PLANES_PER_CH   4
#define SSDC_QUEUE_DEPTH     64
#define SSDC_SRAM_SIZE       (1024 * 1024)   // 1 MB

typedef struct {
    NANDChannel channels[SSDC_CHANNELS];
    CommandQueue issue_queue;
    CommandQueue completion_queue;
    uint8_t      sram[SSDC_SRAM_SIZE];
    uint32_t     sram_used;
    FTL          ftl;
    uint64_t     current_cycle;
    uint64_t     busy_channels;
} SSDController;
```

### Source: `src/ssd_controller.c`

Key functions:
- `ssdc_init()`: Initializes controller, FTL, queues, all channels
- `ssdc_submit_io()`: Enqueues an I/O command with data buffer
- `ssdc_process()`: Simulates `cycles` worth of processing — dequeues commands from the issue queue, dispatches to FTL, and moves completions. Models channel busy state.
- `ssdc_complete()`: Returns the next completed command from the completion queue
- `ssdc_print_queue()`: Prints queue depths, channel busy state, and FTL statistics

The cycle-based processing model allows performance analysis:
```
for (int t = 0; t < 1000000; t++) {
    ssdc_submit_io(&ctrl, IO_WRITE, random_lba(), data);
    ssdc_process(&ctrl, 1);  // advance one cycle
    if (ssdc_complete(&ctrl, &done_cmd) == 0) {
        // count completed IOs
    }
}
```

## Expected Output

Running the SSD controller demo (integrated with FTL):

```
SSD Controller State:
  Current Cycle:      1000
  Issue Queue Depth:  3 / 64
  Completion Queue:   7 / 64
  Busy Channels:      2 / 4
  SRAM Used:          12288 / 1048576 bytes
```

## Building

```bash
make ftl_demo
./bin/ftl_demo
```

The SSD controller module is used internally by the FTL and NVMe demos. A standalone SSD controller benchmark can be easily added.

## References

- Micheloni et al., "Inside Solid State Drives (SSDs)", Springer 2013, Chapter 3: SSD Architecture
- Agrawal et al., "Design Tradeoffs for SSD Performance", USENIX ATC 2008
- Dirik et al., "Performance of Flash Solid State Drives", HotStorage 2009
- CMU 18-746: SSD Architecture and NAND Flash (Chapters 13-15)
