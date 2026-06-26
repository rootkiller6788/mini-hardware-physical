# Hardware Offloading

## Overview

Hardware offloading refers to the migration of network processing tasks from the general-purpose CPU to specialized hardware on the NIC or dedicated accelerator cards. This document surveys the landscape of hardware offloading technologies: checksum offloading, TCP segmentation (TSO), large receive offload (LRO), RSS, flow steering, TLS offload, IPsec offload, compression acceleration (Intel QAT), and advanced SmartNIC/DPU architectures (NVIDIA BlueField, AWS Nitro).

## Why Offload?

### CPU Cost of Software Network Processing

| Operation               | CPU Cost (approx.)                    |
|-------------------------|---------------------------------------|
| IP Checksum             | 1 cycle per byte                      |
| TCP Checksum            | 1 cycle per byte (entire segment)     |
| TCP Segmentation        | Context switch + alloc + copies       |
| AES-128-GCM encryption  | ~1 cycle per byte (with AES-NI)       |
| memcpy (per byte)       | ~0.05 cycles (SIMD)                   |

At 100 Gbps (line rate):
- Checksums alone consume ~25 GHz of CPU (just touching bytes)
- With TSO + LRO + RSS, 1 core can drive 100 Gbps TCP

### The Offload Spectrum

```
                  Offload Level
  ┌─────┴──────┴──────┴──────┴──────┴──────┴──────┐
  None        CSUM    TSO/LRO   RSS     TLS/IPsec   Full TOE/DPU
  │           │       │         │       │           │
Pure SW   Checksums  Segment   Flow    Crypto      Full TCP
                      Merge     Steer              + App Offload
```

## Checksum Offload

### How It Works

The NIC computes or verifies IP/TCP/UDP checksums during DMA transfers:

```
TX Path:
  1. Driver builds packet in host memory
  2. Driver sets IXSM (IP checksum) or TXSM (TCP/UDP checksum) flag in descriptor
  3. NIC computes checksum during DMA read from host memory
  4. NIC inserts checksum at correct offset in the packet
  5. NIC transmits packet

RX Path:
  1. NIC receives packet
  2. NIC computes checksum during DMA write to host memory
  3. NIC compares with received checksum
  4. NIC sets IPCS (IP Checksum Valid) or L4CS (L4 Checksum Valid) in descriptor status
  5. Kernel reads status and avoids recomputing
```

### One's Complement Checksum Algorithm

```
uint16_t checksum(uint16_t *data, int nwords) {
    uint32_t sum = 0;
    for (int i = 0; i < nwords; i++)
        sum += data[i];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}
```

### Pseudo-Header for TCP/UDP Checksum

```
TCP/UDP checksum covers:
  Pseudo-header:  Src IP (4 bytes) + Dst IP (4 bytes) + Zero (1) + Protocol (1) + TCP Length (2)
  TCP/UDP header + payload
```

## TCP Segmentation Offload (TSO)

### Detailed Operation

```
Application writes 64 KB to TCP socket
        ↓
Kernel TCP stack:
  1. Builds template TCP/IP header
  2. Creates single large SKB with TSO metadata:
     - MSS: 1460
     - Header length: 66 bytes (Eth + IP + TCP)
     - Total payload: 65478 bytes
        ↓
NIC TSO Engine:
  1. Segments payload into MSS-sized pieces
  2. For each segment:
     a. Copies/clones TCP/IP header from template
     b. Updates IP ID (increments for each segment)
     c. Updates TCP sequence number (adds payload offset)
     d. Sets PSH flag on last segment
     e. Computes IP and TCP checksums
     f. Prepends Ethernet header
     g. Transmits segment
        ↓
Wire: 45 segments of 1460 bytes (+ 1 final smaller segment)
```

### NV (Network Virtualization) TSO

VXLAN/GENEVE tunnel encapsulation adds complexity:
- Outer headers (IP + UDP + VXLAN/GENEVE) must be prepended
- Each segment gets both inner and outer headers
- NICs with VXLAN TSO handle this transparently

## Large Receive Offload (LRO) & Generic Receive Offload (GRO)

### LRO (Hardware)

```
Wire segments: [Seg1] [Seg2] [...] [SegN]
                   │      │             │
                   └──────┴─────────────┘
                           │
                    NIC LRO Engine
                    (merges on-chip)
                           │
                      Single large
                      packet to host
```

**Merging Conditions**:
1. Same TCP connection (IPs + ports match)
2. Contiguous TCP sequence numbers
3. Same/similar TCP flags
4. Valid checksums on all segments

