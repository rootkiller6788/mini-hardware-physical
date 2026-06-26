# TPU Architecture — Evolution and Design

## Overview

The Tensor Processing Unit (TPU) is Google's custom ASIC for neural network workloads. Designed for the domain of dense linear algebra, TPUs have evolved through four generations from a simple inference accelerator to a training supercomputer.

| Generation | Year | Process | Key Innovation | Peak Per Chip |
|-----------|------|---------|---------------|---------------|
| TPUv1 | 2015 | 28nm | Systolic array, int8 inference | 92 TOPS (int8) |
| TPUv2 | 2017 | 16nm | bfloat16 training, HBM | 180 TFLOPS (bf16) |
| TPUv3 | 2018 | 16nm+ | 2× HBM, liquid cooling | 420 TFLOPS (bf16) |
| TPUv4 | 2021 | 7nm | Optical interconnect, sparse cores | 275 TFLOPS (bf16) |
| TPUv5p | 2023 | 5nm | Enhanced sparse, larger UB | 459 TFLOPS (bf16) |

## TPUv1: The Inference Engine

### Chip Architecture

```
┌────────────────────────────────────────┐
│              TPUv1 Chip                 │
│  ┌──────────────────────────────────┐  │
│  │   Host Interface (PCIe Gen3 ×16)   │  │
│  │   Memory: 8 GB DDR3-2133 (30 GB/s) │  │
│  ├──────────────────────────────────┤  │
│  │         Unified Buffer              │  │
│  │      (24 MB SRAM, 30 GB/s)         │  │
│  ├───┬──────────────┬────────────────┤  │
│  │   │   Systolic   │                │  │
│  │ W │   Array      │  Accumulators  │  │
│  │ e │   256×256    │  (4K × 32-bit) │  │
│  │ i │   INT8 MACs  │                │  │
│  │ g ├──────────────┴────────────────┤  │
│  │ h │   Vector Unit (Pool of ALUs)   │  │
│  │ t │   Activation Pipeline          │  │
│  │   │   (ReLU, Sigmoid, Tanh)       │  │
│  │ F │                                │  │
│  │ I │   Unified Buffer                │  │
│  │ F │   (24 MB SRAM, scratchpad)     │  │
│  │ O │                                │  │
│  └───┴────────────────────────────────┘  │
└────────────────────────────────────────┘
```

### Key Design Decisions

**CISC ISA**: ~20 instructions, each executing a large amount of work atomically. This simplifies control logic compared to RISC/VLIW.

**Deterministic execution**: No caches, no branch prediction, no speculative execution. Every instruction has a known, fixed latency — critical for real-time serving.

**8-bit integer only**: TPUv1 was designed for quantized inference (INT8 weights + INT8 or INT16 activations). No floating-point support.

**Weight Stationary dataflow**: Weights are loaded once into the systolic array, then many activations stream through. Optimal for batched inference.

**PCIe attached**: The TPU plugs into existing servers as a coprocessor (like a GPU), avoiding custom infrastructure.

### Performance Comparison (ISCA 2017)

| Workload | TPUv1 | K80 GPU | Haswell CPU |
|----------|-------|---------|-------------|
| MLP0 (fully connected) | 1.9× faster | 1.0× | 1.0× |
| LSTM1 (recurrent) | 3.5× faster | 1.0× | 1.0× |
| CNN1 (convolutional) | 2.3× faster | 1.0× | 1.0× |
| TOPS | 92 | — | — |
| TOPS/W | 1.23 | — | — |
| Die area | 331 mm² | 561 mm² | 662 mm² |
| TDP | 75W | 300W | 145W |

The TPU achieves 25-29× better TOPS/W than contemporary GPUs/CPUs, but this comparison is for INT8 vs FP32 — part of the gain comes from reduced precision.

### Roofline Analysis

The TPU's roofline model (ISCA 2017, Figure 4) shows:

```
Compute limit: 92 TOPS (horizontal roof)
Memory limit:  30 GB/s × Operations per Byte (sloped roof)

Ceiling achieved when:
  Operational Intensity ≥ 92 TOPS / 30 GB/s = 3070 ops/byte
```

