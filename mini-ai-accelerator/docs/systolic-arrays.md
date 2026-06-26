# Systolic Arrays — Fundamentals and Design

## Historical Context

Systolic arrays were introduced by H.T. Kung and Charles Leiserson at Carnegie Mellon University in their 1978 paper "Systolic Arrays (for VLSI)". The term "systolic" draws an analogy to the human circulatory system — data pulses rhythmically through the processing elements (PEs) in lockstep, like blood through the heart.

> "A systolic system is a network of processors that rhythmically compute and pass data through the system... In a systolic computer system, the function of a processor is analogous to that of the heart." — Kung & Leiserson, 1978

The original motivation was the VLSI (Very Large Scale Integration) design challenge: how to organize many simple processors on a chip with minimal wiring complexity. Systolic arrays achieve this through:
1. Regular, local interconnects (nearest-neighbor only)
2. Modular expandability (add more PEs to scale)
3. Simple control (all PEs execute the same operation each cycle)

## 1D Systolic Arrays

### Architecture

A 1D systolic array is a linear chain of identical PEs. Each PE stores a weight and receives a stream of activations from its left neighbor.

```
Clock cycle ──────────────────────────────────────────────────────→

Input a₃, a₂, a₁, a₀ ──→ [PE₀] ──→ [PE₁] ──→ [PE₂] ──→ [PE₃]
                           w₀        w₁        w₂        w₃
                           │         │         │         │
                           y₀        y₁        y₂        y₃
```

### Operation

At each cycle, every PE:
1. Receives an activation from the left (or input)
2. Multiplies it by the stored weight
3. Adds the product to the accumulator
4. Passes the activation to the right

After all activations have passed through, each PE's accumulator holds the dot product:

```
yᵢ = Σⱼ aⱼ × wᵢⱼ

where aⱼ is the j-th input activation
      wᵢⱼ is the weight at PEᵢ for input j
```

### Timing for Convolution / Filtering

For a weight vector of length K and input stream of length N:

```
Latency = N + K - 1 cycles
Throughput = 1 output per cycle (after pipeline fill)
```

The first K-1 cycles "fill" the pipeline; the last K-1 cycles "drain" it.

### Limitations

1D systolic arrays compute vector-vector or matrix-vector products. For matrix-matrix multiplication (the dominant operation in deep learning), they require iterating over one dimension, losing the parallelism of a full 2D array.

## 2D Systolic Arrays

### Architecture

A 2D systolic array extends the 1D concept to a grid. Each PE connects to its left and top neighbors:

```
         a₀      a₁      a₂      a₃         ← activations flow →
          ↓       ↓       ↓       ↓
w₀ → [PE₀₀] → [PE₀₁] → [PE₀₂] → [PE₀₃]
       ↓        ↓        ↓        ↓
w₁ → [PE₁₀] → [PE₁₁] → [PE₁₂] → [PE₁₃]
       ↓        ↓        ↓        ↓
w₂ → [PE₂₀] → [PE₂₁] → [PE₂₂] → [PE₂₃]
       ↓        ↓        ↓        ↓
w₃ → [PE₃₀] → [PE₃₁] → [PE₃₂] → [PE₃₃]
       ↓        ↓        ↓        ↓
       y₀       y₁       y₂       y₃       ← partial sums accumulate ↓
```

### Data Flow Directions

| Flow | Direction | Contents |
|------|-----------|----------|
| Activation flow | Left → Right | Input feature values |
| Weight flow | Top → Bottom | Weight matrix values |
| Partial sum flow | Diagonal | Accumulating dot products |

Different dataflows organize these flows differently (see dataflow module).

### Operation per Cycle

```
For each PE(i,j) in parallel:
    c_{ij}(t+1) = c_{ij}(t) + a(i,j,t) × w(i,j,t)
    pass a(i,j,t) to PE(i, j+1)    // activation moves right
    pass w(i,j,t) to PE(i+1, j)    // weight moves down
```

