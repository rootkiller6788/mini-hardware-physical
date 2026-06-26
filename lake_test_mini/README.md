# lake_test_mini — 数据湖硬件测试框架 (Hardware Testing for Data Lake Workloads)

> **Bridges module 1 (hardware-physical) ↔ module 7 (data-engine-lakehouse)**
>
> 对标: MIT 6.004 · CMU 18-447 · Stanford EE282 · CMU 15-721

A **zero-dependency C implementation** of a hardware testing and benchmarking framework optimized for data lake/lakehouse workloads. Simulates cache hierarchy, memory bandwidth, I/O profiling, workload generation (TPC-H, TPC-DS, log analytics), performance modeling (Roofline, Amdahl, Gustafson, USL), and statistical analysis of benchmark results.

---

## Module Status: COMPLETE ✅

| Level | Status | Notes |
|-------|--------|-------|
| **L1** Definitions | **Complete** | 7 headers with full struct/typedef/enum API declarations |
| **L2** Core Concepts | **Complete** | Cache hierarchy, memory bandwidth, I/O profiling, workload characterization |
| **L3** Engineering Structures | **Complete** | Test suite orchestration, I/O scheduler, NVMe queue pair, Volcano cost model |
| **L4** Standards/Theorems | **Complete** | AMAT, Amdahl's Law, Gustafson's Law, USL, Roofline Model, M/D/1 & M/M/1 queueing |
| **L5** Algorithms/Methods | **Complete** | STREAM benchmark, Stack distance profiling, Zipfian generation, OLS regression, ANOVA |
| **L6** Canonical Problems | **Complete** | End-to-end benchmark suite, hardware recommendation engine, trace analysis |
| **L7** Applications | **Complete** | TPC-H/TPC-DS/Log Analytics workload generation, lakehouse config advisor |
| **L8** Advanced Topics | **Complete** | ML-based performance prediction, NUMA-aware placement, Central Limit Theorem verification |
| **L9** Industry Frontiers | Partial | Documented: cloud benchmarking, PerfKitBenchmarker, multi-tenant isolation |

- **include/ + src/ lines**: 5,277
- **Tests**: 47 tests, all passing
- **No TODO/FIXME/stub/placeholder**

---

## Quick Start

```bash
cd lake_test_mini
make test    # Build and run all 47 tests
make clean   # Clean build artifacts
```

---

## Core Definitions (L1)

