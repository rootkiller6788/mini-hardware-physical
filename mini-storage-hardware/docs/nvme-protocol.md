# NVMe Protocol — Non-Volatile Memory Express 协议

## Overview

NVMe (Non-Volatile Memory Express) is the industry-standard software interface for PCIe SSDs. It was designed specifically for non-volatile memory, replacing legacy protocols (SATA AHCI) that were designed for mechanical hard drives. NVMe eliminates the serial command queue bottleneck, enabling massive parallelism that matches the internal parallelism of NAND flash.

## Revision History

| Version | Year | Key Features |
|---------|------|-------------|
| 1.0     | 2011 | Initial spec, 64K queues, 64K cmds/queue |
| 1.1     | 2012 | Multi-path I/O, sanitize, firmware update |
| 1.2     | 2014 | SGL, end-to-end data protection, controller memory buffer |
| 1.3     | 2017 | Directives, virtualization enhancements, sanitize crypto erase |
| 1.4     | 2019 | I/O determinism, persistent event log, endurance group management |
| 2.0     | 2020 | Zoned Namespaces (ZNS), Key Value, Endurance Groups (partial) |
| 2.0a/b  | 2021+ | FDP (Flexible Data Placement), computational storage |

## PCIe Interface

### Register Interface (MMIO)

The controller exposes a set of 32-bit registers via PCIe Base Address Register (BAR0):

```
BAR0 Memory-Mapped Registers:
  Offset  Size   Register
  ------  ----   --------
  0x00    8      Controller Capabilities (CAP)
  0x08    4      Version (VS)
  0x0C    4      Interrupt Mask Set (INTMS)
  0x10    4      Interrupt Mask Clear (INTMC)
  0x14    4      Controller Configuration (CC)
  0x1C    4      Controller Status (CSTS)
  0x20    4      NVM Subsystem Reset (NSSR)
  0x24    8      Admin Queue Attributes (AQA)
  0x28    8      Admin SQ Base Address (ASQ)
  0x30    8      Admin CQ Base Address (ACQ)
  0x1000+        Doorbell Stride (SQ0TDBL, CQ0HDBL, ...)
```

### Controller Initialization Sequence

```
1. Host sets CC.EN = 0 (disable controller)
2. Host waits for CSTS.RDY = 0
3. Host allocates Admin SQ/CQ in host memory (physically contiguous)
4. Host writes AQA (Admin Queue Attributes)
5. Host writes ASQ (Admin SQ Base Address)
6. Host writes ACQ (Admin CQ Base Address)
7. Host sets CC.EN = 1 (enable controller)
8. Host waits for CSTS.RDY = 1
9. Host sends Identify command to discover device capabilities
10. Host creates I/O Queue Pairs via Admin commands
11. Host sends I/O commands through created queues
```

## Command Format (64 Bytes)

```
DWORD 0:  [7:0 OPCODE] [8:15 FUSE] [16:31 CID]
DWORD 1:  [31:0 NSID]
DWORD 2-3: Reserved
DWORD 4-5: Metadata Pointer (MPTR)
DWORD 6-7: Data Pointer (DPTR) — PRP1 / SGL Entry 1
DWORD 8-9: PRP2 / SGL Entry 2
DWORD 10-11: Starting LBA (SLBA) — bits 63:0
DWORD 12:  [15:0 NLB] [16:31 DSM Attributes]
DWORD 13: CDW13 — depends on opcode
DWORD 14: CDW14 — depends on opcode
DWORD 15: CDW15 — depends on opcode
```

### Opcodes

| Category | Opcode | Command |
|----------|--------|---------|
| Admin | 0x00 | Delete I/O SQ |
| Admin | 0x01 | Create I/O SQ |
| Admin | 0x02 | Get Log Page |
| Admin | 0x04 | Delete I/O CQ |
| Admin | 0x05 | Create I/O CQ |
| Admin | 0x06 | Identify |
| Admin | 0x08 | Abort |
| Admin | 0x09 | Set Features |
| Admin | 0x0A | Get Features |
| Admin | 0x0C | Async Event Request |
| I/O | 0x00 | Flush |
| I/O | 0x01 | Write |
| I/O | 0x02 | Read |
| I/O | 0x04 | Write Uncorrectable |
| I/O | 0x05 | Compare |
| I/O | 0x08 | Write Zeroes |
| I/O | 0x09 | Dataset Management (TRIM) |

## Submission Queue (SQ)

The SQ is a circular buffer in host memory:

```
┌────────────────────────────────────────────────┐
│  Slot 0    │  Command (64 bytes)                │
│  Slot 1    │  Command (64 bytes)                │
│  ...       │  ...                               │
│  Slot N-1  │  Command (64 bytes)                │
└────────────────────────────────────────────────┘

Host writes commands, increments Tail doorbell
Controller reads commands, increments internal Head
```

**SQ Tail Doorbell Write**:
```
1. Host constructs command in SQ slot[tail]
2. Host increments tail ← (tail + 1) % queue_depth
3. Host writes new tail value to SQyTDBL MMIO register
4. This signals the controller: "new commands available"
```

## Completion Queue (CQ)

The CQ is also a circular buffer in host memory:

```
┌────────────────────────────────────────────────┐
│  Slot 0    │  Completion (16 bytes)              │
│  Slot 1    │  Completion (16 bytes)              │
│  ...       │  ...                               │
│  Slot N-1  │  Completion (16 bytes)              │
└────────────────────────────────────────────────┘

Controller writes completions, increments Tail
Host reads completions, writes Head doorbell
```

