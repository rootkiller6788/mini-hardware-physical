# mini-nic-sim — NIC Architecture Deep Dive

## Overview

The Network Interface Card (NIC) is the hardware component that connects a host computer to a network. Modern NICs are sophisticated devices with their own processors, memory, and DMA engines. This document provides a comprehensive analysis of NIC architecture as modeled in `nic_arch.h` and `nic_arch.c`, covering ring buffer structures, DMA operation, interrupt handling, and advanced features such as MSI-X and Receive Side Scaling (RSS).

## NIC Architecture Block Diagram

```
+------------------------------------------------------------------+
|                           HOST SYSTEM                             |
|  +-----------+   +-----------+   +-----------------------------+  |
|  |   CPU     |   |  Memory   |   |     PCIe Bus                 |  |
|  +----+------+   +-----+-----+   +--------------+--------------+  |
|       |                |                         |                 |
+---------------------------------------------------+----------------+
                                                    |
          +-----------------------------------------+----------+
          |                   NIC (PCIe Endpoint)              |
          |  +-----------+  +-----------+  +----------------+  |
          |  | DMA Engine|  | TX Queue  |  | RX Queue       |  |
          |  | (BD Ring) |  | (Ring Buf)|  | (Ring Buffer)  |  |
          |  +-----------+  +-----------+  +----------------+  |
          |  +-----------+  +-----------+  +----------------+  |
          |  | MAC/PHY   |  | Checksum  |  | TSO/LRO Engine |  |
          |  | (SerDes)  |  | Offload   |  |                |  |
          |  +-----+-----+  +-----------+  +----------------+  |
          +--------|--------------------------------------------+
                   |
              +----+----+
              |  RJ-45  |   <--- Ethernet Cable
              +---------+
```

## Ring Buffers: TX and RX Descriptor Rings

### Concept

Ring buffers are circular queues that hold descriptors, which are data structures describing the location, length, and status of network packets in host memory. The NIC and driver communicate through these rings without costly synchronization primitives.

### TX Descriptor Ring (NICTxRing)

The TX ring operates as a producer-consumer model:

- **Producer (Driver)**: Fills descriptors at the `tail` position.
- **Consumer (NIC)**: Reads descriptors starting from `head`, processes them, and advances `head`.

```
     Head (NIC reads here)
       |
       v
+---+---+---+---+---+---+---+---+
| D | D | D |   |   |   |   |   |  <-- Ring of descriptors
+---+---+---+---+---+---+---+---+
               ^
               |
           Tail (Driver writes here)
```

**Descriptor Fields**:
| Field       | Bits | Description                                    |
|-------------|------|------------------------------------------------|
| buffer_addr | 64   | Physical address of the packet buffer in RAM   |
| length      | 32   | Length of the packet data in bytes             |
| flags       | 16   | EOP (End of Packet), SOP (Start of Packet), CSUM_OFFLOAD, TSO |
| status      | 16   | Completion status set by NIC after DMA         |

**Workflow**:
1. Driver allocates a socket buffer (sk_buff) in kernel memory
2. Driver fills a TX descriptor with the buffer's physical address and length
3. Driver sets SOP/EOP flags and optionally enables checksum offload or TSO
4. Driver writes to the TX doorbell register (advances tail)
5. NIC's DMA engine reads descriptors from head
6. NIC DMAs packet data from host memory to NIC internal FIFO
7. NIC transmits the packet on the wire
8. NIC writes completion status to the descriptor
9. NIC advances head and raises TX completion interrupt

### RX Descriptor Ring (NICRxRing)

The RX ring differs in that descriptors are pre-allocated by the driver:

- **Producer (Driver)**: Pre-posts empty buffers at `tail`.
- **Consumer (NIC)**: Writes received data into buffers at `head`.

**Workflow**:
1. Driver allocates empty packet buffers and posts descriptors
2. NIC receives a packet from the wire
3. NIC's DMA engine writes the packet data to the buffer at `head`
4. NIC updates the descriptor with actual length and sets status
5. NIC advances head and raises RX interrupt
6. Driver processes the received packet
7. Driver replenishes the ring with new empty buffers at `tail`

## DMA Engine

Direct Memory Access (DMA) is the mechanism by which the NIC reads from and writes to host memory without CPU involvement. This is critical for high throughput networking.

### Operation Modes

1. **Descriptor-Based DMA**: The NIC follows a linked list or ring of descriptors, each pointing to a physically contiguous buffer. This is the most common mode.

2. **Packet-Based DMA**: The NIC DMAs entire packets in a single operation. Used in simpler NICs with limited descriptor support.

