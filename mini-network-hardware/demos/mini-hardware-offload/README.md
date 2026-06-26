# mini-hardware-offload — Hardware Offloading Engines

## Overview

Hardware offloading moves computationally expensive network processing tasks from the CPU to dedicated hardware on the NIC or other accelerators. This reduces CPU utilization, improves throughput, and lowers latency. This document surveys all major offloading technologies as modeled in `offload.h` and `offload.c`.

## Taxonomy of Network Offloads

```
Network Offloading
├── Stateless Offloads
│   ├── Checksum Offload (IP, TCP, UDP)
│   ├── TSO (TCP Segmentation Offload)
│   ├── LRO (Large Receive Offload)
│   └── GRO (Generic Receive Offload)
├── Flow Processing
│   ├── RSS (Receive Side Scaling)
│   ├── Flow Director / Steering
│   └── n-tuple Filters
├── Cryptographic Offloads
│   ├── TLS (kTLS, inline TLS)
│   ├── IPsec (ESP, AH)
│   └── MACsec (IEEE 802.1AE)
├── Compression
│   ├── Intel QAT (QuickAssist Technology)
│   └── NVMe Computational Storage
└── Advanced / SmartNIC
    ├── NVIDIA BlueField DPU
    ├── AWS Nitro Cards
    ├── Intel IPU (Infrastructure Processing Unit)
    └── Pensando DSC
```

## Checksum Offload

### Theory

IP, TCP, and UDP use the one's complement of the one's complement sum for error detection. Computing these checksums in software requires the CPU to touch every byte of every packet — a significant overhead at high packet rates.

### One's Complement Sum Algorithm

```
Step 1: Sum all 16-bit words in the data
Step 2: Add any carry bits back into the low 16 bits
Step 3: Take the one's complement (bitwise NOT)
```

```
Example: Checksum of [0xDEAD, 0xBEEF]
  0xDEAD + 0xBEEF = 0x19D9C
  Wrap carry: 0x9D9C + 1 = 0x9D9D
  Complement: ~0x9D9D = 0x6262
```

### Hardware Implementation

NIC hardware computes checksums during DMA transfers:
- **TX Path**: NIC computes IP/TCP/UDP checksums and inserts them into the packet before transmission. Driver only needs to compute the TCP pseudo-header checksum.
- **RX Path**: NIC verifies checksums on received packets and marks them as good/bad in the descriptor status field.

### Benefits
- Saves ~1 CPU cycle per byte of packet data
- At 100 Gbps, this represents ~12.5 GHz of CPU savings

## TCP Segmentation Offload (TSO)

### Problem

Applications often write large buffers (e.g., 64 KB) to a TCP socket. The kernel must segment this into MSS-sized packets (typically 1460 bytes for Ethernet). Without TSO, the CPU does the segmentation, requiring:
1. Memory allocation for each segment
2. Copy of segment data into each fragment buffer
3. Separate DMA operations for each fragment
4. Interrupt per completion

### TSO Operation

With TSO, the NIC performs segmentation in hardware:

```
Application writes 64 KB
         |
         v
Kernel creates one large descriptor
with TSO flag + MSS value
         |
         v
NIC TSO Engine:
  - Splits into MSS-sized segments
  - Generates correct IP ID for each
  - Computes TCP sequence numbers
  - Computes IP/TCP checksums
  - Transmits each segment
         |
         v
Wire: 44 segments of 1460 bytes each
```

### TSO Parameters
| Parameter | Typical Value | Description                           |
|-----------|---------------|---------------------------------------|
| MSS       | 1460          | Maximum Segment Size per segment      |
| Max size  | 64 KB         | Maximum TSO buffer size (some NICs: 256 KB) |
| Segment header template | Variable | Pre-computed header for cloning  |

### Performance
- Reduces CPU utilization by 70-90% for large TCP sends
- Reduces PCIe transactions (one descriptor vs N descriptors)
- Critical for 10 Gbps+ TCP throughput

## Large Receive Offload (LRO)

### Operation

LRO is the receive-side analog of TSO. When the NIC receives multiple TCP segments belonging to the same flow, it coalesces them into a single large packet before DMA to host memory:

```
Wire: [Seg1] [Seg2] [Seg3] [Seg4]
         |      |      |      |
         v      v      v      v
         NIC LRO Engine
              |
              v
Host Memory: [Large merged packet]
```

### Constraints
- Must have same source/destination IP and TCP ports
- Must have contiguous TCP sequence numbers
- Must have same TCP flags (no mixed SYN/FIN/RST)
- Timestamp values must be adjusted

### Downsides
- Can interact poorly with bridging/routing (packets may need to be re-segmented)
- Over-aggressive merging can break CUBIC/BIC TCP congestion control

## Generic Receive Offload (GRO)

GRO is a software-based (kernel) improvement over LRO:

- Merges packets after they reach the kernel, avoiding LRO's downsides
- Protocol-aware: supports TCP, UDP, SCTP, and tunneled protocols
- Respects forwarding requirements (maintains packet boundaries when bridging)
- Selective merging based on protocol-specific rules

## Receive Side Scaling (RSS)

### Purpose

Distributes incoming packets across multiple receive queues, each pinned to a different CPU core:

```
Incoming packets
    |
    v
RSS Hash (Toeplitz)
    |
    +---> Queue 0 ---> CPU 0
    +---> Queue 1 ---> CPU 1
    +---> Queue 2 ---> CPU 2
    +---> Queue 3 ---> CPU 3
```