For INT8 matrix multiply (3072 ops per byte of weights):
```
Ops per weight byte = 2 (M+N) / 1 = 2 (close to roofline intercept)
```

This means the TPU operates near the roofline "ridge point" — balanced between compute and memory.

## TPUv2/v3: Adding Training

### What Changed

TPUv2 introduced training capability through:
1. **bfloat16**: 16-bit floating point with the same exponent range as FP32 (8-bit exponent, 7-bit mantissa)
2. **HBM**: High-Bandwidth Memory (16 GB HBM2 at 600 GB/s)
3. **Two cores per chip**: Two 128×128 systolic arrays, each with their own UB
4. **Inter-chip interconnect**: 2D torus for multi-chip scaling (up to 256 chips)

### bfloat16 Format

```
bfloat16: 1 sign | 8 exponent | 7 mantissa
FP16:     1 sign | 5 exponent | 10 mantissa
FP32:     1 sign | 8 exponent | 23 mantissa

bfloat16 preserves FP32's dynamic range (±3.4×10³⁸) but with FP16's storage (16 bits).
Tradeoff: Less mantissa precision → 7 vs 10 bits
Benefit:  Easy to convert to/from FP32 (truncate last 16 bits)
         → No complex rounding logic needed
         → Same maximum value as FP32 → no overflow risk in training
```

### Training with bfloat16

Training uses a mixed-precision scheme:
- **Master copy**: FP32 weights (for gradient accumulation)
- **Compute**: bfloat16 matrix multiplies
- **Gradient accumulation**: FP32
- **Weight update**: FP32 (Adam, SGD momentum)

This provides ~2× throughput vs FP32 training with negligible accuracy loss.

### TPUv3 Improvements

TPUv3 was primarily a "scale-up" of TPUv2:
- 2× HBM capacity (32 GB HBM2)
- 1.34× clock speed (940 MHz vs 700 MHz)
- Liquid cooling (allows higher TDP within same thermal envelope)
- 2.3× performance (420 vs 180 TFLOPS)

## TPUv4: The Data Center Platform

### Architectural Innovations

**Optical Circuit Switch (OCS)**: TPUv4 uses optical interconnect between chips instead of electrical. Benefits:
- 4× less power than equivalent electrical switches
- Reconfigurable topology (software-defined interconnect)
- Lower latency (light travels faster through fiber than electrons through copper)

**Sparse Cores (SC)**: TPUv4 introduces dedicated hardware for sparse operations:
```
TPUv4 Chip:
  Core 0: MXU (128×128) + SC (Sparse Core)
  Core 1: MXU + SC
  Core 2: MXU + SC
  Core 3: MXU + SC
  ...
  Core 7: MXU + SC
```

The sparse core handles embedding lookups (naturally sparse) and sparse-dense matmuls.

**2D Torus + OCS**: 3D interconnect topology:
```
  Intra-chip: 2D torus (4×4 grid of cores within chip package)
  Inter-chip: OCS (optical, any-to-any reconfigurable)
```

This allows TPUv4 pods of 4096 chips to be connected in flexible topologies.

### TPUv4 Performance

| Metric | TPUv3 | TPUv4 | Improvement |
|--------|-------|-------|-------------|
| Peak TFLOPS/chip (bf16) | 123 | 275 | 2.2× |
| HBM | 32 GB HBM2 | 32 GB HBM2e | — |
| HBM BW | 1200 GB/s | 1200 GB/s | — |
| UB | 16 MB/core | 16 MB/core | — |
| Interconnect | Copper 2D torus | Optical OCS + 2D torus | 10× BW, lower latency |
| Process | 16nm | 7nm | — |
| TDP | ~300W | ~200W/chip | Lower (advanced node) |

### Scale: TPUv4 Pod

A TPUv4 pod connects 4096 chips:

```
1 Pod = 64 racks
1 Rack = 64 TPUv4 chips (16 boards × 4 chips)
1 Board = 4 TPUv4 chips

Total: 4096 chips × 8 cores/chip = 32,768 cores
Peak: 4096 × 275 TFLOPS = 1.1 EFLOPS (bf16)
Power: ~900 kW (including cooling, networking)
```