### Matrix Multiplication Mapping

For C = A × B where:
- A: M×K matrix (activations)
- B: K×N matrix (weights)
- C: M×N matrix (output)

The 2D array computes partial sums as follows:

```
Each PE(i,j) at the top row receives:
  - Weight B[k,j] from above at cycle k
  - Activation A[i,k] from the left

After K cycles, PE(i,j) accumulates:
  C[i,j] = Σ_{k=1}^{K} A[i,k] × B[k,j]
```

### Complete Pipeline (M=4, K=4, N=4 with 4×4 array)

```
Cycle 0:  PE(0,0) receives a₀₀ × w₀₀
Cycle 1:  PE(0,0) receives a₀₁ × w₁₀, PE(0,1) receives a₀₀ × w₀₁
Cycle 2:  PE(0,0) receives a₀₂ × w₂₀, PE(0,1) receives a₀₁ × w₁₁ ...
Cycle 3:  ...
Cycle 6:  First result emerges from PE(3,3): C[3,3]
Cycle 7-9: Drain pipeline
Cycle 10: Last result C[0,0] at PE(0,3)
```

Total cycles: M + N + K - 2 = 4 + 4 + 4 - 2 = 10

### Throughput

```
Peak OPS = 2 × N² × f

where:
  N = systolic array dimension (assuming square)
  f = clock frequency
  factor 2 = 1 multiply + 1 add per MAC per cycle
```

| Platform | Array Size | Clock | INT8 TOPS | BFloat16 TFLOPS |
|----------|-----------|-------|-----------|-----------------|
| TPUv1 | 256×256 | 700 MHz | 92 | — |
| TPUv2 | 128×128 ×2 | 700 MHz | — | 46 |
| TPUv3 | 128×128 ×2 | 940 MHz | — | 123 |
| TPUv4 | 128×128 ×2 | 1.05 GHz | 275 | 275 |
| Eyeriss v1 | 12×14 | 200 MHz | 0.067 | — |
| Eyeriss v2 | 16×16 ×8 | 250 MHz | 1.02 | — |

## Systolic Arrays for Deep Learning

### Convolution via im2col

Convolution is the most common operation in CNNs. Systolic arrays handle convolutions by transforming them into matrix multiplication via the `im2col` operation:

```
Input:  4D tensor (N, C, H, W)
Filter: 4D tensor (K, C, R, S)
Output: 4D tensor (N, K, OH, OW)

Step 1: im2col
  For each output position (oh, ow), extract a patch of size C×R×S
  → Matrix of size (OH·OW) × (C·R·S)

Step 2: Reshape filter
  → Matrix of size (C·R·S) × K

Step 3: Matrix multiply
  (OH·OW) × (C·R·S) · (C·R·S) × K → (OH·OW) × K

Step 4: Reshape output
  → Feature map of size K × OH × OW
```

### im2col Overhead

im2col creates redundant copies of input data (each element appears in multiple patches). For a 3×3 convolution with stride 1, each input element appears 9 times in the im2col matrix — a 9× memory overhead.

**Alternative**: Direct convolution on systolic arrays without im2col, using more complex dataflow.

### Batch Processing

The TPU is optimized for batch inference. For batch size B:

```
Inputs: B × M × K
Weights: K × N (shared across batch)
Output: B × M × N

Total cycles = B × (M + K) + N - 2  →  O(B×M×K/N² + N)
```

Weight loading is amortized across the batch, dramatically improving utilization.

### Utilization Analysis

For an N×N array processing an M×K × K×N matrix multiply:

```
PE utilization = (M × K) / (N × ceil(M/N) × ceil(K/N)) × 100%

Examples:
  M=256, K=256, N=256: 256²/(256×256) = 100% (perfect fit)
  M=56,  K=64,  N=256: 3584/(256×256) = 5.5% (poor fit)
  M=512, K=512, N=256: 512²/(256×512×2×2) = partially utilized
```

