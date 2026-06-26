# mini-sparse-accelerator — Sparse Computation Acceleration

## Overview

Sparsity is one of the most powerful optimization techniques for neural network inference and training. Modern neural networks exhibit significant sparsity — both natural (from ReLU activations producing zeros) and induced (from pruning). Exploiting this sparsity can reduce computation by 2-10× with minimal accuracy loss.

> Reference: Gale, T. et al., "The State of Sparsity in Deep Neural Networks", arXiv:1902.09574, 2019

### Why Sparsity?

Neural networks are overparameterized. A typical ResNet-50 (25.5M parameters) can be pruned by 80-90% without accuracy loss. The VGG-16 model (138M parameters) can be pruned by over 90%. Sparse computation exploits this redundancy:

| Benefit | Mechanism | Typical Improvement |
|---------|-----------|-------------------|
| Fewer MACs | Skip zero-weight multiplications | 2-4× (dense pruned) |
| Less memory | Store only non-zero values (CSR format) | 2-10× compression |
| Lower bandwidth | Fetch fewer weights from DRAM | 2-10× |
| Lower energy | Skip MACs and reads for zeros | 2-4× energy reduction |

## Sparse Matrix Formats

### Coordinate Format (COO)

Each non-zero is stored as a (row, col, value) triple:

```
Matrix A:
  [ 3  0  0  2 ]
  [ 0  5  0  0 ]
  [ 1  0  0  4 ]
  [ 0  0  6  0 ]

COO representation:
  row:    [0, 0, 1, 2, 2, 3]
  col:    [0, 3, 1, 0, 3, 2]
  values: [3, 2, 5, 1, 4, 6]
```

COO is intuitive but inefficient for computation — requires scanning all entries to find a specific row.

### Compressed Sparse Row (CSR)

CSR compresses the row indices, enabling efficient row-wise computation:

```
CSR representation:
  row_ptr:  [0, 2, 3, 5, 6]     // cumulative count of non-zeros per row
  col_idx:  [0, 3, 1, 0, 3, 2]  // column index of each non-zero
  values:   [3, 2, 5, 1, 4, 6]  // value of each non-zero

Interpretation:
  Row 0: non-zeros at columns 0, 3 with values 3, 2
  Row 1: non-zero at column 1 with value 5
  Row 2: non-zeros at columns 0, 3 with values 1, 4
  Row 3: non-zero at column 2 with value 6
```

Storage cost:
```
CSR bytes = nnz × (sizeof(value) + sizeof(col_idx)) + (rows+1) × sizeof(row_ptr)
          = N × (4 + 4) + (M+1) × 4 bytes (32-bit)

Dense bytes = M × N × sizeof(value)
             = M × N × 4 bytes

Break-even sparsity: 50% (when nnz = M×N/2)
For typical 90% sparsity: 5× storage reduction
```

### Compressed Sparse Column (CSC)

Dual of CSR — stores columns instead of rows. Better for operations accessing data by column.

### Block-Sparse Formats

For structured sparsity, the basic unit is a block (tile) rather than an element:

```
Block-sparse matrix (block size 2×2):
  [ B₀₀  ·   B₀₂ ]
  [  ·   B₁₁  ·  ]
  [ B₂₀  ·   B₂₂ ]

Only store and compute non-zero blocks, reducing index overhead.
```

## Sparsity Types

### Unstructured (Random) Sparsity

Any element can be zero. Achieved via magnitude-based pruning: remove smallest-magnitude weights.

```
Dense weight matrix (4×4):
  [ 0.8  -0.3   0.1   0.0 ]       [ 0.8  -0.3   0.1   0.0 ]
  [-0.2   0.9  -0.1   0.0 ]  →    [-0.2   0.9  -0.1   0.0 ]
  [ 0.3  -0.5   1.2  -0.0 ]       [ 0.3  -0.5   1.2   0.0 ]
  [-0.1   0.4  -0.7   0.0 ]       [-0.1   0.4  -0.7   0.0 ]

Sparsity: 3/16 = 18.75% (naturally sparse last column)
After magnitude pruning (threshold=0.05): removes 4 more values
New sparsity: ~44%
```