This is the hardware that trained PaLM, Gemini, and other Google-scale models.

## Unified Buffer Design

### What is the UB?

The Unified Buffer is an on-chip SRAM scratchpad — software-managed, fully addressable memory accessible by all compute units.

```
UB Size across generations:
  TPUv1: 24 MB (8 MB weights + 16 MB activations)
  TPUv2: 8 MB/core × 2 cores = 16 MB per chip
  TPUv3: 16 MB/core × 2 cores = 32 MB per chip
  TPUv4: 16 MB/core × 8 cores = 128 MB per chip
```

### UB vs Cache

| Property | Unified Buffer (TPU) | Cache (CPU) |
|----------|---------------------|-------------|
| Management | Software (compiler) | Hardware (LRU, etc.) |
| Latency | Deterministic | Variable (cache hit/miss) |
| Capacity | Large (16 MB per core) | Small (32 KB L1, 256 KB L2) |
| Associativity | Direct-mapped (simple) | 8-16 way set-associative |
| Coherence | Software-managed | Hardware (MESI protocol) |
| Miss handling | None (always present) | Cache miss → DRAM (100+ cycles) |

The UB's determinism is key for serving: the TPU runtime can precisely predict inference latency, enabling tight SLA guarantees.

### UB Memory Layout (Example)

For a ResNet-50 convolution layer:

```
┌─────────────────────┐ 0x000000
│ Weight matrix        │
│ (64 × 3 × 7 × 7)   │ 64×147 floats = 37,632 bytes
├─────────────────────┤
│ Input activations   │
│ (56 × 56 × 64)     │ 200,704 floats = 802,816 bytes
├─────────────────────┤
│ Output activations  │
│ (56 × 56 × 64)     │ 200,704 floats = 802,816 bytes
├─────────────────────┤
│ im2col workspace    │
│ (56×56 × 3×7×7)    │ 1,843,200 bytes (peak)
├─────────────────────┤
│ Instructions        │
│ (~500 bytes)        │
├─────────────────────┤
│ Free/Stack          │
└─────────────────────┘

Total used: ~3.5 MB (< 16 MB UB in TPUv3)
```

The UB is large enough that most layers fit entirely on-chip, avoiding off-chip DRAM access during computation.

## PCIe vs Direct Connect

TPUv1 uses PCIe Gen3 ×16 (≈12.8 GB/s bidirectional) to attach to the host CPU. This creates an "IO bottleneck" for small models:

```
ResNet-50 inference (single image):
  Model weights: 25.5 MB (INT8)
  Input: 150 KB (224×224×3)
  Output: 4 KB (1000-class softmax)
  PCIe transfer time: 25.7 MB / 12.8 GB/s ≈ 2.0 ms
  Compute time: 68k MACs / 92 TOPS ≈ 0.74 ns... (actually ~30 µs due to pipelining overhead for small batches)

For batch size 1, IO time dominates compute time.
For batch size 1024, compute time dominates (weights loaded once, shared across batch).
```

TPUv2+ switched to a custom interconnect (ICI) between TPU chips, removing PCIe from the critical path. The host loads data once; subsequent TPU-to-TPU communication uses ICI.

## TPU Pod Architecture

### Networking Topology

```
TPUv4 Pod (4096 chips):

  ┌─[Rack 0]──[Rack 1]──...──[Rack 63]─┐
  │  64 TPUv4   64 TPUv4        64 TPUv4 │ ← Electrical (within rack)
  │    chips      chips           chips   │
  └────┬──────────┬───────────────┬──────┘
       │          │               │
  ┌────┴──────────┴───────────────┴──────┐
  │       Optical Circuit Switch (OCS)     │ ← Optical (between racks)
  └──────────────────────────────────────┘
```

ICI (Inter-Chip Interconnect) within a rack: 50 GB/s per link, 2D torus
OCS between racks: Dynamically reconfigurable optical paths

### Training Large Models

