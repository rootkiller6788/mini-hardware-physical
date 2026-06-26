# NIC Architecture

## Overview

This document provides an in-depth exploration of Network Interface Card (NIC) architecture, covering hardware components, the DMA engine, descriptor rings, interrupt management, Receive Side Scaling (RSS), SR-IOV virtualization, and the DPDK kernel-bypass model.

## NIC Block Diagram

```
                          HOST SYSTEM
+---------------------------------------------------------------+
|  CPU Cores (0..N)         |        System Memory (DRAM)       |
|                           |                                    |
|  +----+  +----+  +----+  |  +----------------------------+   |
|  | C0 |  | C1 |  | C2 |  |  | TX Descriptor Ring         |   |
|  +----+  +----+  +----+  |  | RX Descriptor Ring         |   |
|      |       |       |    |  | Packet Buffers (sk_buffs)  |   |
|      +-------+-------+    |  | Driver Structures          |   |
|              |            |  +----------------------------+   |
|         PCIe Root Complex |                                    |
+--------------|-------------+----------------|-----------------+
               |                              |
               v                              v
+---------------------------------------------------------------+
|                         NIC / ADAPTER                          |
|                                                                |
|  +-----------+    +-----------+    +------------------------+  |
|  | PCIe EP   |    | DMA       |    | On-Chip Memory (SRAM)  |  |
|  | (Gen3/4/5)|    | Engine    |    | - TX FIFO              |  |
|  |           |    | (BD Ring  |    | - RX FIFO              |  |
|  | MSI-X     |    |  Walker)  |    | - Descriptor Cache     |  |
|  +-----------+    +-----------+    +------------------------+  |
|                                                                |
|  +-----------+    +-----------+    +------------------------+  |
|  | Checksum  |    | TSO / LRO |    | MAC / PHY (SerDes)     |  |
|  | Offload   |    | Engines   |    | - 10G/25G/40G/100G     |  |
|  | Engine    |    |           |    | - PCS, PMA, PMD        |  |
|  +-----------+    +-----------+    +-----------+------------+  |
+----------------------------------------------------------------+
                                                   |
                                              +----+----+
                                              |  RJ-45  |
                                              |  / SFP  |
                                              +---------+
```

## DMA Engine

### Architecture

The DMA engine autonomously moves data between host memory and NIC internal buffers:

1. **Descriptor Fetch**: Engine reads the next descriptor from the TX ring in host memory
2. **Data Transfer**: Engine DMAs packet data from host memory to NIC TX FIFO
3. **Descriptor Writeback**: Engine writes completion status back to the descriptor
4. **Interrupt Generation**: Engine signals completion to the host

### Descriptor Ring Walk

```
TX Ring (in host memory, alloc'd by driver):

  Base Address Register (BAR) points here ---+
                                             |
    +--------+--------+--------+----         v
    | Desc 0 | Desc 1 | Desc 2 | ...  | Desc N-1 |
    +--------+--------+--------+----         ^
    |  Owned by NIC  |  Owned by Driver  |

  head (NIC reads) ----^
  tail (Driver writes) ------^
```

### DMA Transfer Types

| Transfer Type | Direction       | Trigger        |
|---------------|-----------------|----------------|
| TX Descriptor Fetch | Host → NIC   | Doorbell write |
| TX Data      | Host → NIC      | Descriptor     |
| RX Data      | NIC → Host      | Packet arrival |
| RX Descriptor Writeback | NIC → Host | Data DMA complete |

## Descriptor Rings

### TX Descriptor Format

```
Offset  Size  Field
------  ----  -----
0x00    8     Buffer Physical Address
0x08    4     Length (bytes)
0x0C    2     Flags:
               Bit 0: EOP (End of Packet)
               Bit 1: IFCS (Insert FCS)
               Bit 2: RS (Report Status)
               Bit 3: RSV (Reserved)
               Bit 4: IXSM (Insert IP Checksum)
               Bit 5: TXSM (Insert TCP/UDP Checksum)
               Bit 6: TSO (TCP Segmentation Offload)
0x0E    2     Status (written by NIC):
               Bit 0: DD (Descriptor Done)
               Bit 1-15: Reserved
```