**Pros**: Higher achievable sparsity for same accuracy
**Cons**: Irregular — hard to accelerate in hardware, load imbalance

### Structured Sparsity (NVIDIA 2:4)

NVIDIA introduced 2:4 structured sparsity in Ampere (A100) GPUs: in every group of 4 consecutive values, exactly 2 must be zero.

```
Weight vector: [0.8, -0.3, 0.1, 0.02, 0.5, -0.6, 0.0, 0.01, ...]
                 └───────group 1───────┘ └───────group 2───────┘

Keep 2 largest |w| per group:
  Group 1: keep 0.8, -0.3 → [0.8, -0.3, 0, 0]
  Group 2: keep 0.5, -0.6 → [0.5, -0.6, 0, 0]

Result: [0.8, -0.3, 0, 0, 0.5, -0.6, 0, 0, ...]
Sparsity: exactly 50%
```

The 2:4 format uses metadata (2-bit indices per 4 values indicating which are non-zero):

```
Compressed storage: Values[8], Indices[4] (each 2-bit index selects 2 of 4)
Storage: 8×4 + 4×0.25 = 33 bytes vs 16×4 = 64 bytes (50% reduction)
```

**Key advantage**: Regular pattern enables efficient hardware — sparse tensor cores load compressed weight tiles and perform only non-zero MACs.

### Block Sparsity

Larger structured groups (16×16 blocks, etc.) for coarser granularity:

```
Block-sparse (4×4 blocks):
  [████  ····  ████ ]
  [····  ████  ···· ]    (█ = non-zero block, · = zero block)
  [████  ····  ████ ]
```

Better hardware efficiency but lower maximum sparsity for similar accuracy.

## Sparse Computation

### Sparse-Dense Matrix Multiplication (SpMM)

The fundamental operation: multiply a sparse weight matrix (CSR) by a dense activation vector.

```c
void sparse_spmm(SparseMatrix *A, float *dense_B, int K, float *result) {
    for (int i = 0; i < A->rows; i++) {
        result[i] = 0.0f;
        for (int j = A->row_ptr[i]; j < A->row_ptr[i + 1]; j++) {
            int col = A->col_idx[j];
            float val = A->values[j];
            result[i] += val * dense_B[col];  // Only non-zero MACs
        }
    }
}
```

Computational complexity: O(nnz) vs O(rows × cols) for dense.

### Sparse-Sparse Operations

When both operands are sparse (e.g., sparse activations from ReLU):

```
S × S multiplication: O(nnz_A × nnz_B / cols) worst case
```

Sparse activations (50-70% for ReLU networks) can double the effective sparsity, but sparse-sparse operations are harder to accelerate — the irregular pattern of both operands creates complex dataflow.

### 2:4 Sparse Tensor Core Operations

NVIDIA's 2:4 sparse tensor cores accelerate matrix multiplication with 2:4-sparse operands:

```
Warp-level matrix multiply: A (FP16) × B (FP16, 2:4 sparse) = C (FP32)

A: 16×16 FP16 tile
B: 16×16 FP16 tile (2:4 compressed → 16×8 effective weights)
C: 16×16 FP32 accumulator

Throughput: Same as dense FP16 tensor core (312 TFLOPS on A100)
           → Effective 2× speedup for 2:4 sparse weights
```

The key insight: the 2:4 pattern encodes exactly which 50% of weights to skip, making the hardware data path exactly half the width.

## Algorithms

### Magnitude-Based Pruning

The simplest and most common approach:

```
def magnitude_prune(W, sparsity_target):
    flat = abs(W).flatten()
    threshold = percentile(flat, sparsity_target * 100)
    W_pruned = W * (abs(W) >= threshold)
    return W_pruned
```

For structured (2:4) pruning:

```
def prune_2of4(W):
    for each group of 4 consecutive elements:
        sort by magnitude
        keep top 2, zero bottom 2
    return W
```

