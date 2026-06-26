# mini-storage-hardware — 存储硬件 (C 语言实现)

> 参考 CMU 18-746 Storage Systems, Stanford CS240, MIT 6.5830

Storage hardware simulation library covering NAND flash, SSD controllers, FTL, wear leveling, garbage collection, ECC, and NVMe protocol. All implemented in C99 with no external dependencies beyond libc and libm.

## Module Table — 模块表

| Module | Header | Source | Description |
|--------|--------|--------|-------------|
| **FTL** | `include/ftl.h` | `src/ftl.c` | Flash Translation Layer simulator — logical-to-physical mapping, page/block/hybrid modes, write pointer management |
| **SSD Controller** | `include/ssd_controller.h` | `src/ssd_controller.c` | Multi-channel SSD controller — command queuing, channel parallelism, SRAM buffer, cycle-based simulation |
| **NVMe** | `include/nvme.h` | `src/nvme.c` | NVMe protocol — admin/I/O queue pairs, doorbell registers, command submission/completion |
| **Wear Leveling** | `include/wear_leveling.h` | `src/wear_leveling.c` | Wear leveling algorithms — dynamic, static, hybrid with erase count distribution statistics |
| **Garbage Collection** | `include/gc.h` | `src/gc.c` | GC for SSDs — GREEDY/COST_BENEFIT/AGED_BLOCKS policies, victim selection, write amplification |
| **ECC** | `include/ecc.h` | `src/ecc.c` | Error correction codes — Hamming(7,4), BCH(15,7,2), single/double-bit error detection |

## Directory Tree — 目录树

```
mini-storage-hardware/
├── README.md
├── Makefile
├── include/
│   ├── ftl.h
│   ├── ssd_controller.h
│   ├── nvme.h
│   ├── wear_leveling.h
│   ├── gc.h
│   └── ecc.h
├── src/
│   ├── ftl.c
│   ├── ssd_controller.c
│   ├── nvme.c
│   ├── wear_leveling.c
│   ├── gc.c
│   └── ecc.c
├── examples/
│   ├── ftl_demo.c
│   ├── wear_level_demo.c
│   ├── gc_demo.c
│   ├── nvme_cmd_demo.c
│   └── ecc_demo.c
├── demos/
│   ├── mini-ftl-sim/
│   │   └── README.md
│   ├── mini-ssd-controller/
│   │   └── README.md
│   ├── mini-wear-leveler/
│   │   └── README.md
│   └── mini-nvme-queue/
│       └── README.md
├── docs/
│   ├── course-alignment.md
│   ├── ftl-internals.md
│   ├── nand-flash-basics.md
│   └── nvme-protocol.md
├── tests/
└── benches/
```

## Build Commands — 构建命令

```bash
# Build all demos
make all

# Build individual demos
make ftl_demo
make wear_level_demo
make gc_demo
make nvme_cmd_demo
make ecc_demo

# Run all demos
make test

# Clean build artifacts
make clean
```

## Dependencies

- C99 compiler (GCC or Clang)
- libm (math library, for `sqrt()` in wear leveling)
- No other external dependencies

## Key Concepts

### FTL (Flash Translation Layer)
- **Address Mapping**: page-level, block-level, hybrid (BAST/FAST)
- **Write Pointer**: sequential append to free pages
- **Page States**: FREE → VALID → INVALID (no in-place update)

### SSD Controller
- **Channels**: 4 independent NAND channels, each with 4 planes
- **Latencies**: Read 50μs, Write 900μs, Erase 3ms (typical NAND)
- **Command Flow**: Issue Queue → FTL → NAND → Completion Queue

### NVMe Protocol
- **Queue Pairs**: Admin (1) + I/O (up to 8 in this model)
- **Doorbell Mechanism**: MMIO writes signal command arrival/completion consumption
- **Command Format**: 64-byte NVMe command with opcode, namespace, SLBA, NLB

### Wear Leveling
- **Dynamic**: allocate free blocks with lowest erase count
- **Static**: migrate cold data from low-wear to high-wear blocks
- **Statistics**: min, max, avg, stddev of erase count distribution

### Garbage Collection
- **Policies**: GREEDY (fewest valid pages), COST_BENEFIT, AGED_BLOCKS
- **Write Amplification**: WA = (host writes + GC writes) / host writes
- **Over-provisioning**: 7% default, affects GC frequency

### Error Correction
- **Hamming(7,4)**: encode 4 data bits → 7 code bits, correct 1 error
- **BCH(15,7,2)**: 7 data bits → 15 code bits, correct up to 2 errors
- **Applications**: per-page ECC in NAND flash, typically 40-100+ bits per 1KB sector