### RX Descriptor Format

```
Offset  Size  Field
------  ----  -----
0x00    8     Buffer Physical Address
0x08    4     Length (written by NIC: actual received bytes)
0x0C    2     Flags:
               Bit 0: EOP
               Bit 1-15: Reserved
0x0E    2     Status:
               Bit 0: DD (Descriptor Done)
               Bit 1: EOP (End of Packet)
               Bit 2: IXSM (IP Checksum Valid)
               Bit 3: TXSM (TCP/UDP Checksum Valid)
               Bit 4: VP (VLAN Packet)
               Bit 5-6: Reserved
               Bit 7: RXE (RX Data Error)
```

### Ring Management

```
Initial State:
  head = 0, tail = N (all descriptors owned by driver)

Driver posts packets:
  For each packet, fill desc[tail], tail = (tail+1) % N
  Write tail to TDBA (TX Descriptor Base Address) tail register

NIC processes:
  Read desc[head], DMA data, set DD status bit
  head = (head+1) % N

Wrap Detection:
  When head or tail wraps, the NIC/driver must handle the modulo
  correctly. The ring must always have at least one empty slot
  to distinguish full from empty.
```

## Interrupt Moderation / Coalescing

### Problem Statement

At high packet rates, per-packet interrupts overwhelm the CPU:

```
Packet Rate vs Interrupt Rate:
  10 Gbps, 64-byte packets = ~14.88 Mpps
  Each packet → 1 interrupt
  ~14.88 million interrupts/sec
  At ~5 μs per interrupt → CPU saturated
```

### Interrupt Throttling Strategies

#### Time-Based (rx-usecs)
- Set a timer on first packet arrival
- Raise interrupt when timer expires
- All packets arriving during the window are processed in one interrupt
- Tradeoff: adds latency (window duration)

#### Frame-Based (rx-frames)
- Count received frames
- Raise interrupt after N frames
- Tradeoff: latency varies with packet rate

#### Adaptive (ITR - Interrupt Throttle Rate)
- NIC dynamically adjusts coalescing parameters based on traffic load
- Low traffic → low latency (small coalescing window)
- High traffic → high throughput (large coalescing window)

### Recommended Settings

| Scenario              | rx-usecs | rx-frames | tx-usecs | tx-frames |
|-----------------------|----------|-----------|----------|-----------|
| Low-latency trading   | 0-5      | 1         | 0-5      | 0         |
| Web server            | 20-50    | 64        | 20-50    | 64        |
| Bulk data transfer    | 100-200  | 256       | 100-200  | 256       |

## MSI-X and Interrupt Steering

MSI-X extends MSI (Message Signaled Interrupts) with multiple independent vectors:

```
Traditional INTx (Legacy):
  Single IRQ line shared by all devices → IRQ sharing overhead

MSI:
  Up to 32 vectors per device → dedicated IRQ per function

MSI-X:
  Up to 2048 vectors per device
  Each vector independently targetable to any CPU core
  Per-vector masking (no need to mask at device level)
```

### MSI-X Table

```
Vector | Message Address       | Message Data | Control
-------|-----------------------|--------------|--------
0      | 0xFEE00000 (APIC ID)  | 0x31 (IRQ#)  | 0x00000000
1      | 0xFEE01000            | 0x32         | 0x00000000
...
```

## Receive Side Scaling (RSS)

### Hardware Components

1. **RSS Hash Function**: Toeplitz hash over packet header fields
2. **Indirection Table**: Maps hash result to RX queue
3. **RSS Key**: Secret key for the hash function (programmable)

### Hash Input Fields

The hash can be computed over any combination of:
- Source IPv4/IPv6 address
- Destination IPv4/IPv6 address
- Source TCP/UDP port
- Destination TCP/UDP port
- (TCP) Flags may optionally be included