### Iterative Pruning (Lottery Ticket Hypothesis)

Instead of one-shot pruning, prune iteratively:

```
1. Train network to convergence
2. Prune p% of smallest weights
3. Fine-tune remaining weights (or rewind to initialization — Lottery Ticket)
4. Repeat until target sparsity
```

The Lottery Ticket Hypothesis (Frankle & Carbin, 2019) shows that randomly-initialized dense networks contain sparse subnetworks ("winning tickets") that can be trained in isolation to match the full network's accuracy.

### Gradient-Based Pruning

Use gradient information to decide which weights to prune:

```
Importance(w_i) = |w_i × ∇L(w_i)|    // SNIP
                = |∑ ∇L(w_i) × w_i|   // GraSP
                ≈ |w_i|               // magnitude (approximation)
```

Gradient-based methods are more expensive but can find better sparse structures.

## Sparse Accelerator Architecture

### Hardware Support for Sparsity

**Sparse Tensor Cores (NVIDIA A100)**:
- Accept 2:4 compressed weight matrices
- Decompress on-the-fly in the MAC pipeline
- Skip zero-weight multiplications
- 2× throughput vs dense tensor cores at same power

**TPUv4 Sparse Cores**:
- Sparse core (SC) alongside dense matrix unit (MXU)
- Embedding lookups — naturally sparse operations
- Compressed storage reduces memory bandwidth

**Cambricon Sparse Accelerator**:
- Dedicated sparse-dense and sparse-sparse engines
- Index-matching units for irregular sparsity

### Load Balancing for Irregular Sparsity

The main challenge with unstructured sparsity: different rows have different nnz, causing load imbalance.

Solutions:
1. **Row-based work distribution**: Each PE processes one row
2. **Adaptive sparse tiles**: Dynamically partition based on non-zero density
3. **Inner-parallel**: Multiple PEs process one row in parallel
4. **Outer-serial**: Process rows sequentially with work stealing

### Sparsity-Aware Dataflow

Traditional dataflows (weight stationary, output stationary) assume dense matrices. Sparse dataflows must handle:

```
Problem: In weight stationary, PE(i,j) has weight W[i,j] = 0
         → Activation passes through but no MAC
         → Poor utilization if many zeros clustered

Solution: Compress weight matrix before loading into array
         → Skip zero-weight columns entirely
         → Use indirection to map compressed to physical PEs
```

## Implementation Details

### CSR Creation from Dense Matrix

```c
void sparse_csr_from_dense(float *dense, int rows, int cols, SparseMatrix *sparse) {
    int nnz = 0;
    // Count non-zeros
    for (int i = 0; i < rows * cols; i++)
        if (fabsf(dense[i]) > 1e-7f) nnz++;

    sparse->row_ptr = realloc(sparse->row_ptr, (rows + 1) * sizeof(int));
    sparse->col_idx = realloc(sparse->col_idx, nnz * sizeof(int));
    sparse->values = realloc(sparse->values, nnz * sizeof(float));

    int idx = 0;
    sparse->row_ptr[0] = 0;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            float val = dense[i * cols + j];
            if (fabsf(val) > 1e-7f) {
                sparse->col_idx[idx] = j;
                sparse->values[idx] = val;
                idx++;
            }
        }
        sparse->row_ptr[i + 1] = idx;
    }
}
```

### 2:4 Pruning Implementation

```c
void sparse_2of4_prune(float *weights, int rows, int cols) {
    int total = rows * cols;
    for (int i = 0; i < total; i += 4) {
        // Select 2 largest magnitudes in group of 4
        float group[4];
        for (int k = 0; k < 4 && i + k < total; k++)
            group[k] = fabsf(weights[i + k]);

        // Simple bubble sort to find top 2
        int keep[4] = {0};
        int top2[2] = {-1, -1};
        for (int k = 0; k < 4; k++) {
            if (top2[0] == -1 || group[k] >= group[top2[0]]) {
                top2[1] = top2[0];
                top2[0] = k;
            } else if (top2[1] == -1 || group[k] >= group[top2[1]]) {
                top2[1] = k;
            }
        }
        keep[top2[0]] = keep[top2[1]] = 1;
        for (int k = 0; k < 4 && i + k < total; k++)
            if (!keep[k]) weights[i + k] = 0.0f;
    }
}
```

