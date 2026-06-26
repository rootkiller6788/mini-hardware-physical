# mini-systolic-array — Systolic Array Deep Dive

## Overview

Systolic arrays are the computational backbone of modern AI accelerators. Coined by H.T. Kung and Charles Leiserson in their seminal 1978 paper "Systolic Arrays (for VLSI)", the concept describes a grid of processing elements (PEs) where data flows rhythmically through the array in lockstep, much like blood pulsing through the circulatory system — hence "systolic".

> Reference: Kung, H.T., and Leiserson, C.E., "Systolic Arrays (for VLSI)," in *Sparse Matrix Proceedings*, 1978.

### Why Systolic Arrays?

Traditional processor architectures suffer from the von Neumann bottleneck: data must shuttle between memory and the CPU, consuming significant energy and limiting throughput. A systolic array addresses this by:

1. **Minimizing data movement**: Each PE receives data only from its immediate neighbors
2. **Maximizing reuse**: Weights remain stationary, activations flow through
3. **Regular communication**: All data flows in a predictable, synchronized pattern
4. **Scalability**: The array can be expanded by adding more PEs

## Theory

### 1D Systolic Array

A one-dimensional systolic array consists of a linear chain of PEs. Data flows from left to right, with each PE performing a multiply-accumulate (MAC) operation.

```
Input stream:  a₀, a₁, a₂, a₃ ──→ [PE₀] ──→ [PE₁] ──→ [PE₂] ──→ [PE₃]
Weight stream: w₀ ──→ [PE₀]    w₁ ──→ [PE₁]    w₂ ──→ [PE₂]    w₃ ──→ [PE₃]
                                  ↓           ↓           ↓           ↓
                              y₀ = ∑a·w₀   y₁ = ∑a·w₁   y₂ = ∑a·w₂   y₃ = ∑a·w₃
```

The 1D systolic array computes a matrix-vector product with O(N) PEs in O(M+N) cycles for an M×N matrix.

### 2D Systolic Array

A two-dimensional systolic array extends the 1D concept to a grid. This enables direct matrix-matrix multiplication.

```
         a₀       a₁       a₂       a₃
          ↓        ↓        ↓        ↓
w₀ → [PE₀₀] → [PE₀₁] → [PE₀₂] → [PE₀₃]     (weight flows top to bottom)
              ↓        ↓        ↓        ↓
w₁ → [PE₁₀] → [PE₁₁] → [PE₁₂] → [PE₁₃]     (activation flows left to right)
              ↓        ↓        ↓        ↓
w₂ → [PE₂₀] → [PE₂₁] → [PE₂₂] → [PE₂₃]
              ↓        ↓        ↓        ↓
w₃ → [PE₃₀] → [PE₃₁] → [PE₃₂] → [PE₃₃]
              ↓        ↓        ↓        ↓
            y₀       y₁       y₂       y₃     (partial sums accumulate downward)
```

Each PE stores:
- A **weight** (pre-loaded before computation)
- An **activation** (received from the left neighbor)
- A **partial sum** (passed downward or accumulated locally)

### Dataflow Types

| Dataflow | Weight Movement | Activation Movement | Accumulation | Best For |
|----------|----------------|---------------------|--------------|----------|
| Weight Stationary | Stationary | Broadcast left→right | Local | Inference (large batches) |
| Output Stationary | Broadcast | Broadcast | Stationary | Training (gradient accumulation) |
| Input Stationary | Broadcast | Stationary | Accumulate in array | Streaming data |
| Row Stationary | Pass vertically | Pass horizontally | Pass diagonally | Energy-efficient conv layers |

### Throughput Analysis

For an N×N systolic array running at frequency f:

```
MAC operations per cycle = 2 × N²   (each PE does 1 multiply + 1 add per cycle)
Peak TOPS = 2 × N² × f × 10⁻¹²
```

**Example: Google TPUv1**
- Array size: 256×256
- Clock frequency: 700 MHz
- Peak throughput: 2 × 256² × 700×10⁶ × 10⁻¹² = 92 TOPS

For convolution layers (the dominant operation in CNNs):