The "dark silicon" problem: large arrays are underutilized for small matrix dimensions. Solutions include:
1. **Multi-tenancy**: Run multiple models simultaneously on different array regions
2. **Mixed-dimension support**: Array configurable into multiple smaller sub-arrays
3. **Flexible tiling**: Dynamic partitioning based on workload dimensions

## Implementation Considerations

### Precision

TPUv1 systolic cells perform INT8 multiplication with 32-bit accumulation:

```
PE internal state:
  weight:    INT8  (8 bits, pre-loaded)
  activation: INT8  (8 bits, flowing through)
  product:   INT16 (16 bits, 8×8)
  accumulator: INT32 (32 bits, Σ of INT16 products)
```

Our C implementation uses FP32 for simplicity but captures the same architectural patterns.

### Clock Distribution

A systolic array requires a global clock signal distributed to all PEs. For large arrays (256×256), clock skew becomes a challenge. Modern H-trees and clock meshes distribute the clock with skew < 100 ps across the array.

### Defect Tolerance

With 65,536 PEs in a single array (TPUv1), manufacturing defects are statistically guaranteed. Large arrays include redundant rows/columns that can be activated to bypass defective PEs.

## Scalability Beyond a Single Chip

### Multi-Chip Systolic Arrays

The systolic concept extends to inter-chip communication:

```
Chip 0 (128×128)  ←→  Chip 1 (128×128)
       ↕                    ↕
Chip 2 (128×128)  ←→  Chip 3 (128×128)
```

TPUv4 uses a 2D torus interconnect between 8 cores on a chip, and 4096 chips are connected via optical circuit switches (OCS) in a TPUv4 pod.

### Wafer-Scale Systolic Arrays

Cerebras CS-2 takes the systolic array concept to the extreme: an entire wafer as a single systolic array.

| Metric | Cerebras CS-2 | Google TPUv4 |
|--------|--------------|--------------|
| PEs | 850,000 | 8 × 128×128 = 131,072 |
| Die area | 46,225 mm² (wafer) | ~400 mm² ×2 |
| On-chip memory | 40 GB SRAM | 32 GB HBM |
| Process | TSMC 7nm | TSMC 7nm |
| Peak (fp16) | ? | 275 TFLOPS |

## Summary

| Property | 1D Systolic | 2D Systolic |
|----------|-------------|-------------|
| Topology | Linear chain | Mesh/grid |
| Connections/PE | 2 (left, right) | 4 (left, right, top, bottom) |
| Operations | Vector ops | Matrix-matrix ops |
| Latency (M×K · K×N) | O(M × K × N) | O(M + N + K) |
| Throughput | Low | High (pipelined) |
| Used in | Early DSPs | All modern AI accelerators |

The 2D systolic array remains the dominant compute fabric for AI accelerators because:
1. Matrix multiplication maps perfectly to the 2D mesh topology
2. Regular dataflow minimizes control overhead
3. Nearest-neighbor communication scales to large arrays without wiring explosion
4. Pipelined execution achieves high throughput even with modest clock speeds

## References

1. Kung, H.T. and Leiserson, C.E., "Systolic Arrays (for VLSI)", CMU-CS-79-103, 1978
2. Kung, H.T., "Why Systolic Architectures?", IEEE Computer, Vol. 15, No. 1, pp. 37-46, 1982
3. Jouppi, N.P. et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017
4. Sze, V. et al., "Efficient Processing of Deep Neural Networks: A Tutorial and Survey", Proc. IEEE, 2017
5. Chen, Y.H. et al., "Eyeriss v1: An Energy-Efficient Reconfigurable Accelerator for DCNNs", JSSC 2017
6. Chen, Y.H. et al., "Eyeriss v2: A Flexible Accelerator for Emerging DNNs on Mobile Devices", JETCAS 2019
7. Rocki, K. et al., "The Cerebras CS-2 Wafer Scale Engine", Hot Chips 2021
