# mini-rdma-transport — RDMA Internals Deep Dive

## Overview

Remote Direct Memory Access (RDMA) enables direct memory access from one host to another without involving the operating system of either machine. It provides zero-copy, kernel-bypass data transfer with microsecond-level latency — critical for high-performance computing, distributed storage, and financial trading systems. This document covers the internals of RDMA as modeled in `rdma.h` and `rdma.c`.

## Queue Pair Model

The Queue Pair (QP) is the fundamental communication abstraction in RDMA. Each QP consists of two work queues:

### Send Queue (SQ)

The SQ holds Work Queue Elements (WQEs) that describe outbound data transfer operations:
- **SEND**: Two-sided operation; receiver must post a RECV WQE
- **RDMA WRITE**: One-sided; writes directly to remote memory
- **RDMA READ**: One-sided; reads directly from remote memory
- **ATOMIC**: Atomic read-modify-write on remote memory

### Receive Queue (RQ)

The RQ holds WQEs posted by the application to receive incoming SEND operations. Note: RDMA WRITE and READ operations do NOT consume RQ WQEs on the remote side — they bypass the remote CPU entirely.

### Completion Queue (CQ)

The CQ collects Completion Queue Entries (CQEs) that signal completion of work requests. Both SQ and RQ completions are reported through CQs:

```
+------------+            +------------+
|   SEND WQE |--- SQ ---> |            |
+------------+            |            |
+------------+            |   QP (QPN) |---> CQ (CQE)
|   RECV WQE |--- RQ ---> |            |
+------------+            +------------+
```

## RDMA Operational States

Queues progress through a well-defined state machine:

```
RESET ----> INIT ----> RTR ----> RTS
              |          |          |
              v          v          v
           ERROR      ERROR      ERROR
```

| State | Description |
|-------|------------|
| RESET | Initial state; QP not configured |
| INIT  | QP initialized but cannot receive |
| RTR  | Ready to Receive; can accept incoming packets |
| RTS  | Ready to Send; fully operational, can send and receive |
| ERROR | Error state; QP must be reset |

**State Transition Prerequisites**:
1. **RESET → INIT**: Local QP attributes configured
2. **INIT → RTR**: Remote QP attributes exchanged, RQ posted
3. **RTR → RTS**: SQ attributes configured, SQ posted

## Memory Registration and Protection

Before memory can be accessed via RDMA, it must be registered:

### Memory Region (MR)

A registered memory region has:
| Field | Description |
|-------|------------|
| addr  | Virtual address of the registered buffer |
| len   | Length of the buffer in bytes |
| lkey  | Local key; used by the local HCA to access this MR |
| rkey  | Remote key; shared with remote peer for RDMA operations |

### Protection Domain (PD)

A Protection Domain groups QPs and MRs together, providing isolation:
- QPs can only access MRs within the same PD
- Enforces security boundaries between applications

### Registration Process

1. Application allocates memory (malloc, mmap)
2. Application calls `rdma_reg_mr()` to register the region with the HCA
3. HCA pins the memory (prevents swapping) and programs the IOMMU
4. HCA returns lkey and rkey to the application
5. Application shares rkey with remote peer via out-of-band mechanism

## One-Sided Operations

One-sided operations are RDMA's killer feature. They bypass the remote CPU:

### RDMA Write

```
Initiator                          Responder
+-------+                         +-------+
|  CPU  |                         |  CPU  |  (unaware of transfer)
+---+---+                         +---+---+
    |                                  |
+---+---+  RDMA WRITE             +---+---+
|  HCA  | ----------------------> |  HCA  |
+---+---+  (addr, rkey, data)    +---+---+
    |                                  |
    v                                  v
[Local Buffer]                  [Remote Memory]
```

The initiator specifies:
- Remote virtual address
- Remote key (rkey) for authorization
- Length of data to write
- Local buffer containing data

### RDMA Read

Similar to RDMA Write, but data flows from remote to local memory without remote CPU involvement.

### Atomic Operations

- **Fetch-and-Add**: Atomically add a value to remote memory and return old value
- **Compare-and-Swap**: Atomic CAS on remote memory
- Useful for distributed locking and synchronization

## InfiniBand Architecture

InfiniBand is the original RDMA transport, defining a complete fabric from the physical layer up:

