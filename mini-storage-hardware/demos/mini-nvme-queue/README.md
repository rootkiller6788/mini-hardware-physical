# NVMe Queue — NVMe Protocol and Queue Pair Internals

## Overview

NVMe (Non-Volatile Memory Express) is the modern storage protocol designed from the ground up for SSDs connected via PCIe. Unlike legacy protocols (SATA, SAS) which were designed for mechanical HDDs with a single deep command queue, NVMe exposes up to 65,535 I/O queue pairs, each supporting 65,535 commands. This massive parallelism matches the internal parallelism of SSDs.

This module models the NVMe controller with admin and I/O submission/completion queue pairs, doorbell registers, and command format. The implementation demonstrates the full command lifecycle: ring doorbell → controller fetches command → executes → posts completion → host consumes.

## Theory

### PCIe Base

NVMe rides on PCI Express (PCIe), which provides:
- Direct CPU-to-device memory-mapped I/O (MMIO) access
- Low latency (~1-2 μs for Non-Posted Reads)
- High bandwidth (PCIe Gen3 x4 = 4 GB/s, Gen4 x4 = 8 GB/s, Gen5 x4 = 16 GB/s)
- Multiple MSI-X interrupt vectors for per-queue interrupt steering

```
CPU ──PCIe Root Complex──→ NVMe Controller (PCIe Endpoint)
                            │
                            ├── Admin SQ/CQ (Queue 0)
                            ├── I/O SQ/CQ Pair 1
                            ├── I/O SQ/CQ Pair 2
                            ├── ...
                            └── I/O SQ/CQ Pair N
```

### Queue Pairs

Each Queue Pair consists of:
- **Submission Queue (SQ)**: Circular buffer in host memory where the host writes commands. The controller reads from it.
- **Completion Queue (CQ)**: Circular buffer in host memory where the controller writes completions. The host reads from it.

```
Submission Queue (Host → Controller):
  Head ─→ [CMD_0] [CMD_1] [CMD_2] [CMD_3] [empty...]
                                               Tail ─→
  
  Host: increments Tail when adding commands
  Controller: increments Head when consuming commands

Completion Queue (Controller → Host):
  Head ─→ [COMP_0] [COMP_1] [COMP_2] [empty...]
                                             Tail ─→
  
  Controller: increments Tail when posting completions
  Host: increments Head when consuming completions
```

### Doorbell Mechanism

The doorbell is the synchronization primitive. There are no locks — just MMIO writes:

1. **Host writes SQ Tail Doorbell**: Tells controller "I added commands up to position X"
2. **Controller fetches** commands from SQ entries [Head, Tail)
3. **Controller processes** commands
4. **Controller posts** completions to CQ
5. **Controller sends interrupt** (MSI-X) to host
6. **Host processes** completions
7. **Host writes CQ Head Doorbell**: Tells controller "I consumed completions up to position Y"

```
   Host Side                    PCIe Bus                Controller Side
   ─────────                    ────────                ────────────────
1. Write SQ Doorbell     ────MMIO Write──→     Receive doorbell
   (ring the bell)                                ↓
                                          2. Fetch commands from SQ
                                               (DMA read from host mem)
                                                 ↓
                                          3. Process commands
                                                 ↓
4. Receive interrupt    ←──MSI-X Interrupt───  Send completion
   (or poll CQ)                                   ↓
                                          4. Write completion to CQ
   Read CQ and process                          (DMA write to host mem)
        ↓
5. Write CQ Head Doorbell ──MMIO Write──→     Free CQ slots
```

### NVMe Command Format (64 bytes)

| DWORD | Bits | Field | Description |
|-------|------|-------|-------------|
| 0     | 7:0  | Opcode | READ=0x02, WRITE=0x01, IDENTIFY=0x06 |
| 0     | 15:8 | FUSE   | 0=normal, 1=fused second, 2=reserved |
| 0     | 31:16| CID    | Command Identifier |
| 1     | 31:0 | NSID   | Namespace Identifier |
| 2-3   | 63:0 | Reserved | |
| 4-5   | 63:0 | MPTR   | Metadata Pointer |
| 6-7   | 63:0 | PRP1   | PRP Entry 1 / Data Pointer / SGL Segment |
| 8-9   | 63:0 | PRP2   | PRP Entry 2 |
| 10    | 63:0 | SLBA   | Starting LBA |
| 11    | 15:0 | NLB    | Number of Logical Blocks (0-based) |
| 15    | 31:0 | CDW15  | Command DWORD 15 |

### PRP vs SGL

**PRP (Physical Region Page)**:
- Simple: two 64-bit physical addresses (PRP1, PRP2)
- PRP1: first page of data (or PRP List pointer if data > 4KB)
- PRP2: PRP List pointer (array of 64-bit physical addresses)
- Mandatory for all NVMe controllers
- Limited flexibility but sufficient for most workloads

