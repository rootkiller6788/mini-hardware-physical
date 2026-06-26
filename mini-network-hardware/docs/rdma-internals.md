# RDMA Internals

## Overview

Remote Direct Memory Access (RDMA) is a technology that enables one computer to directly access the memory of another without involving the operating system or CPU of either machine. This document provides a comprehensive analysis of RDMA internals: queue pairs, memory registration, zero-copy data transfer, kernel bypass, one-sided vs two-sided operations, and the InfiniBand transport architecture.

## RDMA Architectural Overview

### Standard TCP/IP vs RDMA

```
Traditional TCP/IP:
  App → Kernel Buffer → TCP Stack → NIC Driver → NIC → Wire
  (Multiple copies, context switches, CPU per-byte touching)

RDMA:
  App (Registered MR) → NIC (DMA) → Wire
  (Zero copies, zero kernel involvement, CPU bypass)
```

### Key Benefits

| Benefit             | Mechanism                                    | Impact                    |
|---------------------|----------------------------------------------|---------------------------|
| Zero Copy           | Direct DMA between app buffer and wire       | Up to 3x throughput       |
| Kernel Bypass       | User-space verbs API → direct HCA access     | 10x latency reduction     |
| CPU Offload         | Transport processing in HCA hardware         | Freed CPU cycles          |
| One-Sided Ops       | RDMA Read/Write without remote CPU           | <1 μs remote access       |

## Queue Pair (QP) Model

### Structure

A Queue Pair is the fundamental RDMA communication endpoint:

```
+---------------------------------------+
|           Queue Pair (QP)              |
|                                        |
|  +------------------+  +------------+  |
|  | Send Queue (SQ)  |  | Recv Q (RQ)|  |
|  |  [WQE][WQE][WQE] |  | [WQE][WQE] |  |
|  +--------+---------+  +------+-----+  |
|           |                   |         |
|           v                   v         |
|  +-----------------------------------+  |
|  |      Completion Queue (CQ)         |  |
|  |  [CQE][CQE][CQE] ...               |  |
|  +-----------------------------------+  |
+---------------------------------------+
```

### Work Queue Elements (WQEs)

A WQE describes a data transfer operation:

```
struct WQE {
    Opcode opcode;    // SEND, RDMA_WRITE, RDMA_READ, ATOMIC_*
    uint64_t sge[2];  // Scatter-gather elements (addr, len, lkey)
    uint32_t length;
    uint32_t flags;   // SIGNALED, INLINE, SOLICITED
    ...
};
```

### Scatter-Gather List (SGE)

Each WQE can reference multiple non-contiguous memory buffers:

```
SGE[0]: addr=0x1000, len=1024  ──┐
SGE[1]: addr=0x3000, len=512   ──┤─→ Combined message (1472 bytes)
SGE[2]: addr=0x5000, len=256   ──┘
```

### Completion Queue (CQ)

Completions signal that a work request has finished:

```
CQE {
    uint32_t wqe_id;     // Matches WQE posted
    uint32_t byte_count; // Bytes transferred
    uint8_t  opcode;     // Completed operation
    uint8_t  status;     // 0 = success
    ...
};
```

## RDMA Transport Operations

### SEND (Two-Sided)

Requires a matching RECV WQE to be posted on the remote side:

```
Initiator SQ              Responder RQ
[SEND WQE] ──────────────> [RECV WQE]  (must be pre-posted)
  data                      data → app buffer
[CQE: complete]            [CQE: complete]
```

### RDMA WRITE (One-Sided)

Writes directly to remote memory without a pre-posted RECV:

```
Initiator SQ              Responder Memory
[WRITE WQE] ──────────────> [MR at addr with rkey]
  (addr, rkey, data)         data written directly
[CQE: complete]             (No CQE on remote!)
```

### RDMA READ (One-Sided)

Reads directly from remote memory without remote CPU involvement:

```
Initiator SQ              Responder Memory
[READ WQE] ───────────────> [MR at addr with rkey]
  (addr, rkey, len)          read data
  data ←───────────────────
[CQE: complete + data]
```

### Atomic Operations

```
CmpSwap: if mem[addr] == compare, mem[addr] = swap; return old value
FetchAdd: old = mem[addr]; mem[addr] += add; return old
```

## Memory Registration

### Purpose

Memory must be registered to:
1. Pin physical pages (prevent swap-out)
2. Translate virtual to physical addresses (for DMA)
3. Associate access keys (lkey/rkey) for protection
4. Program the IOMMU for address translation

### Memory Region Lifecycle

```
Allocate (malloc/mmap)
    |
    v
Register (ibv_reg_mr)
    |  - Pins memory pages
    |  - Returns lkey (local key)
    |  - Returns rkey (remote key)
    v
Use in RDMA operations (send/recv/read/write)
    |
    v
Deregister (ibv_dereg_mr)
    |  - Unpins pages
    |  - Invalidates keys
    v
Free (free/munmap)
```

### Protection Domain

A Protection Domain provides isolation between unrelated applications:

```
PD 1:
  QP A, QP B ──── MR 1, MR 2

PD 2:
  QP C ──── MR 3, MR 4

QP A cannot access MR 3 (different PDs)
```

## InfiniBand Transport Architecture

### Protocol Stack

```
+------------------+  +------------------+
|  Upper Layer     |  |  Upper Layer     |
|  Protocol (ULP)  |  |  Protocol (ULP)  |
+------------------+  +------------------+
|  Transport Layer |  |  Transport Layer |
|  (RC, UC, RD, UD)|  |  (RC, UC, RD, UD)|
+------------------+  +------------------+
|  Network Layer   |  |  Network Layer   |
|  (GRH, LRH)      |  |  (GRH, LRH)      |
+------------------+  +------------------+
|  Link Layer      |  |  Link Layer      |
|  (Flow Control)  |  |  (Flow Control)  |
+------------------+  +------------------+
|  Physical Layer  |  |  Physical Layer  |
|  (SDR/DDR/QDR/..)|  |  (SDR/DDR/QDR)  |
+------------------+  +------------------+
```

