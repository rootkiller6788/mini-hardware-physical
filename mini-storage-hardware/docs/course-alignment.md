# Course Alignment — 课程对照

> 本模块与以下计算机体系结构与存储系统课程内容相对应。

## CMU 18-746: Storage Systems

CMU 18-746 is a graduate-level course covering storage systems from device physics to distributed file systems. This module covers the hardware/physical layer topics.

| CMU 18-746 Topic | Chapter | mini-storage-hardware Module | Description |
|-----------------|---------|------------------------------|-------------|
| NAND Flash Basics | Ch 13-15 | `ftl.h`, `docs/nand-flash-basics.md` | Floating gate transistor, SLC/MLC/TLC, PEC, endurance, retention, read/write disturb |
| Flash Translation Layer | Ch 16-17 | `ftl.h/c`, `demos/mini-ftl-sim/` | Page-level, block-level, hybrid FTL, log buffer approaches (BAST/FAST) |
| Wear Leveling | Ch 18 | `wear_leveling.h/c`, `demos/mini-wear-leveler/` | Dynamic, static, hybrid wear leveling, erase count distribution |
| Garbage Collection | Ch 19 | `gc.h/c`, `examples/gc_demo.c` | GREEDY, COST_BENEFIT, AGED_BLOCKS policies, write amplification |
| SSD Architecture | Ch 14-15 | `ssd_controller.h/c`, `demos/mini-ssd-controller/` | Multi-channel, die/plane parallelism, command queuing, DRAM cache |
| Error Correction Codes | Ch 20 | `ecc.h/c`, `examples/ecc_demo.c` | Hamming(7,4), BCH, LDPC, Reed-Solomon |
| NVMe Protocol | Ch 21 | `nvme.h/c`, `demos/mini-nvme-queue/` | Queue pairs, doorbell mechanism, PRP/SGL, admin/I/O commands |

## Stanford CS240: Advanced Topics in Computer Systems

| CS240 Topic | mini-storage-hardware Module | Description |
|------------|------------------------------|-------------|
| Flash Storage Systems | `ftl.h`, `gc.h` | FTL design space, log-structured approaches |
| Storage Performance | `ssd_controller.h` | Channel parallelism, latency modeling, throughput analysis |
| Reliability | `ecc.h`, `wear_leveling.h` | Error correction, endurance management |
| Modern Storage Interfaces | `nvme.h` | NVMe protocol, PCIe attachment, queue-based interface |

## MIT 6.5830: Database Systems (Storage Layer)

| 6.5830 Topic | mini-storage-hardware Module | Description |
|--------------|------------------------------|-------------|
| Storage Hardware | `ftl.h`, `ssd_controller.h` | NAND flash physics, SSD internals |
| Storage Interfaces | `nvme.h` | NVMe command protocol, latency characteristics |
| Reliability & Recovery | `ecc.h`, `wear_leveling.h`, `gc.h` | Error correction, wear endurance, garbage collection |

## Topic Cross-Reference

| Concept | CMU 18-746 | Stanford CS240 | MIT 6.5830 | Module Implementation |
|---------|-----------|---------------|------------|----------------------|
| NAND Cell Physics | Ch 13 | Lecture 5 | Lecture 7 | `docs/nand-flash-basics.md` |
| SLC vs MLC vs TLC | Ch 13 | Lecture 5 | N/A | `PageType` enum in `ftl.h` |
| FTL Mapping | Ch 16-17 | Lecture 6 | N/A | `ftl.c` mapping functions |
| Write Amplification | Ch 19 | Lecture 6 | Lecture 8 | `FTLStats.write_amplification` |
| Wear Leveling | Ch 18 | N/A | N/A | `wear_leveling.c` |
| Garbage Collection | Ch 19 | N/A | N/A | `gc.c` GREEDY/CB/AGED |
| NVMe Queue Pairs | Ch 21 | Lecture 7 | N/A | `nvme.c` SQ/CQ + doorbell |
| ECC (Hamming/BCH) | Ch 20 | N/A | N/A | `ecc.c` |
| SSD Channel Parallelism | Ch 14 | Lecture 6 | N/A | `ssd_controller.c` |
| Over-provisioning | Ch 19 | N/A | N/A | `GarbageCollector.overprovisioning_pct` |

## Learning Path

Recommended study order for this module:

1. **NAND Flash Basics** (`docs/nand-flash-basics.md`) — Understand the physical medium
2. **FTL Internals** (`docs/ftl-internals.md`) — How flash looks like a block device
3. **SSD Controller** (`demos/mini-ssd-controller/`) — Complete system architecture
4. **Wear Leveling** (`demos/mini-wear-leveler/`) — Endurance management
5. **Garbage Collection** (`examples/gc_demo.c`) — Write amplification tradeoffs
6. **ECC** (`docs/nand-flash-basics.md` + `ecc.h`) — Data integrity
7. **NVMe Protocol** (`docs/nvme-protocol.md` + `demos/mini-nvme-queue/`) — Host interface
