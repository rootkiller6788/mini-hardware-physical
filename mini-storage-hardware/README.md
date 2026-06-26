# mini-storage-hardware — SSD/NAND Flash Storage Simulator

**Module Status: COMPLETE ✅**

> include/ + src/ = **3543 lines** ≥ 3000 ✓  
> `make test` — 23/23 tests pass ✓  
> L1-L6: Complete | L7: Partial+ (3 apps) | L8: Partial+ (4 topics) | L9: Partial (documented)

---

## Knowledge Coverage (L1-L9)

| Level | Name | Status | Key Deliverables |
|-------|------|--------|-----------------|
| **L1** | Definitions | ✅ Complete | NVMeCommand, FTL, FlashPage, ECCEncoder, LDPCCode, NANDCell, EnduranceSpec, BathtubModel |
| **L2** | Core Concepts | ✅ Complete | Flash Translation Layer, Fowler-Nordheim tunneling, ISPP, Read Disturb, CCI, Arrhenius retention |
| **L3** | Engineering Structures | ✅ Complete | FTL mapping table, NVMe SQ/CQ doorbell, Channel interleaving, PRP scatter-gather, Power state machine |
| **L4** | Standards/Theorems | ✅ Complete | Shannon's theorem (channel capacity), Weibull distribution, JEDEC JESD218/219, Write Amplification formula, Arrhenius law |
| **L5** | Algorithms/Methods | ✅ Complete | Hamming(7,4) ECC, BCH Berlekamp-Massey, Min-Sum LDPC, Cost-Benefit GC, WRR arbitration, ISPP programming |
| **L6** | Canonical Problems | ✅ Complete | SSD Controller simulation, Garbage Collection, Wear Leveling, NVMe command processing, ECC encode/decode pipeline |
| **L7** | Applications | ✅ Partial+ | NVMe Identify/Namespace mgmt, Power state management PS0-PS4, SLC Turbo Write buffer |
| **L8** | Advanced Topics | ✅ Partial+ | LDPC Min-Sum decoding, Reed-Solomon RS(255,239), Multi-Stream GC, Thermal throttling, Write cliff detection, Bathtub curve |
| **L9** | Industry Frontiers | ✅ Partial | AI-driven GC (documented), Computational Storage, ZNS SSD, CXL-attached memory |

---

## Core Definitions (L1)

| Type | Definition | Header |
|------|-----------|--------|
| `FTL` | Flash Translation Layer with page-level mapping | `ftl.h` |
| `FlashPage` / `FlashBlock` / `FlashPlane` | NAND flash geometry | `ftl.h` |
| `GarbageCollector` | GC with greedy/cost-benefit/aged-blocks policies | `gc.h` |
| `WearLeveler` | Dynamic/static/hybrid wear leveling | `wear_leveling.h` |
| `NVMeController` / `NVMeQueue` / `NVMeCommand` | NVMe 1.4 controller model | `nvme.h` |
| `SSDController` / `IOCommand` / `NANDChannel` | Full SSD data path | `ssd_controller.h` |
| `ECCEncoder` / `ECCType` | Hamming, BCH, LDPC, RS codes | `ecc.h` |
| `LDPCCode` / `LDPCDecoder` | Tanner graph, LLR messages | `ldpc.h` |
| `NANDCell` / `NANDDie` / `NANDDevice` | Cell physics model | `nand_model.h` |
| `EnduranceSpec` / `EnduranceTracker` / `BathtubModel` | JEDEC endurance | `endurance.h` |

---

## Core Theorems (L4)

1. **Shannon's Channel Coding Theorem (1948)**
   - C = B·log₂(1 + SNR) — capacity of AWGN channel
   - LDPC codes approach capacity within ~0.0045 dB
   - Function: `ldpc_shannon_limit(rate)`

2. **Write Amplification Formula (Desnoyers, 2014)**
   - WA = 1 / (1 - u/(1+op)) where u = utilization, op = overprovisioning
   - Function: `gc_write_amplification_formula(u, op)`

3. **Weibull Reliability Distribution**
   - F(t) = 1 - exp(-(t/η)^β)
   - β < 1: infant mortality, β = 1: random failures, β > 1: wear-out
   - Function: `weibull_cdf()`, `weibull_failure_rate()`, `weibull_mttf()`

4. **Arrhenius Data Retention Law**
   - t_ret = A·exp(Ea/(k·T)), Ea ≈ 1.1 eV for NAND
   - Function: `nand_retention_time(cell, T)`

5. **Hamming Bound for ECC**
   - 2^m ≥ n + 1 for single-error correction with m parity bits
   - Verified in `ecc_hamming_encode/decode`

---

## Core Algorithms (L5)