### Layers
| Layer        | Function                                          |
|--------------|---------------------------------------------------|
| Physical     | 1x, 4x, 12x lanes; SDR, DDR, QDR, FDR, EDR, HDR   |
| Link         | Flow control, credit-based (no packet drop)        |
| Network      | LID-based routing within subnet                   |
| Transport    | Reliable Connection (RC), Unreliable Datagram (UD)|

### Transport Types
| Type | Acronym | Description                             |
|------|---------|-----------------------------------------|
| RC  | Reliable Connected    | Ordered, reliable, two-sided           |
| UC  | Unreliable Connected  | Unordered, connected                   |
| RD  | Reliable Datagram     | Reliable, unconnected                  |
| UD  | Unreliable Datagram   | Best-effort, unconnected               |
| XRC | Extended RC           | RC with shared receive queues          |
| DC  | Dynamically Connected | Connectionless reliable (Mellanox)     |

## RoCE (RDMA over Converged Ethernet)

RoCE adapts RDMA to Ethernet networks, enabling data center convergence:

### RoCE v1
- Link-layer protocol (EtherType 0x8915)
- Requires lossless Ethernet (PFC — Priority Flow Control)
- Limited to single broadcast domain (no IP routing)

### RoCE v2
- UDP-based encapsulation (port 4791)
- IP-routable across subnets
- Uses DCQCN for congestion control
- Dominant RDMA flavor in modern data centers

```
+--------------+
|  RDMA Payload|
+--------------+
|  InfiniBand  |
|  Transport   |
+--------------+
|  UDP Header  |
+--------------+
|  IP Header   |
+--------------+
|  Ethernet    |
+--------------+
```

## iWARP (Internet Wide Area RDMA Protocol)

iWARP runs RDMA over TCP/IP, providing advantages and disadvantages:

- **Advantage**: Works on any TCP/IP network, no special switch configuration
- **Disadvantage**: TCP adds latency and state; kernel TCP stack limits performance
- **Implementation**: RDMAP, DDP, and MPA layers on top of TCP

## Zero-Copy Data Path

Traditional TCP vs RDMA data paths:

### Traditional TCP (CPU-mediated)
```
App Buffer -> Kernel Socket Buffer -> TCP/IP Stack -> NIC Driver -> DMA -> NIC
  (copy 1)      (copy 2)                (copy 3)
```

### RDMA (Zero-copy)
```
App Buffer (Registered MR) -> HCA DMA -> Wire
  (direct, no CPU copies)
```

## Kernel Bypass

RDMA bypasses the kernel entirely:

1. Application calls `rdma_post_send()` via userspace library (libibverbs)
2. Libibverbs writes directly to the HCA's command registers via mmap'd PCIe BAR
3. HCA processes the WQE without kernel involvement
4. Completion is delivered directly to userspace via CQ polling

```
Userspace:   [Application] --> [libibverbs]
                |                   |
                | mmap'd BAR        |
                v                   v
Kernel:     [  RDMA Subsystem (registration only)  ]
                |                   |
Hardware:   [          HCA/NIC         ]
```

## Congestion Control

RDMA networks require lossless behavior for optimal performance:

- **IB**: Credit-based flow control at link level
- **RoCE v2**: DCQCN (Data Center Quantized Congestion Notification)
  - ECN marking by switches
  - Rate adjustment by end-host NICs
- **iWARP**: TCP congestion control (New Reno, CUBIC)

## Implementation Notes in `rdma.c`

The C99 implementation provides a self-contained simulation:

- `rdma_init()`: Creates a protection domain and initializes QP/CQ structures
- `rdma_reg_mr()`: Registers a memory region with auto-generated keys
- `rdma_create_qp()`: Creates QP with specified initial state
- `rdma_post_send()`: Posts WQE to SQ (requires QP in RTS state)
- `rdma_remote_write()`: Performs simulated one-sided RDMA write
- `rdma_poll_cq()`: Polls completion queue for completed operations

## References

- MIT 6.829: Computer Networks — RDMA, Datacenter Transport
- InfiniBand Architecture Specification, Volume 1
- RoCE v2 Specification (IBTA Annex A17)
- IETF RFC 5040: Remote Direct Memory Access Protocol (RDMAP)
- Linux kernel: drivers/infiniband/

---

*Generated for mini-network-hardware project*