### GRO (Software)

```
Wire segments → NIC → Individual packets to kernel
                             │
                    Kernel GRO Engine
                    (merges in software)
                             │
                      Single large SKB
                      passed up stack
```

GRO vs LRO:
| Aspect             | LRO (Hardware)         | GRO (Software)          |
|--------------------|------------------------|-------------------------|
| Location           | NIC chip               | Kernel network stack    |
| Protocol support   | TCP only (typically)   | TCP, UDP, SCTP, tunnels |
| Bridging-friendly  | No (must re-segment)   | Yes (preserves boundaries) |
| Customizable       | Limited                | Highly                   |

## Receive Side Scaling (RSS)

### Architecture

```
                    Incoming packets
                          │
                          ▼
              ┌───────────────────────┐
              │   RSS Hash Function    │
              │   Toeplitz(Key, 5-tuple)│
              └───────────┬───────────┘
                          │
                  ┌───────┴───────┐
                  │               │
             hash % NQ        indirection
                  │               │
                  ▼               ▼
        ┌─────────────────────────────┐
        │      RX Queues (0..N-1)      │
        ├──────┬──────┬──────┬─────────┤
        │ Q 0  │ Q 1  │ Q 2  │  Q 3    │
        └──┬───┴──┬───┴──┬───┴────┬────┘
           │      │      │        │
           ▼      ▼      ▼        ▼
        CPU 0  CPU 1  CPU 2    CPU 3
```

### Toeplitz Hash

```
Function Toeplitz(key, input):
    result = 0
    for each bit b in input:
        if b == 1:
            result ^= leftmost 32 bits of key
        shift key left by 1
    return result
```

### Hardware RSS Indirection Table

```
Entry: Queue
  0      0      ─┐
  1      2       │ 2-to-1 indirection
  2      0       │ (hash values 0 and 2
  3      1       │  both go to queue 0)
  4      3       │
  ...    ...
```

## Flow Steering / Flow Director

### Types

| Type            | Match Criteria              | Capacity     |
|-----------------|----------------------------|--------------|
| Perfect Match   | Exact 5-tuple              | ~8000 rules  |
| ntuple Filter   | Arbitrary header fields    | ~32-128 rules|
| ATR             | Learned from TX patterns   | Auto-populated |

### Flow Director Configuration

```
ethtool -N eth0 flow-type tcp4 src-ip 10.0.0.1 dst-ip 10.0.0.2 \
        src-port 1234 dst-port 80 action 3
# Routes matching flows to RX queue 3
```

## TLS Offload

### kTLS (Kernel TLS)

```
Userspace
  │
  │ open(2), setsockopt(TLS)
  ▼
┌────────────────────────────────┐
│   Kernel TLS (kTLS)            │
│   ┌──────────────────────────┐ │
│   │  TLS Record Processing   │ │
│   │  - AES-GCM encrypt/decrypt│ │
│   │  - Record framing        │ │
│   └──────────┬───────────────┘ │
└──────────────┼─────────────────┘
               │
               ▼
         NIC (plain TCP segments, still TSO-eligible)
```

### Inline TLS Offload

```
Userspace
  │
  ▼
┌────────────────────────────────┐
│   Kernel (handshake only)       │
└──────────────┬─────────────────┘
               │
               ▼
┌────────────────────────────────┐
│   NIC (Full TLS processing)    │
│   - AES-GCM encryption         │
│   - Authentication tag         │
│   - Record formatting          │
│   - TCP segmentation (TSO)     │
└──────────────┬─────────────────┘
               │
               ▼
         Wire (encrypted TLS records as TCP segments)
```

## IPsec Offload

### Processing

IPsec ESP (Encapsulating Security Payload):
```
Original packet: [IP][TCP][Payload]
                   ↓
           ┌───────────────┐
           │  IPsec ESP     │
           │  1. Encrypt    │
           │  2. Auth tag   │
           └───────┬────────┘
                   ↓
[New IP][ESP Header][Encrypted IP+TCP+Payload][ESP Trailer][Auth Tag]
```

### Offload Levels

| Level              | NIC Handles                                              |
|--------------------|----------------------------------------------------------|
| Crypto offload     | AES-GCM encryption/decryption only                       |
| Packet offload     | NIC builds ESP header, encrypts, adds trailer            |
| Full offload       | NIC manages SA lookup, anti-replay, lifetime tracking    |

## Intel QuickAssist Technology (QAT)

