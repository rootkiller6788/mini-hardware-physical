# Course Alignment

## Module → Course Mapping

This document maps the `mini-network-hardware` modules to their corresponding courses and topics in the Stanford CS144, MIT 6.829, and UC Berkeley EE 122 curricula.

## Module-Course Mapping Table

| mini-network-hardware Module | Stanford CS144: Computer Networking | MIT 6.829: Computer Networks | UC Berkeley EE 122: Communication Networks |
|---|---|---|---|
| **nic_arch** (NIC Architecture) | Lecture 3: Packet Switching, NIC hardware, DMA, descriptor rings | N/A (lower-level than course scope) | Lecture 8: Link Layer — NIC functions, PCIe, DMA |
| **mac** (MAC/PHY Layer) | Lecture 19: Ethernet, CSMA/CD, MAC addressing, CRC | Lecture 3: Link Layer Protocols, Ethernet | Lecture 9: Ethernet, Switching, VLANs |
| **switch_fabric** (Switch Fabric) | Lecture 20: Bridging, Switching, Learning Bridge, STP | Lecture 7: Switch Design, Crossbar, Banyan Networks | Lecture 10: Switching, Spanning Tree, Switch Fabrics |
| **offload** (Hardware Offloading) | Lecture 22: TSO, LRO, checksum offload, RSS | Lecture 21: Hardware Offloading, TOE, NIC Functions | Lecture 19: TCP Offloading, Segmentation, Checksums |
| **rdma** (RDMA) | N/A (graduate-level topic) | Lecture 19: RDMA, InfiniBand, RoCE, Zero-Copy | Lecture 20: RDMA, Kernel Bypass, Datacenter Transport |
| **pcie** (PCIe Interface) | Lecture 1 (context): Host-NIC interface | N/A (hardware-level) | N/A (hardware-level) |

## Detailed Course Coverage

### Stanford CS144: Introduction to Computer Networking

| Topic | mini-hardware Module | Key Concepts Covered |
|---|---|---|
| Packet Switching | nic_arch | Descriptor rings, DMA, interrupt handling |
| Ethernet / MAC | mac | Frame format, MAC addresses, CRC32, MTU |
| Bridging & Switching | switch_fabric | Self-learning switches, MAC table, flooding, forwarding |
| Performance Optimizations | offload | TSO, LRO, checksum offload, TCP offload |

### MIT 6.829: Computer Networks

| Topic | mini-hardware Module | Key Concepts Covered |
|---|---|---|
| Ethernet & CSMA/CD | mac | Frame construction, CRC verification, statistics |
| Switch Design | switch_fabric | Crossbar, shared memory buffering, scheduling (FIFO/Priority/WRR) |
| RDMA & Kernel Bypass | rdma | Queue pairs, memory regions, one-sided operations, InfiniBand transport |
| Hardware Offloading | offload | TSO, LRO, GRO, checksum computation, SmartNIC architecture |

### UC Berkeley EE 122: Introduction to Communication Networks

| Topic | mini-hardware Module | Key Concepts Covered |
|---|---|---|
| Network Adapter Architecture | nic_arch | Ring buffers, DMA engine, interrupt coalescing, RSS |
| Link Layer | mac | Ethernet framing, MAC addresses, error detection (CRC) |
| Switching | switch_fabric | Self-learning switch, MAC table, flooding, VLANs |
| Transport & Offloading | offload | TCP Segmentation Offload, checksum offload, NIC optimizations |
| High-Speed Networking | rdma | RDMA one-sided operations, zero-copy, kernel bypass |

## Module Dependencies

```
nic_arch.h ──── standalone (NIC model)
mac.h      ──── standalone (data link layer)
pcie.h     ──── standalone (PCIe interface)
switch_fabric.h ─── depends on mac.h (uses MACFrame, MAC_ADDR_LEN)
offload.h  ──── standalone (offload engines)
rdma.h     ──── standalone (RDMA context, QP model)
```

## Learning Progression

Recommended study order following the courses' structure:

1. **Link Layer Foundation**: `mac.h` → understand Ethernet framing, MAC addressing, CRC
2. **Hardware Interface**: `pcie.h` → understand how NIC connects to host via PCIe
3. **NIC Internals**: `nic_arch.h` → understand descriptor rings, DMA, interrupts
4. **Switch Design**: `switch_fabric.h` → understand packet forwarding, MAC learning
5. **Performance Optimization**: `offload.h` → understand checksum offload, TSO, LRO
6. **Advanced Transport**: `rdma.h` → understand RDMA, one-sided operations, kernel bypass

## Testing and Verification

| Module | Demo Program | What It Tests |
|---|---|---|
| mac | `mac_demo.c` | MAC parsing, frame construction, CRC32, FCS verification |
| rdma | `rdma_demo.c` | RDMA init, MR registration, one-sided write, CQ polling |
| switch_fabric | `switch_sim_demo.c` | 4-port switch sim, MAC learning, forwarding, flooding |
| offload | `checksum_offload_demo.c` | IP checksum compute/verify, TSO segmentation |

---

*Generated for mini-network-hardware project*