## Expected Output

Running sparse_dot_demo:

```
Original Dense Weight Matrix (8x8):
  ... (dense values) ...

=== Applying 2:4 Structured Sparsity ===
W (2:4 pruned) (8x8):
  ... (50% zeros, structured pattern) ...

Compression Analysis:
  Original elements: 64
  Non-zero elements: 32
  Sparsity: 50.00%
  Dense storage: 256 bytes
  CSR storage: 192 bytes
  Compression ratio: 1.33x

=== Sparse-Dense MatMul ===
Sparse output: ... (matches dense output within error)

=== Operations Comparison ===
  Dense MAC operations: 64
  Sparse MAC operations: 32
  Theoretical speedup: 2.00x
  Structured sparsity (2:4): 50.0% of weights retained
```

For larger matrices with higher sparsity (e.g., 90% unstructured), the speedup is more dramatic:

```
Dense MAC operations: 1,048,576 (1024×1024)
Sparse MAC operations: 104,858 (90% sparsity)
Theoretical speedup: 10.00x
CSR compression ratio: 5.00x
```

## Real-World Results

| Model | Method | Sparsity | Accuracy | Throughput Gain |
|-------|--------|----------|----------|-----------------|
| ResNet-50 | Magnitude prune + fine-tune | 80% | -0.5% top-1 | 3× (GPU) |
| ResNet-50 | 2:4 structured | 50% | -0.1% top-1 | 2× (A100 sparse TC) |
| BERT-Large | Movement pruning | 95% | -0.5% F1 | 5× (theoretical) |
| GPT-3 | SparseGPT | 50% | minimal | 2× (LLM inference) |
| VGG-16 | Magnitude prune | 92% | -0.7% top-1 | 10× (theoretical) |

## Build and Run

```bash
cd mini-ai-accelerator
make sparse_dot_demo
./bin/sparse_dot_demo
```

## References

1. Gale, T. et al., "The State of Sparsity in Deep Neural Networks", arXiv:1902.09574, 2019
2. Frankle, J. and Carbin, M., "The Lottery Ticket Hypothesis: Finding Sparse, Trainable Neural Networks", ICLR 2019
3. Mishra, A. et al., "Accelerating Sparse Deep Neural Networks", arXiv:2104.08378, 2021
4. NVIDIA, "NVIDIA A100 Tensor Core GPU Architecture", Whitepaper, 2020
5. Frantar, E. and Alistarh, D., "SparseGPT: Massive Language Models Can Be Accurately Pruned in One-Shot", ICML 2023
6. Pool, J. and Yu, C., "Channel Permutations for N:M Sparsity", NeurIPS 2021
7. Zhou, A. et al., "Learning N:M Fine-Grained Structured Sparse Neural Networks From Scratch", ICLR 2021
8. Han, S. et al., "Learning both Weights and Connections for Efficient Neural Networks", NeurIPS 2015
9. Stanford CS217: Hardware Accelerators for Machine Learning, Lecture on Sparse Acceleration

## Summary

| Sparsity Type | Granularity | Compressibility | Hardware Friendly | Max Sparsity |
|--------------|-------------|-----------------|-------------------|--------------|
| Unstructured | Element | CSR/CSC format | Low (irregular) | 90-99% |
| 2:4 Structured | Group of 4 | Compressed tile | High (A100 TC) | 50% |
| 4:8 Structured | Group of 8 | Compressed tile | Medium | 50% |
| Block (16×16) | Block | Block coordinate | High | 50-75% |
| Channel | Whole channel | Remove filters | Very high | 30-50% |

The choice of sparsity type depends on the deployment target: GPUs excel with 2:4 structured sparsity (tensor cores), while custom accelerators can handle unstructured sparsity with specialized dataflow.