| Algorithm | Complexity | Implementation |
|-----------|-----------|---------------|
| Hamming(7,4) ECC | O(n) | `ecc_hamming_encode/decode` |
| BCH(15,7,2) with Berlekamp-Massey | O(n·t) | `ecc_bch_encode/decode` |
| RS(255,239) systematic encoding | O(n·(n-k)) | `ecc_rs_encode` |
| LDPC Min-Sum Belief Propagation | O(N·dv·iter) | `ldpc_min_sum_decode` |
| LDPC Sum-Product (tanh-based) | O(N·dv·iter) | `ldpc_bp_decode` |
| Gallagher LDPC construction | O(N·M) | `ldpc_gallager_construct` |
| Greedy GC victim selection | O(n_blocks) | `gc_select_victim` |
| Cost-Benefit GC (Rosenblum 1992) | O(n_blocks) | `gc_select_cost_benefit` |
| Wear-Aware GC | O(n_blocks) | `gc_select_wear_aware` |
| ISPP programming model | O(pulses) | `nand_ispp_program` |
| WRR Arbitration | O(n_queues) | `nvme_arbiter_next_sq` |
| Gamma function (Lanczos) | O(1) | `gamma_lanczos` |
| Hot/Cold data classifier | O(1) per update | `ftl_is_hot_lba` |

---

## Cross-Module Data Flow

```
Host Write → NVMe SQ/CQ (nvme.c)
           → SSD Controller Command Queue (ssd_controller.c)
           → FTL LBA→PBA mapping (ftl.c)
           → NAND page program (nand_model.c) with ECC encode (ecc.c/ldpc.c)
           → Wear Leveling check (wear_leveling.c)
           → GC trigger if free blocks < threshold (gc.c)
           → Endurance tracking update (endurance.c)
```

---

## 九校课程映射 (University Curriculum Mapping)

| School | Course | Module Coverage |
|--------|--------|----------------|
| **MIT** | 6.004 Computation Structures | ECC, Hamming codes, Shannon theory |
| **Stanford** | CS 144 Networking | NVMe protocol, SQ/CQ doorbell model |
| **Berkeley** | CS 162 Operating Systems | FTL (log-structured), GC, Wear Leveling |
| **CMU** | 15-410 OS | Flash Translation Layer, write amplification |
| **CMU** | 15-445 Database Systems | Log-structured merge, cost-benefit GC |
| **ETH** | 263-0006 Computer Architecture | SSD controller, channel interleaving |
| **Cambridge** | Part II Concurrent Systems | NVMe submission/completion queues |
| **清华** | 计算机体系结构 | NAND flash physics, ISPP, retention |
| **Georgia Tech** | CS 6290 HPCA | Memory hierarchy, SSD reliability |

---

## Files

```
mini-storage-hardware/
├── README.md               (this file)
├── Makefile                 make test → all 23 tests pass
├── include/                 (9 headers, 810 lines)
│   ├── ecc.h               ECC: Hamming, BCH, RS, Shannon
│   ├── ldpc.h              LDPC: Tanner graph, BP, Min-Sum
│   ├── ftl.h               FTL: page/block/hybrid mapping
│   ├── gc.h                GC: greedy, cost-benefit, wear-aware
│   ├── wear_leveling.h     WL: dynamic, static, hybrid
│   ├── nvme.h              NVMe: SQ/CQ, PRP, Identify, Arbitration
│   ├── ssd_controller.h    SSD: channels, power, thermal, QoS
│   ├── nand_model.h        NAND: ISPP, retention, disturb, CCI
│   └── endurance.h         Endurance: DWPD, TBW, Weibull, bathtub
├── src/                     (9 sources, 2733 lines)
│   ├── ecc.c               ECC implementation (375 lines)
│   ├── ldpc.c              LDPC decoder (339 lines)
│   ├── ftl.c               FTL + SLC cache (346 lines)
│   ├── gc.c                GC + WA analysis (386 lines)
│   ├── wear_leveling.c     WL + retention (263 lines)
│   ├── nvme.c              NVMe + Identify (272 lines)
│   ├── ssd_controller.c    SSD simulator (341 lines)
│   ├── nand_model.c        NAND physics (217 lines)
│   └── endurance.c         Endurance modeling (194 lines)
├── examples/                (6 demos)
│   ├── ftl_demo.c
│   ├── gc_demo.c
│   ├── wear_level_demo.c
│   ├── nvme_cmd_demo.c
│   ├── ecc_demo.c
│   └── integration_test.c  (23 assert-based tests)
├── tests/
├── docs/
└── benches/
```

## Completion Checklist

- [x] include/ + src/ ≥ 3000 lines (3543)
- [x] make test passes (23/23)
- [x] L1-L6 Complete
- [x] L7 Partial+ (≥2 applications)
- [x] L8 Partial+ (≥1 advanced topic with implementation)
- [x] L9 Partial (documented)
- [x] No TODO/FIXME/stub/placeholder
- [x] README.md with full knowledge coverage report
- [x] Cross-module integration via integration_test.c
- [x] All knowledge points mapped to independent functions