```
Throughput utilization (%) = (2×R×S×C) / (256 × utilization_factor)
```

where R and S are filter dimensions, C is input channels, and utilization drops when these are smaller than the array dimensions.

### Latency Model

For a matrix multiplication C = A × B where A is M×K and B is K×N:

```
Pipelined latency = M + N + K - 2  cycles
Steady-state throughput = 1 output per cycle after pipeline fill
```

The pipeline phases:
1. **Fill phase** (K cycles): Weights and activations are loaded into the array
2. **Drain phase** (M+N-2 cycles): Partial sums propagate to the output

### Timing Diagram

```
Cycle:    0   1   2   3   4   5   6   7   8   9  10
          │   │   │   │   │   │   │   │   │   │   │
PE₀₀:   a₀w₀ a₁w₀ a₂w₀ a₃w₀  0  ...
         │//
PE₀₁:   a₀w₁ a₁w₁ a₂w₁ a₃w₁  ...
PE₁₀:     a₀w₀ a₁w₀ a₂w₀ a₃w₀ ...
PE₁₁:       a₀w₁ a₁w₁ a₂w₁ a₃w₁ ...
```

## Implementation

### Core Data Structures

```c
#define MAX_SYSTOLIC_SIZE 16

typedef struct {
    float accumulator;    // Accumulated dot product
    float weight;         // Pre-loaded weight value
    float activation;     // Current input activation
    float partial_sum;    // Partial sum passed to next cell
} SystolicCell;

typedef struct {
    int rows, cols;
    SystolicCell cells[MAX_SYSTOLIC_SIZE][MAX_SYSTOLIC_SIZE];
    float input_fifo[MAX_SYSTOLIC_SIZE];   // Activation input queue
    float weight_fifo[MAX_SYSTOLIC_SIZE];  // Weight input queue
    float output_buffer[MAX_SYSTOLIC_SIZE];
    int input_head, weight_head;
} SystolicArray;
```

### Creating the Array

```c
SystolicArray *systolic_array_create(int rows, int cols) {
    SystolicArray *sa = malloc(sizeof(SystolicArray));
    sa->rows = rows; sa->cols = cols;
    sa->input_head = 0; sa->weight_head = 0;
    // Initialize all cells to zero
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            memset(&sa->cells[i][j], 0, sizeof(SystolicCell));
    return sa;
}
```

### Loading Weights

Weights are pre-loaded into the PE array. In weight-stationary dataflow, weights remain fixed throughout the entire computation of one layer.

```c
void systolic_load_weights(SystolicArray *sa, float *weights, int rows, int cols) {
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            sa->cells[i][j].weight = weights[i * cols + j];
}
```

### Cycle-by-Cycle Execution

Each cycle performs three operations simultaneously:

```c
void systolic_cycle(SystolicArray *sa) {
    // 1. Compute: each cell does accum += activation * weight
    for (int i = 0; i < sa->rows; i++)
        for (int j = 0; j < sa->cols; j++)
            sa->cells[i][j].accumulator +=
                sa->cells[i][j].activation * sa->cells[i][j].weight;

    // 2. Shift activations right (left-to-right flow)
    for (int i = 0; i < sa->rows; i++)
        for (int j = sa->cols - 1; j >= 1; j--)
            sa->cells[i][j].activation = sa->cells[i][j-1].activation;

    // 3. Shift weights down (top-to-bottom flow)
    for (int j = 0; j < sa->cols; j++)
        for (int i = sa->rows - 1; i >= 1; i--)
            sa->cells[i][j].weight = sa->cells[i-1][j].weight;
}
```

### Full Matrix Multiplication

```c
void systolic_run(SystolicArray *sa, float *A, float *B,
                  int M, int N, int K, float *result) {
    systolic_load_weights(sa, B, K, N);
    int total_cycles = M + N + K - 2;
    for (int c = 0; c < total_cycles; c++) {
        // Feed in next input row
        if (c < M)
            for (int j = 0; j < K; j++)
                sa->input_fifo[j] = A[c * K + j];
        systolic_cycle(sa);
    }
    // Read results from last column
    for (int i = 0; i < N; i++)
        result[i] = sa->cells[i][N-1].accumulator;
}
```