### Hash Computation

```
hash = Toeplitz(secret_key,
                src_ip, dst_ip,
                src_port, dst_port)

queue = hash & (num_queues - 1) [for power-of-two queue counts]
```

### RSS Indirection Table

Reprogrammable table maps hash result to queue, enabling:
- Flow pinning: specific flows to specific cores
- Asymmetric queue handling: more queues on faster cores
- Dynamic rebalancing during live migration

## Flow Steering / Flow Director

Flow director enables precise control over which packets go to which queue:

- **Perfect Match Filters**: Exact match on specific 5-tuples; limited to ~8000 entries
- **ATR (Application Targeted Routing)**: NIC learns flow-to-queue mappings by observing transmitted packets
- **ntuple Filters**: Programmable filters matching on arbitrary header fields

## TLS Offload

### kTLS (Kernel TLS)

kTLS offloads the symmetric encryption portion of TLS to the kernel or NIC:

```
Application
    |
    v
TLS Library (OpenSSL) ---> kTLS
    |                          |
    | Handshake only           | Record encryption
    v                          v
Userspace              Kernel / NIC Hardware
```

Two levels of kTLS:
1. **Software kTLS**: Kernel performs AES-GCM using CPU cryptographic instructions (AES-NI)
2. **Hardware kTLS**: NIC performs AES-GCM in dedicated cryptographic engines

### Inline TLS Offload

Entire TLS record processing done on NIC:
- AES-GCM encryption/decryption and authentication
- TCP segmentation combined with TLS (fewer PCIe round trips)
- Requires NIC with inline crypto engines (e.g., Mellanox ConnectX-6 Dx)

## IPsec Offload

IPsec processing is computationally expensive (encryption + authentication per packet):

- **ESP Offload**: NIC performs Encapsulating Security Payload encryption and integrity
- **Full Offload**: NIC handles Security Association lookup, encryption, and packet formatting
- **Linux XFRM Interface**: Standard kernel API for IPsec offload

## Intel QuickAssist Technology (QAT)

Intel QAT is a hardware accelerator for cryptography and compression:

| Function             | Algorithm              | Throughput per Device |
|----------------------|------------------------|------------------------|
| Symmetric Crypto     | AES-128/256-GCM        | Up to 100 Gbps        |
| Asymmetric Crypto    | RSA, ECDH, ECDSA       | Up to 100K ops/sec    |
| Compression          | DEFLATE, LZ4, ZSTD     | Up to 100 Gbps        |
| Hash                 | SHA-1/256/384/512, MD5 | Up to 100 Gbps        |

Common in load balancers, VPN gateways, and storage appliances.

## NVIDIA BlueField DPU

The BlueField Data Processing Unit (DPU) is a SmartNIC with its own multicore ARM processor, DRAM, and accelerators:

```
+--------------------------------------------+
|            BlueField-2 DPU                  |
|  +--------+  +---------+  +--------------+ |
|  | 8x ARM |  | Crypto  |  | NVMe/RoCE     | |
|  | Cortex |  | Accele- |  | Offload        | |
|  | A72    |  | rator   |  | Engines        | |
|  +--------+  +---------+  +--------------+ |
|  +--------------------------------------+  |
|  |     ConnectX-6 Dx NIC ASIC            |  |
|  |  (2x 100GbE, PCIe Gen4, RoCE, NVMe)   |  |
|  +--------------------------------------+  |
+--------------------------------------------+
```

Use cases:
- OVS (Open vSwitch) offload
- NVMe-oF (NVMe over Fabrics) target
- Security policy enforcement
- Storage virtualization (SPDK)

## AWS Nitro Cards

AWS Nitro is a family of custom ASICs that offload virtualization functions:

| Card                  | Function                                        |
|-----------------------|-------------------------------------------------|
| Nitro VPC Card        | VPC networking (encapsulation, security groups) |
| Nitro EBS Card        | EBS storage virtualization                     |
| Nitro Instance Storage | NVMe local storage controller                  |
| Nitro Controller      | System management and monitoring               |

Each card frees up host CPU cycles for customer workloads, enabling bare-metal-like performance in virtualized environments.

## Implementation in `offload.c`

Our C99 implementation provides:

| Function                      | Purpose                                            |
|-------------------------------|----------------------------------------------------|
| `offload_engine_create()`     | Allocate and initialize an offload engine           |
| `offload_csum_compute()`      | Compute 16-bit one's complement checksum            |
| `offload_checksum_verify()`   | Verify a checksum against expected value            |
| `offload_tso_segment()`       | Split large packet into MSS-sized segments          |
| `offload_lro_merge()`         | Merge multiple received packets into one            |
| `offload_print_stats()`       | Display offload engine statistics                   |

The `OffloadEngine` structure tracks enable status and cumulative statistics (packets processed, bytes saved).

## References

- MIT 6.829: Computer Networks — Hardware Offloading, RDMA, SmartNICs
- UC Berkeley EE 122: Introduction to Communication Networks — TCP, Checksums, Segmentation
- Intel 82599 Datasheet — Checksum Offload, TSO, RSS
- Linux Kernel Documentation: networking/checksum-offloads.rst
- NVIDIA BlueField DPU Architecture Whitepaper
- AWS Nitro System Architecture

---

*Generated for mini-network-hardware project*