**SGL (Scatter-Gather List)**:
- More flexible: supports bit bucket, keyed, last segment
- Can describe arbitrary memory layouts
- Optional in NVMe 1.0, mandatory in NVMe 1.2+

### Admin vs I/O Commands

| Aspect | Admin Commands | I/O Commands |
|--------|---------------|--------------|
| Queue | Admin SQ/CQ (Queue ID 0) | I/O SQ/CQ (Queue ID 1-65535) |
| Purpose | Create/delete I/O queues, identify, set features | Data transfer (read, write, flush, compare) |
| Size | Small (typically 4KB) | Large (up to several MB) |
| Frequency | Rare (initialization time) | Frequent (every I/O) |

### Interrupts

NVMe supports three interrupt modes:
1. **Pin-based (INTx)**: Legacy, one interrupt line shared across all queues
2. **MSI-X**: Modern, up to 2048 vectors, each queue pair gets a dedicated vector
3. **Polling**: Host spins on CQ entries (lowest latency, CPU expensive)

### Queue State Machine

```
Queue Lifecycle:
  ┌─────┐   Create IO SQ    ┌─────────┐   Delete IO SQ   ┌─────┐
  │  -  │ ───────────────→  │ Active  │ ───────────────→ │  -  │
  └─────┘                   └─────────┘                   └─────┘
                                │  ↑
                          Submit│  │Complete
                                ↓  │
                              [Processing]
```

## Implementation

### Header: `include/nvme.h`

```c
typedef struct {
    uint32_t       id;
    uint32_t       depth;
    NVMeCommand    entries[NVME_QUEUE_DEPTH];
    uint32_t       head;
    uint32_t       tail;
    volatile uint32_t doorbell_head;
    volatile uint32_t doorbell_tail;
} NVMeQueue;

typedef struct {
    NVMeQueue      admin_sq;
    NVMeQueue      admin_cq;
    NVMeQueue      io_sqs[NVME_MAX_IO_QUEUES];
    NVMeQueue      io_cqs[NVME_MAX_IO_QUEUES];
    uint32_t       sq_tail_doorbell[1 + NVME_MAX_IO_QUEUES];
    uint32_t       cq_head_doorbell[1 + NVME_MAX_IO_QUEUES];
    uint64_t       ns_size[NVME_MAX_NAMESPACES];
} NVMeController;
```

### Source: `src/nvme.c`

Key functions:
- `nvme_init()`: Initializes admin and I/O queue pairs, sets namespace sizes
- `nvme_submit_admin_cmd()`: Enqueues a command to admin SQ, rings doorbell
- `nvme_submit_io_cmd()`: Enqueues a command to I/O SQ[qid], rings doorbell
- `nvme_process_cq()`: Pops a completion from CQ[qid], rings head doorbell
- `nvme_print_regs()`: Dumps all queue states and doorbell registers

### Example: `examples/nvme_cmd_demo.c`

```c
NVMeCommand cmd;
memset(&cmd, 0, sizeof(cmd));
cmd.opcode    = NVME_OP_READ;
cmd.namespace_id = 1;
cmd.slba      = 0;      // read from start of namespace
cmd.nlb       = 7;      // 8 sectors (0-based count)
cmd.data_ptr  = (uint64_t)data_buffer;

nvme_submit_io_cmd(&ctrl, 0, &cmd);  // submit to I/O queue 0
// ... some time later ...
NVMeCompletion comp;
nvme_process_cq(&ctrl, 1, &comp);    // get completion from I/O CQ 0
```

## Expected Output

```
=== NVMe Command Demo ===

[1] NVMe Controller initialized
  Admin SQ: head=0 tail=0 depth=64
  Admin CQ: head=0 tail=0 depth=64
  Doorbell - Admin SQ Tail: 0
  Doorbell - Admin CQ Head: 0
  I/O SQ[0]: head=0 tail=0 depth=64
  I/O CQ[0]: head=0 tail=0 depth=64

[2] Submitting READ commands via I/O SQ[0]...
    Submitted READ cmd 0: slba=0 nlb=7 -> rc=0
    ...

[3] Doorbell registers after submissions:
    I/O SQ[0] Tail Doorbell: 4

[4] Submitting WRITE commands via I/O SQ[0]...

[5] Processing completion queue I/O CQ[0]...
    Completion: sq_id=1 sq_head=0 cmd_id=0 status=0x0000
    ...

[7] Processing admin completion queue...
    Admin Completion: sq_id=0 status=0x0000
```

## Building

```bash
make nvme_cmd_demo
./bin/nvme_cmd_demo
```

## References

- NVM Express Base Specification v2.0, Chapter 4: Data Structures, Chapter 5: Admin Commands
- Walker, "NVMe Command Set and Submission/Completion Queue Architecture", SNIA 2013
- VMware, "Understanding NVMe Performance", 2017
- CMU 18-746: NVMe and Host Interface (Chapter 21)
- Stanford CS240: NVMe and Modern Storage Interfaces