### RSS Configuration

```
ethtool -X eth0 equal 8    # 8 queues, equally weighted
ethtool -X eth0 weight 1 1 1 2  # Weighted distribution
ethtool -x eth0            # Show RSS configuration
ethtool -X eth0 hkey <96-byte-key>  # Set RSS hash key
```

## SR-IOV (Single Root I/O Virtualization)

### Architecture

```
+------------------------------------------+
|              Physical Server              |
|                                          |
|  +----------------+  +----------------+  |
|  |     VM 1       |  |     VM 2       |  |
|  |  +----------+  |  |  +----------+  |  |
|  |  | VF Driver |  |  |  | VF Driver |  |  |
|  |  +-----+----+  |  |  +-----+----+  |  |
|  +--------|-------+  +--------|-------+  |
|           |                   |          |
|  +--------+-------------------+-------+  |
|  |        Hypervisor (VMM)           |  |
|  |  +-----------------------------+  |  |
|  |  |        PF Driver            |  |  |
|  |  +-----------------------------+  |  |
|  +-----------------|-----------------+  |
|                    |                    |
+--------------------|--------------------+
                     |
          +----------+----------+
          |         PF          |
          |  +------+-------+   |
          |  | VF0  |  VF1  |   |
          |  +------+-------+   |
          |       SR-IOV        |
          |        NIC          |
          +---------------------+
```

### Key Concepts

| Term | Description |
|---|---|
| PF (Physical Function) | Full PCIe function with configuration and management |
| VF (Virtual Function) | Lightweight PCIe function with minimal config space |
| VEB (Virtual Ethernet Bridge) | Internal switch connecting VFs |
| VEPA (Virtual Ethernet Port Aggregator) | Reflective relay to external switch |

### Benefits
- Direct I/O for VMs (no hypervisor overhead)
- Near-native throughput and latency
- QoS per VF
- Live migration support (via bonding with para-virtual device)

## DPDK Model

DPDK provides a userspace, poll-mode driver framework for maximum packet processing performance:

### Architecture Changes

| Traditional Kernel | DPDK |
|---|---|
| Interrupt-driven | Poll-mode (busy-wait) |
| Kernel-to-userspace copies | Direct userspace access via UIO/VFIO |
| Socket buffers (sk_buff) | mbuf (DPDK buffer) |
| Standard 4K pages | Huge pages (2MB/1GB) |
| Per-packet syscall overhead | Batch processing |

### DPDK Receive Path

```
1. App calls rte_eth_rx_burst(port, queue, mbufs, nb_pkts)
2. PMD checks RX descriptor ring for new packets
3. PMD reaps completed descriptors
4. PMD fills mbufs with received packet metadata and pointers
5. App processes packets directly in userspace
6. App recycles mbufs by posting new RX descriptors
```

### Performance Characteristics

| Metric | Kernel Stack | DPDK |
|---|---|---|
| 64-byte packets (1 core) | ~1-3 Mpps | ~30-50 Mpps |
| Latency (min) | 10-30 μs | 5-10 μs |
| CPU utilization (10 Gbps) | 50-80% | 10-20% (1 core) |

## Implementation Summary

The `nic_arch.h` module models these concepts:

| C Structure | Real-World Analog |
|---|---|
| `NIC` | Full NIC device state |
| `NICTxRing` / `NICRxRing` | TX/RX descriptor rings with head/tail pointers |
| `NICDescriptor` | Individual ring descriptor with buffer_addr, length, flags, status |
| `NICRegister` | Memory-mapped I/O register (BAR0 register space) |
| `NICInterrupt` enum | Interrupt types: TX_COMPLETE, RX_READY, LINK_CHANGE, ERROR |

Functions simulate NIC operation at a high level without actual hardware access, suitable for educational understanding of NIC architecture.

---

*Generated for mini-network-hardware project*