For model-parallel training (each chip holds part of the model):

```
Data parallelism:   Replicate model across chips, split batch
Model parallelism:  Split model layers across chips, pipeline
Tensor parallelism: Split individual ops across chips (matrix-parallel matmul)

TPUv4 pod supports all three simultaneously (3D parallelism).
```

Pipeline parallelism across 4096 chips:
```
Stage 0: Embedding (chip 0-7)
Stage 1: Transformer Block 1 (chip 8-15)
...
Stage 63: Transformer Block 63 (chip 504-511)
Stage 64: LM Head (chip 512-519)
...
```

## Comparison: TPU vs GPU vs Custom ASICs

| Property | TPUv4 | A100 GPU | Cerebras CS-2 |
|----------|-------|----------|---------------|
| Architecture | Systolic array | SIMT + Tensor Cores | Wafer-scale systolic |
| Peak TFLOPS (bf16) | 275 | 312 | ? |
| Memory | 32 GB HBM2e | 80 GB HBM2e | 40 GB SRAM |
| Memory BW | 1200 GB/s | 2039 GB/s | 20 PB/s |
| Interconnect | OCS (optical) | NVLink + InfiniBand | SwarmX fabric |
| Programming | JAX, TensorFlow | CUDA | CSL (custom) |
| Power | ~200W | 400W | ~20 kW (whole wafer) |
| Precision | bf16, int8 | fp32, fp16, bf16, int8, tf32, fp8 | fp16 |

The TPU trades flexibility for efficiency: simpler programming model (JAX maps naturally to systolic arrays), deterministic execution, and purpose-built networking.

## Key Formulas

### Systolic Array Throughput

```
INT8 TOPS = 2 × N² × f_chip × 10⁻¹²

where N = systolic array dimension (128 for v2-v4, 256 for v1)
      f_chip = clock frequency (MHz → 10⁻¹² for TOPS)
      factor 2 = 1 multiply + 1 add per MAC
```

### Memory Bandwidth Requirements

```
Required BW = Total MACs × Bytes per Operand / Compute Cycles

For TPUv4 systolic array:
  128 × 128 = 16,384 MACs/cycle
  bfloat16 weights: 2 bytes per weight
  Required weight BW = 16,384 × 2 = 32,768 bytes/cycle
  At 1.05 GHz: 32,768 × 1.05×10⁹ = 34.4 TB/s needed but only 1.2 TB/s HBM available

  → Relies on UB to reuse weights (weight stationary dataflow)
  → UB provides effective BW >> HBM BW by eliminating redundant DRAM reads
```

### Utilization Efficiency

```
Computation utilization = (Effective TOPS) / (Peak TOPS) × 100%

For matrix multiply:
  Util ≈ min(M, N) / Array_Dim × min(K, Array_Dim) / Array_Dim

  Best case: M, N, K all multiples of array dimension
  Worst case: Very small dimensions (e.g., depthwise convolution: 3×3×1)
```

## References

1. Jouppi, N.P. et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017
2. Jouppi, N.P. et al., "Ten Lessons from Three Generations Shaped Google's TPUv4i", ISCA 2021
3. Norrie, T. et al., "Google's Training Chips Revealed: TPUv2 and TPUv3", Hot Chips 2020
4. Jouppi, N.P. et al., "TPUv4: An Optically Reconfigurable Supercomputer for Machine Learning", ISCA 2023
5. Dean, J. et al., "Large Scale Distributed Deep Networks", NIPS 2012
6. Abadi, M. et al., "TensorFlow: A System for Large-Scale Machine Learning", OSDI 2016
7. Bradbury, J. et al., "JAX: Composable Transformations of Python+NumPy Programs" (2018)
8. Jouppi, N.P. et al., "A Domain-Specific Supercomputer for Training Deep Neural Networks", CACM 2020
9. Jouppi, N.P. et al., "TPU v4: An Optically Reconfigurable Supercomputer for Machine Learning with Hardware Support for Embeddings", arXiv:2304.01433, 2023
10. Google Cloud TPU Documentation, https://cloud.google.com/tpu/docs