### Architecture

```
┌──────────────────────────────────────┐
│          Intel QAT Accelerator        │
│  ┌──────────────────────────────────┐│
│  │ Ring Interface (request/response)││
│  └───────────────┬──────────────────┘│
│  ┌───────────────┴──────────────────┐│
│  │   Processing Engines             ││
│  │   ┌──────────┐   ┌────────────┐  ││
│  │   │ Crypto   │   │ Compression│  ││
│  │   │ - Sym    │   │ - DEFLATE  │  ││
│  │   │ - Asym   │   │ - LZ4      │  ││
│  │   │ - Hash   │   │ - ZSTD     │  ││
│  │   └──────────┘   └────────────┘  ││
│  └──────────────────────────────────┘│
└──────────────────────────────────────┘
```

### Performance

| Workload           | QAT Throughput    | CPU Equivalent      |
|--------------------|-------------------|---------------------|
| AES-256-GCM (4KB)  | 100 Gbps          | 20-30 cores         |
| RSA 2K decrypt     | 100K ops/sec      | 10-15 cores         |
| DEFLATE compress   | 100 Gbps (input)  | 15-20 cores         |

## NVIDIA BlueField DPU

### DPU System Architecture

```
┌─────────────────────────────────────────────┐
│              BlueField-2 DPU                 │
│                                              │
│  ┌──────────────────────┐   ┌─────────────┐ │
│  │  ARM Subsystem       │   │  Accelerator│ │
│  │  8x Cortex-A72       │   │  Blocks     │ │
│  │  16 GB DDR4          │   │  - Crypto   │ │
│  │  DPDK/SPDK support   │   │  - Regex    │ │
│  └──────────┬───────────┘   │  - NVMe-oF  │ │
│             │               └──────┬──────┘ │
│  ┌──────────┴──────────────────────┴──────┐ │
│  │         ConnectX-6 Dx ASIC             │ │
│  │  2x 100GbE | PCIe Gen4 x16 | RoCE v2  │ │
│  │  VXLAN/GENEVE | SR-IOV (1K VFs)       │ │
│  └────────────────────────────────────────┘ │
└─────────────────────────────────────────────┘
```

### Use Cases

| Use Case               | What Runs on DPU                          |
|------------------------|-------------------------------------------|
| OVS Offload            | Full OVS datapath, flow table matching    |
| NVMe-oF Target         | NVMe over Fabrics as local block device   |
| Storage Virtualization | SPDK-based virtual block devices to VMs   |
| Security Gateway       | Stateful firewall, DPI, IPsec termination |

## AWS Nitro System

### Card Architecture

```
┌─────────────────┐  ┌──────────────┐  ┌──────────────┐
│  Nitro VPC Card  │  │  Nitro EBS    │  │  Nitro        │
│  - VPC encap/dec │  │  Card         │  │  Controller   │
│  - Security Grp  │  │  - EBS virt   │  │  - BMC/IPMI   │
│  - Flow logs     │  │  - Encryption │  │  - Monitoring  │
│  - 50/100 Gbps   │  │  - 2.5M IOPS  │  │  - FW updates  │
└─────────────────┘  └──────────────┘  └──────────────┘
```

### Benefits
- Zero host CPU overhead for VPC networking
- EBS volumes appear as local NVMe (no host-side virt)
- Hardware-rooted security (Nitro Security Chip)
- Bare-metal performance for EC2 instances

## Implementation in `offload.c`

The C99 model provides:

| Component | Implementation |
|---|---|
| `offload_csum_compute()` | Full one's complement sum with carry wrap |
| `offload_checksum_verify()` | Verify computed against expected |
| `offload_tso_segment()` | Split large packet into MSS-sized chunks |
| `offload_lro_merge()` | Merge multiple packets into one |
| `OffloadEngine` struct | Type, enabled flag, cumulative stats |
| `offload_print_stats()` | Display packets offloaded and bytes saved |

## References

- UC Berkeley EE 122: TCP Segmentation, Checksums, Hardware Offloads
- IEEE 802.1Qbb: Priority-based Flow Control
- Intel Ethernet Controller XL710 Datasheet (Fortville) — TSO, RSS, Flow Director
- NVIDIA BlueField-2 DPU Software User Manual
- AWS Nitro System: re:Invent 2017-2022 presentations
- Linux Kernel Documentation: networking/checksum-offloads.txt, tls-offload.rst
- Intel QuickAssist Technology Software Guide

---

*Generated for mini-network-hardware project*
