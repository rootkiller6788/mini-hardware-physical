# Knowledge Graph — lake_test_mini

## L1: Definitions (Complete)

| Entry | Type | File |
|-------|------|------|
| TestConfig | struct | lake_test_core.h |
| TestMode | enum | lake_test_core.h |
| BenchResult | struct | lake_test_core.h |
| HwCounterSet | struct | lake_test_core.h |
| TestSuite | struct | lake_test_core.h |
| PlatformInfo | struct | lake_test_core.h |
| LakeWorkloadProfile | struct | lake_test_core.h |
| CacheBenchConfig | struct | cache_bench.h |
| SimCache | struct | cache_bench.h |
| BwTestOperation | enum | mem_bandwidth.h |
| BwDataPoint | struct | mem_bandwidth.h |
| BwBenchResult | struct | mem_bandwidth.h |
| NumaTopology | struct | mem_bandwidth.h |
| MemHierarchySweep | struct | mem_bandwidth.h |
| IoBenchResult | struct | io_profile.h |
| IoScheduler | struct | io_profile.h |
| NvmeQueuePair | struct | io_profile.h |
| WorkloadConfig | struct | workload_gen.h |
| AccessSequence | struct | workload_gen.h |
| QueryPlan | struct | workload_gen.h |
| RooflineAnalysis | struct | perf_model.h |
| AmdahlModel | struct | perf_model.h |
| UslModel | struct | perf_model.h |
| PerfPredictor | struct | perf_model.h |
| DescriptiveStats | struct | result_analyze.h |
| ConfidenceInterval | struct | result_analyze.h |
| AnovaResult | struct | result_analyze.h |
| TimeSeriesDecomp | struct | result_analyze.h |

## L2: Core Concepts (Complete)

- Cache hierarchy simulation (L1/L2/L3 LRU)
- Memory bandwidth measurement (STREAM kernels)
- I/O queue depth scaling analysis
- Data lake workload characterization
- Performance counter modeling
- NUMA topology modeling
- NVMe queue pair protocol
- Volcano query execution model
- Synthetic workload generation

## L3: Engineering Structures (Complete)

- Test suite orchestration pipeline
- I/O scheduler framework (NOOP, DEADLINE, CFQ, MQ-DEADLINE)
- NVMe SQ/CQ doorbell mechanism
- Query plan operator tree
- Cost model parameter structure
- Performance sample collection pipeline

## L4: Standards/Theorems (Complete)

- AMAT (Average Memory Access Time) — Hennessy & Patterson
- Amdahl's Law (1967)
- Gustafson's Law (1988)
- Universal Scalability Law (Gunther 1993)
- Roofline Model (Williams, Waterman, Patterson — CACM 2009)
- Little's Law (L = λW)
- M/D/1 Queueing Theory
- M/M/1 Queueing Theory
- Central Limit Theorem
- Cache Power Law (Hartstein ISCA 2006)

## L5: Algorithms/Methods (Complete)

- Stack Distance Profiling (Mattson 1970)
- STREAM Benchmark (McCalpin 1995)
- Zipfian Sequence Generation (Gray & Shenoy 2000)
- OLS Linear Regression with Gaussian Elimination
- Saturation Curve Fitting (Exponential, Logistic, Power, Michaelis-Menten)
- One-way ANOVA
- Cohen's d Effect Size
- Welch's t-test
- Time Series Decomposition (Classical Additive)
- Outlier Detection (Z-score, IQR)
- Fisher-Yates shuffle (random permutation)

## L6: Canonical Problems (Complete)

- Cache miss curve construction
- Memory hierarchy boundary detection
- I/O queue depth optimization
- End-to-end hardware benchmark suite
- Hardware configuration recommendation
- I/O trace analysis and statistics

## L7: Applications (Complete)

- TPC-H workload generation and analysis
- TPC-DS workload generation and analysis  
- Log analytics workload generation
- Lakehouse buffer pool sizing
- Thread count optimization for query engines

## L8: Advanced Topics (Complete)

- ML-based performance prediction (OLS on hardware counters)
- NUMA-aware optimal data placement
- Central Limit Theorem verification through simulation
- Welch-Satterthwaite degrees of freedom correction

## L9: Industry Frontiers (Partial)

- Cloud instance benchmarking (PerfKitBenchmarker, SPEC Cloud)
- Multi-tenant performance isolation
- AI-driven autoscaling for lakehouse workloads (documented only)