### Systolic Array for Convolution (im2col)

Convolution layers are transformed to matrix multiplication via im2col:

```
Input (H×W×C) → im2col → Matrix (OH·OW × C·R·S)
Kernel (K×C×R×S) → Reshape → Matrix (C·R·S × K)
Output = im2col × Kernel → Reshape → Feature map (K×OH×OW)
```

The systolic array then performs the GEMM operation on the transformed matrices.

## Expected Output

Running the systolic_mm_demo produces output showing each cycle's accumulator state:

```
Systolic Array 4x4:
  Row 0: [acc=0.500 w=0.500 a=1.000] [acc=0.000 w=1.000 a=0.000] ...
  Row 1: [acc=2.500 w=2.500 a=5.000] [acc=0.000 w=3.000 a=0.000] ...
  ...

=== Cycle 0 ===
Systolic Array 4x4:
  Row 0: [acc=0.500 w=0.500 a=1.000] [acc=0.000 w=1.000 a=0.000] ...
  ...

Verification: All results match!
```

## Practical Considerations

### Utilization Efficiency

Real-world utilization rarely reaches 100%. For TPUv1 with ResNet-50:
- Average utilization: ~50-60% for convolutions
- Bottleneck: small channel depth in early/late layers

### Precision

TPUv1 uses bfloat16 multiplication with FP32 accumulation. Our C implementation uses FP32 throughout for simplicity but applies the same architectural principles.

### Scalability

The 2D mesh topology scales with O(N²) PEs per chip. TPUv4 uses a 128×128 array per core, with 8 cores per chip, interconnected via a 2D torus.

## Build and Run

```bash
cd mini-ai-accelerator
make systolic_mm_demo
./bin/systolic_mm_demo
```

## References

1. Kung, H.T. and Leiserson, C.E., "Systolic Arrays (for VLSI)", CMU-CS-79-103, 1978
2. Jouppi, N.P. et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017
3. Sze, V. et al., "Efficient Processing of Deep Neural Networks: A Tutorial and Survey", Proceedings of the IEEE, 2017
4. Chen, Y.H. et al., "Eyeriss: A Spatial Architecture for Energy-Efficient Dataflow for Convolutional Neural Networks", ISCA 2016
5. MIT 6.5930 Hardware Architecture for Deep Learning, Fall 2023
6. Stanford CS217: Hardware Accelerators for Machine Learning

### TPUv1-v4 Evolution

| Feature | TPUv1 | TPUv2 | TPUv3 | TPUv4 |
|---------|-------|-------|-------|-------|
| Year | 2015 | 2017 | 2018 | 2021 |
| Array Size | 256×256 | 128×128 ×2 | 128×128 ×2 | 128×128 ×2 |
| Clock | 700 MHz | 700 MHz | 940 MHz | 1.05 GHz |
| Precision | int8/bfloat16 | bfloat16 | bfloat16 | bfloat16/int8 |
| Memory | 8 GB DDR3 | 16 GB HBM | 32 GB HBM | 32 GB HBM |
| Interconnect | PCIe | Custom | Custom | Optical |
| Perf/Chip | 92 TOPS | 180 TFLOPS | 420 TFLOPS | 275 TFLOPS |

### Throughput Formula Derivation

The peak operations per second of a systolic array:

```
Ops_per_cycle = 2 × Array_Width × Array_Height
              = 2 × N²                    (for square N×N array)

Throughput = Ops_per_cycle × Frequency
           = 2 × N² × f
```

For N=256, f=700 MHz:
```
Throughput = 2 × 65536 × 700×10⁶ = 92×10⁹ ops/s = 92 GOPS (int8)
          = 46 GFLOPS (half precision, one MAC = 2 ops for FLOPS, but int8 MAC ≠ FLOP)
```

For bfloat16, each MAC counts as 2 operations (multiply + add):
```
TFLOPS = 2 × 2 × N² × f × 10⁻¹² = 4 × N² × f × 10⁻¹²
```