3. **Scatter-Gather DMA**: The NIC can gather packet data from multiple non-contiguous buffers (scatter for TX) and split received packets across multiple buffers (gather for RX). This is essential for supporting jumbo frames and TSO.

### DMA Performance Metrics

- **Latency**: Time from descriptor post to DMA completion, typically 1-5 microseconds
- **Bandwidth**: Limited by PCIe lane speed and width
- **Interrupt Coalescing**: Reduces interrupt rate by batching completions

## Interrupt Management

### Traditional Interrupts

Each packet triggers an interrupt, which can overwhelm the CPU at high packet rates (e.g., 10 Gbps line rate at 64-byte packets = ~14.88 Mpps).

### Interrupt Moderation / Coalescing

The NIC delays interrupt generation to batch multiple packet events:

```
Without coalescing:  INT INT INT INT INT INT ...
With coalescing:     --------INT--------INT---- ...
```

**Parameters**:
- `rx-usecs`: Maximum microseconds to wait before RX interrupt
- `rx-frames`: Maximum frames to accumulate before RX interrupt
- `tx-usecs`: Same for TX
- `tx-frames`: Same for TX

The tradeoff is between latency (lower is better) and CPU utilization (higher coalescing reduces interrupts).

### MSI-X (Message Signaled Interrupts Extended)

MSI-X is a PCIe feature that allows a device to allocate multiple interrupt vectors:

- Each vector can be assigned to a specific CPU core
- Enables RSS (Receive Side Scaling) and per-queue interrupt steering
- Avoids shared interrupt overhead (no IRQ line sharing)
- Up to 2048 vectors per device

**MSI-X Table Structure**:
| Field         | Description                          |
|---------------|--------------------------------------|
| Message Address | Destination APIC ID + bits          |
| Message Data   | Interrupt vector number              |
| Vector Control | Mask bit for interrupt masking       |

## Receive Side Scaling (RSS)

RSS distributes incoming packets across multiple receive queues, each associated with a different CPU core. This enables parallel packet processing and prevents a single core from becoming a bottleneck.

### RSS Hash Computation

The Toeplitz hash is computed over a tuple of packet header fields:

```
Hash = Toeplitz(Key, SrcIP, DstIP, SrcPort, DstPort)
```

The hash result modulo N determines the RX queue:
```
Queue = Hash % num_queues
```

### RSS Indirection Table

A programmable table maps hash values to specific queues, allowing flexible load balancing configurations:

```
Indirection Table:
  Hash value 0   -> Queue 0
  Hash value 1   -> Queue 1
  Hash value 2   -> Queue 0
  Hash value 3   -> Queue 3
  ...
```

## SR-IOV (Single Root I/O Virtualization)

SR-IOV allows a single physical NIC to present multiple virtual NICs (Virtual Functions or VFs) to the hypervisor, enabling direct hardware access by virtual machines:

- **Physical Function (PF)**: The full-featured PCIe function that manages the device
- **Virtual Function (VF)**: A lightweight PCIe function with limited configuration space
- Each VF gets its own descriptor rings, MSI-X vectors, and MAC address
- Bypasses hypervisor switching overhead (near-native performance)

## DPDK and Kernel Bypass

The Data Plane Development Kit (DPDK) takes NIC programming to the extreme by completely bypassing the kernel network stack:

- Poll-mode drivers (PMD) that spin-wait on descriptors instead of using interrupts
- Huge page memory for reduced TLB misses
- Lockless ring buffers for inter-thread communication
- Zero-copy packet forwarding between NIC ports

## Implementation in `nic_arch.c`

The `NIC` structure models all essential NIC components:

```
NIC {
    mac_addr[6]        <- Hardware MAC address
    ip_addr            <- IPv4 address
    tx_ring            <- TX descriptor ring with head/tail pointers
    rx_ring            <- RX descriptor ring with head/tail pointers
    registers[]        <- Memory-mapped I/O registers
    interrupts[]       <- Interrupt status flags (TX_COMPLETE, RX_READY, etc.)
    dma_engine_active  <- DMA engine state
    dma_total_bytes    <- Total bytes transferred via DMA
}
```

The `nic_process()` function simulates one processing cycle: DMA transfers complete, descriptors are updated, and interrupts are raised. The `nic_interrupt_handler()` function models interrupt service routine behavior.

## References

- Stanford CS144: Introduction to Computer Networking — NIC hardware, link layer
- Intel 82599 10 Gigabit Ethernet Controller Datasheet
- DPDK Programmer's Guide — Poll Mode Drivers
- PCI Express Base Specification, Revision 5.0
- Mellanox ConnectX-5 Ethernet Adapter Architecture

---

*Generated for mini-network-hardware project*