| Type | Description |
|------|-------------|
| `TestConfig` | Hardware test configuration (working set, access patterns, test mode) |
| `BenchResult` | Benchmark measurement (throughput, latency percentiles, HW counters) |
| `TestSuite` | Orchestrates multiple tests, computes aggregate statistics |
| `PlatformInfo` | Auto-detected/simulated hardware description |
| `HwCounterSet` | CPU performance counters (IPC, cache miss rates, TLB, branches) |
| `LakeWorkloadProfile` | Data lake workload descriptor (TPC-H, TPC-DS, log analytics) |
| `SimCache` | Configurable LRU cache simulator |
| `StackDistProfile` | Stack distance histogram (Mattson's algorithm) |
| `BwDataPoint` | Memory bandwidth measurement (STREAM kernels) |
| `MemHierarchySweep` | Cache/TLB boundary detection |
| `IoBenchResult` | I/O queue depth scaling analysis |
| `NvmeQueuePair` | NVMe submission/completion queue simulation |
| `QueryPlan` | Volcano-style query plan with cost estimation |
| `AccessSequence` | Generated access sequence with entropy/Gini metrics |
| `RooflineAnalysis` | Roofline model with multiple data points |
| `AmdahlModel`, `GustafsonModel`, `UslModel` | Scalability models |
| `PerfPredictor` | OLS regression from hardware counters |
| `DescriptiveStats` | Full descriptive statistics (mean, percentiles, skewness, kurtosis) |
| `AnovaResult`, `CohensD`, `WelchTTest` | Statistical comparison methods |
| `TimeSeriesDecomp` | Classical additive decomposition for latency data |

---

## Core Theorems (L4)

| Theorem | Formula | Implementation |
|---------|---------|---------------|
| **AMAT** (Average Memory Access Time) | AMAT = hit_time + miss_rate × miss_penalty | `cache_compute_amat()` |
| **Amdahl's Law** (1967) | S = 1 / ((1-P) + P/N) | `amdahl_speedup()` |
| **Gustafson's Law** (1988) | S = N + (1-N)s | `gustafson_speedup()` |
| **Universal Scalability Law** (Gunther 1993) | C(N) = N / (1 + σ(N-1) + κN(N-1)) | `usl_throughput()` |
| **Roofline Model** (Williams 2009) | GFLOP/s = min(Peak, BW×AI) | `roofline_ridge()` |
| **Little's Law** | L = λW | `workload_littles_law_concurrency()` |
| **M/D/1 Queueing** | E[W] = λS²/(2(1-ρ)) | `queueing_md1_latency()` |
| **M/M/1 Queueing** | E[T] = 1/(μ−λ) | `queueing_mm1_response_time()` |
| **Cache Power Law** | MR(C) ∝ C^(−α) | `cache_fit_power_law_exponent()` |
| **Central Limit Theorem** | Sampling dist → Normal | `analyze_clt_verify()` |

---

## Core Algorithms (L5)

- **Stack Distance Profiling** (Mattson 1970): Miss curve prediction for any cache size
- **STREAM Benchmark** (McCalpin 1995): COPY, SCALE, ADD, TRIAD kernels
- **Zipfian Sequence Generation** (Gray & Shenoy 2000): Fast rejection sampling
- **OLS Linear Regression**: Performance prediction from hardware counters
- **Gaussian Elimination**: 8×8 matrix inversion for coefficient estimation
- **Saturation Curve Fitting**: Exponential, logistic, power-law, Michaelis-Menten
- **ANOVA (One-way)**: F-test for comparing multiple benchmark configurations
- **Time Series Decomposition**: Classical additive model (trend + seasonal + residual)
- **Outlier Detection**: Z-score method and IQR (Tukey's fences)

---

## Nine-School Course Mapping

| School | Course | Coverage |
|--------|--------|----------|
| **MIT** | 6.004 Computation Structures | Cache simulation, memory hierarchy |
| **MIT** | 6.823 Computer System Architecture | AMAT, cache miss curves, TLB modeling |
| **CMU** | 18-447 Computer Architecture | Roofline model, memory bandwidth, prefetch |
| **CMU** | 15-418 Parallel Computer Architecture | Amdahl/Gustafson/USL scalability |
| **CMU** | 15-721 Advanced Database Systems | Cost estimation, query plan optimization |
| **Stanford** | CS149 Parallel Computing | Roofline, GPU memory, bandwidth analysis |
| **Stanford** | CS240 Advanced Storage Systems | NVMe queue pair, I/O scheduling |
| **UC Berkeley** | CS267 HPC | Roofline model, STREAM benchmark |
| **清华** | 计算机体系结构 | 缓存层次结构, 内存带宽测试 |
| **清华** | 数据库系统概论 | 查询代价估算, Volcano模型 |

---

## File Structure

```
lake_test_mini/
├── Makefile              # make test — 47 tests pass
├── README.md             # This file
├── include/              # 7 headers, 1410 lines
│   ├── lake_test_core.h
│   ├── cache_bench.h
│   ├── mem_bandwidth.h
│   ├── io_profile.h
│   ├── workload_gen.h
│   ├── perf_model.h
│   └── result_analyze.h
├── src/                  # 7 sources, 3867 lines
│   ├── lake_test_core.c
│   ├── cache_bench.c
│   ├── mem_bandwidth.c
│   ├── io_profile.c
│   ├── workload_gen.c
│   ├── perf_model.c
│   └── result_analyze.c
├── tests/
│   └── test_core.c       # 47 unit tests
├── examples/             # 3 end-to-end demos
│   ├── demo_cache_bench.c
│   ├── demo_lake_workload.c
│   └── demo_perf_model.c
├── docs/
│   └── knowledge-graph.md
├── demos/
└── benches/
```

---

## Cross-Module Integration

This module feeds hardware performance data to:

```
lake_test_mini ──► data-engine(7)  ──► backend(8)  ──► frontend(9)
  (hardware          (query planner       (API service)    (dashboard)
   benchmarks)        optimization)
```

**Integration points:**
- `analyze_recommend_lake_config()` → optimal thread count & buffer pool for module 7
- `LakeWorkloadProfile` → workload characterization for query planner
- Hardware counter-based performance prediction → cost model refinement

---

## License

MIT