**Completion Entry (16 bytes)**:

| DWORD | Field       | Description |
|-------|------------|-------------|
| 0     | Command Specific | Depends on command |
| 1     | Reserved    | |
| 2     | SQ Head Ptr | Position in SQ of completed command |
| 2     | SQ ID       | Which SQ the command came from |
| 3     | Command ID  | Echo of CID from command |
| 3     | Phase Tag (P) | Toggles to indicate new entry |
| 3     | Status Field| Success/error code |

### Phase Tag

The phase tag (P bit) is an elegant mechanism to avoid having to clear CQ entries:

```
Initial: All CQ entries have P=0
After first pass through queue: P=1 for new entries
After second pass: P=0 again

Host checks: if CQ[i].P != current_phase → empty slot
             if CQ[i].P == current_phase → new completion
```

**CQ Head Doorbell Write**:
```
1. Host reads and processes completion at slot[head]
2. Host increments head ← (head + 1) % queue_depth
3. Host writes new head value to CQyHDBL MMIO register
4. This tells controller: "completion slot freed, can reuse"
```

## PRP (Physical Region Page)

PRP is the data transfer mechanism. A PRP entry is a 64-bit physical address.

### Data ≤ PAGE_SIZE (typically 4KB)

```
PRP1 = physical address of data buffer
PRP2 = unused (0)
```

### Data > PAGE_SIZE (but ≤ 2 pages)

```
PRP1 = physical address of page 0
PRP2 = physical address of page 1
```

### Data > 2 pages

```
PRP1 = physical address of page 0
PRP2 = physical address of PRP List
         │
         └→ PRP List: [addr_page1, addr_page2, ..., addr_pageN]
            (each 8 bytes, must be page-aligned, non-cross-page-boundary)
```

### Constraints

- PRP entries must point to physical memory
- PRP List must not span page boundaries
- Page sizes: 4KB (typical), may be 8KB, 16KB, etc.
- PRP offset must match across all entries if data starts at non-zero offset

## SGL (Scatter-Gather List)

SGL is more flexible than PRP and is mandatory in NVMe 1.2+:

```
SGL Segment:
  ┌──────────────────┐
  │ SGL Descriptor 0  │ → Data Block or Next Segment Descriptor
  │ SGL Descriptor 1  │ → Data Block
  │ ...               │
  │ SGL Descriptor N  │ → Last Segment (+ Data Block)
  └──────────────────┘
```

SGL Descriptor types:
- **Data Block**: Points to a contiguous data buffer
- **Bit Bucket**: Sink (discard data)
- **Segment**: Points to next SGL Segment
- **Last Segment**: Points to last SGL Segment
- **Keyed**: Special memory handle

## Interrupts

### Pin-Based (INTx)

Legacy interrupt model. One interrupt line per function. All events share the same vector.

### MSI (Message Signaled Interrupts)

```
Controller → PCIe Memory Write → CPU Interrupt Controller (APIC/LAPIC)
```

Up to 32 vectors per function. Still shared, but more granular than INTx.

### MSI-X

```
CQ0 → MSI-X Vector 0 → CPU Core 0
CQ1 → MSI-X Vector 1 → CPU Core 1
CQ2 → MSI-X Vector 2 → CPU Core 2
...
```

Each CQ can have its own MSI-X vector. The host can route specific queues to specific CPU cores, enabling per-core I/O submission/completion with no locking.

### Interrupt Coalescing

To reduce interrupt overhead, the controller can batch completions and fire one interrupt for multiple completions. Parameters:
- **Time**: Max microseconds to wait
- **Threshold**: Max completions to batch

Tradeoff: higher throughput vs. higher tail latency.

## Admin Commands

The Admin Queue (Queue ID 0) handles device configuration:

| Command | Purpose |
|---------|---------|
| Identify | Discover controller/namespace capabilities |
| Create I/O CQ | Allocate a completion queue |
| Create I/O SQ | Allocate a submission queue (linked to CQ) |
| Delete I/O CQ | Deallocate completion queue |
| Delete I/O SQ | Deallocate submission queue |
| Get Features | Read feature (arbitration, power mgmt, etc.) |
| Set Features | Configure feature |
| Get Log Page | Read controller/namespace statistics |
| Async Event Request | Request notification of async events |

## Performance Characteristics

| Metric | Typical Value |
|--------|---------------|
| Command submission | ~0.5-1 μs (doorbell MMIO write) |
| Completion processing | ~1-2 μs (include CQ read + doorbell) |
| Under load (4KB random read) | ~8-10 μs including NAND read |
| Max IOPS (Gen4, QD256, 4KB) | 1M+ IOPS |
| Throughput (Gen4 x4, 128KB) | 7 GB/s |
| Queue depth for saturation | 32-64 for consumer, 128-256 for enterprise |

## Implementation in This Module

Our NVMe implementation (`src/nvme.c`) models:
- Admin SQ/CQ + 8 I/O SQ/CQ pairs
- Circular queue semantics with head/tail pointers
- Doorbell register writes
- Namespace size configuration
- Command submission and completion processing

The model is behavioral rather than cycle-accurate, focusing on the protocol flow rather than PCIe TLP-level simulation.

## References

- NVM Express Base Specification v2.0, NVM Express Inc.
- NVM Express Command Set Specification v1.0
- Marks, "An NVM Express Tutorial", Flash Memory Summit 2013
- Xu, "NVMe: A Look at the New Storage Protocol", SNIA 2013
- CMU 18-746: Storage Systems, Chapter 21: NVMe