### Transport Types

| Type | Full Name           | Delivery    | Ordering | Connections |
|------|---------------------|-------------|----------|-------------|
| RC   | Reliable Connected   | Guaranteed  | Ordered  | 1-to-1      |
| UC   | Unreliable Connected | Best-effort | Ordered  | 1-to-1      |
| RD   | Reliable Datagram    | Guaranteed  | Unordered| 1-to-many    |
| UD   | Unreliable Datagram  | Best-effort | Unordered| 1-to-many    |
| XRC  | Extended RC          | Guaranteed  | Ordered  | M-to-1 (SRQ) |
| DC   | Dynamic Connected    | Guaranteed  | Ordered  | Dynamic      |

### Connection Establishment

```
Initiator                     Responder
    |                            |
    |  REQ (QP attrs)            |
    | -------------------------> |
    |                            |  Create QP
    |                            |  Transition: RESET → INIT → RTR
    |  REP (QP attrs)            |
    | <------------------------- |
    |                            |
    |  Transition: RTR → RTS     |  Transition: RTR → RTS
    |  RTU (Ready To Use)        |
    | -------------------------> |
    |                            |
    |  Data Transfer             |
    | <========================>|
```

## RoCE v2 Encapsulation

RoCE v2 encapsulates IB Transport packets in UDP/IP:

```
+--------------------+
| Ethernet Header    |  14 bytes
+--------------------+
| IP Header          |  20 bytes (IPv4) / 40 bytes (IPv6)
+--------------------+
| UDP Header         |  8 bytes (dest port = 4791)
+--------------------+
| IB Transport (BTH) |  12 bytes
+--------------------+
| IB Payload         |  variable (up to MTU)
+--------------------+
| ICRC               |  4 bytes (Invariant CRC)
+--------------------+
| Ethernet FCS       |  4 bytes
+--------------------+
```

## Flow Control and Congestion Control

### Link-Level Flow Control (InfiniBand)

Credit-based: receiver grants credits; sender cannot exceed available credits:

```
Receiver: I have N credits for you
Sender (N packets sent): N credits consumed
Receiver processes 5 packets: returns 5 credits
```

### PFC (Priority Flow Control) for RoCE

IEEE 802.1Qbb PFC enables lossless Ethernet required by RoCE:

```
8 priority levels; each independently paused
PFC pause frame: "stop sending on priority X for time T"
Prevents packet loss due to congestion
```

### DCQCN (Data Center QCN) for RoCE v2

End-to-end congestion control:
1. Switches mark packets with ECN when congested
2. Receiver reflects ECN marks in CNP (Congestion Notification Packet)
3. Sender reduces injection rate on receiving CNP
4. Rate recovery using additive-increase algorithm

## Kernel Bypass Mechanism

### Userspace Verbs Flow

```
1. Application calls ibv_post_send(qp, wr)
       |
2. Libibverbs writes WQE to Send Queue
   (via mmap'd doorbell page — no syscall!)
       |
3. HCA fetches WQE via DMA
       |
4. HCA processes the transport operation
   (segmentation, checksum, ACK handling in hardware)
       |
5. HCA writes CQE to Completion Queue in host memory
       |
6. Application polls CQ: ibv_poll_cq(cq, 1, &wc)
   (no syscall, direct memory read)
```

### Doorbell Mechanism

Doorbells notify the HCA of new work without a kernel transition:

```
mmap'd BAR space:
  Page 0: Doorbell page (write-only, generates PCIe write TLP)
     Offset 0x00: Send Queue Doorbell (QPN, new tail index)
     Offset 0x04: Receive Queue Doorbell
  Page 1: BlueFlame page (for small inline data writes)
```

## Zero-Copy Data Path

### Traditional: 3 copies minimum

```
App Buffer → Kernel Socket Buffer (copy 1)
           → Protocol Stack processing (CPU touches every byte)
           → DMA Engine copies to NIC (copy 2, technically)
           → NIC → Wire
```

### RDMA: 0 copies

```
App Registered Memory Region → NIC DMA Engine reads directly → Wire
                                            or
Wire → NIC DMA Engine writes directly → App Registered Memory Region
```

## Implementation in `rdma.c`

The C99 simulation in this module provides:

| Function | Purpose |
|---|---|
| `rdma_init()` | Initialize RDMA context with PD and empty MR/CQ/QP tables |
| `rdma_reg_mr()` | Register a memory region with auto-assigned lkey/rkey |
| `rdma_create_qp()` | Create a QP in a specified initial state |
| `rdma_modify_qp()` | Transition QP between states (RESET→INIT→RTR→RTS) |
| `rdma_post_send()` | Post a WQE to the SQ (requires RTS state) |
| `rdma_remote_write()` | Simulate one-sided RDMA write (memcpy + CQE) |
| `rdma_poll_cq()` | Poll the completion queue for finished operations |
| `rdma_dump_qp()` | Display QP state information |
| `rdma_dump_mr()` | Display all registered memory regions |

## References

- MIT 6.829 Lecture Notes: RDMA, Datacenter Transport
- InfiniBand Trade Association (IBTA) Specification Vol. 1
- RFC 5040: Remote Direct Memory Access Protocol (RDMAP)
- Mellanox Programming Manual: RDMA Aware Networks
- Linux RDMA Subsystem Documentation (drivers/infiniband/)

---

*Generated for mini-network-hardware project*
