# Course Alignment

This module maps concepts from `mini-ai-accelerator` to three primary academic sources.

## Core References

| Source | Type | Key Concepts | Module Mapping |
|--------|------|-------------|----------------|
| **Google TPU ISCA 2017** | Research Paper | Systolic array, Unified Buffer (UB), CISC ISA, PCIe attached, INT8 quantized inference, deterministic execution | systolic_array, tpu_isa, quantization |
| **MIT 6.5930** | Graduate Course | Systolic arrays as spatial architectures, dataflow taxonomy (weight/input/output/row stationary), Eyeriss energy model, tensor core evolution | systolic_array, dataflow, mma |
| **Stanford CS217** | Graduate Course | Quantization (PTQ, QAT, INT8/INT4), sparsity (unstructured, 2:4, block), hardware-aware training, LLM inference optimization | quantization, sparse_accel |

## Detailed Mapping

### Google TPU ISCA 2017

> Jouppi, N.P. et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017

| TPU Concept | Our Module | Implementation |
|-------------|------------|----------------|
| 256×256 systolic array (MXU) | `systolic_array.h` / `systolic_array.c` | `SystolicArray` with configurable N×N (max 16×16) |
| Unified Buffer (24 MB SRAM) | `tpu_isa.h` / `tpu_isa.c` | `TPUCore.unified_buffer` as contiguous byte array |
| Weight FIFO for weight loading | `systolic_array.h` | `weight_fifo[]` and `systolic_load_weights()` |
| Activation pipeline | `systolic_array.h` | `input_fifo[]` and `systolic_load_activation()` |
| CISC ISA (< 20 instructions) | `tpu_isa.h` / `tpu_isa.c` | `TPUInstruction` with 8 opcodes (MATMUL, LOAD, STORE, etc.) |
| PCIe host interface | `tpu_isa.h` | `tpu_load_program()` simulates host→TPU transfer |
| INT8 quantization for inference | `quantization.h` / `quantization.c` | `quant_quantize()` and `quant_dequantize()` with per-tensor params |
| Deterministic execution (no cache) | `tpu_isa.c` | `tpu_step()` executes instructions sequentially |
| Accumulator buffer (4K×32b) | `tpu_isa.h` | `TPUCore.accumulator_buffer[4096]` |
| Activation pipeline (ReLU, Sigmoid, Tanh) | `tpu_isa.h` | `OP_ACTIVATION` with `TPUActivation` enum |
| 92 TOPS peak (int8 at 700 MHz) | — | Discussed in demos/mini-tpu-sim/README.md |
| TOPS/W efficiency vs GPU/CPU | — | Discussed in demos/mini-tpu-sim/README.md |

### MIT 6.5930: Hardware Architecture for Deep Learning

> Fall 2023, Prof. Joel Emer, Vivienne Sze, et al.

| MIT 6.5930 Topic | Our Module | Implementation |
|------------------|------------|----------------|
| Systolic arrays — Kung & Leiserson (1978) | `systolic_array.h` / `demos/mini-systolic-array/` | `SystolicArray` with 2D mesh, cycle-level simulation |
| 1D vs 2D systolic arrays | `demos/mini-systolic-array/README.md` | Detailed comparison with diagrams |
| Weight stationary dataflow | `dataflow.h` / `dataflow.c` | `WEIGHT_STATIONARY` energy/timing model |
| Output stationary dataflow | `dataflow.h` / `dataflow.c` | `OUTPUT_STATIONARY` energy/timing model |
| Input stationary dataflow | `dataflow.h` / `dataflow.c` | `INPUT_STATIONARY` energy/timing model |
| Row stationary (Eyeriss) | `dataflow.h` / `dataflow.c` | `ROW_STATIONARY` energy/timing model |
| Energy modeling (MAC, SRAM, DRAM) | `dataflow.h` | Constants: `MAC_ENERGY_PJ`, `SRAM_READ_ENERGY_PJ`, `DRAM_READ_ENERGY_PJ` |
| Tiling strategies for large matrices | `mma.h` / `mma.c` | `mma_large_matmul()` with configurable `TILE_SIZE` |
| im2col for convolutions | `mma.h` / `mma.c` | `mma_conv2d_to_matmul()` with explicit im2col |
| Throughput analysis (utilization formulas) | `demos/mini-systolic-array/README.md` | Ops = 2 × N² × f derivation |
| Dataflow comparison methodology | `dataflow.c` | `dataflow_compare_all()` with table output |

### Stanford CS217: Hardware Accelerators for Machine Learning

> K. Olukotun, et al.

| CS217 Topic | Our Module | Implementation |
|-------------|------------|----------------|
| Post-training quantization (PTQ) | `quantization.h` / `quantization.c` | `quant_find_params()` + `quant_quantize()` |
| Quantization-aware training (QAT) concept | `demos/mini-quantized-inference/README.md` | Fake quantization, STE gradient explained |
| Per-tensor vs per-channel quantization | `quantization.h` / `quantization.c` | `quant_per_tensor_find_params()` and `quant_per_channel_find_params()` |
| Symmetric vs asymmetric quantization | `quantization.c` | Affine formula: q = round(r/S) + Z |
| Calibration methods (min-max, KL, percentile) | `demos/mini-quantized-inference/README.md` | Formulas and trade-offs discussed |
| Quantization error analysis (MSE, max error) | `quantization.h` / `quantization.c` | `quant_compute_mse()` and `quant_compute_max_error()` |
| Unstructured sparsity (magnitude pruning) | `sparse_accel.h` / `sparse_accel.c` | `sparse_csr_from_dense()` converts to CSR |
| Structured sparsity (2:4 NVIDIA) | `sparse_accel.h` / `sparse_accel.c` | `sparse_2of4_prune()` implements 2-of-4 selection |
| CSR/CSC sparse formats | `sparse_accel.h` | `SparseMatrix` with `row_ptr`, `col_idx`, `values` |
| Sparse-dense matmul (SpMM) | `sparse_accel.h` / `sparse_accel.c` | `sparse_spmm()` with O(nnz) complexity |
| Operations savings estimation | `sparse_accel.h` / `sparse_accel.c` | `sparse_compute_reduction()` |
| Compression ratio analysis | `sparse_accel.c` | `sparse_print_compression_ratio()` |
| SmoothQuant / outlier handling | `demos/mini-quantized-inference/README.md` | Scaling transformation explained |
| LLM.int8() mixed precision | `demos/mini-quantized-inference/README.md` | Outlier isolation explained |
| MMA / tensor cores | `mma.h` / `mma.c` | `mma_tile_matmul()` as tile-level MAC engine |
| Block/tiled matmul for large matrices | `mma.h` / `mma.c` | `mma_large_matmul()` with triple-nested tiling |

## Key Formulas Across All Sources

### Throughput (from TPU ISCA 2017)

```
TOPS = 2 × (systolic_array_width × systolic_array_height) × clock_frequency × 10⁻¹²
```

### Quantization Error (from CS217)

```
ε_max = S / 2                         (per element, uniform distribution)
MSE  = S² / 12                        (mean squared error)
SQNR = 10 × log₁₀(σ² / MSE)           (signal-to-quantization noise ratio)
```

### Sparsity Speedup (from CS217)

```
Theoretical speedup = Dense_MACs / Sparse_MACs = M × N / nnz
Effective speedup    = f(load_balance, memory_bandwidth, compression_overhead)
```

### Dataflow Energy (from MIT 6.5930)

```
E_total = N_MAC × E_MAC + N_SRAM × E_SRAM + N_DRAM × E_DRAM

E_MAC  = 1 pJ    (45nm technology)
E_SRAM = 5 pJ    (on-chip buffer read)
E_DRAM = 640 pJ  (off-chip DRAM read, ~128× more expensive than E_MAC)
```

### Systolic Latency (from MIT 6.5930, Kung & Leiserson)

```
Pipeline fill + compute + drain = K + M×N/array_size + (M+N-2)

For M×K × K×N multiply: latency = M + N + K - 2
                          throughput = 1 result per cycle (after fill)
```

## Cross-Cutting Themes

These three sources converge on several key principles that our module embodies:

1. **Minimize data movement**: All accelerator designs (TPU, Eyeriss, tensor cores) prioritize keeping data close to compute
2. **Specialize for the domain**: Matrix multiplication dominates deep learning; optimized hardware for this single operation yields massive efficiency gains
3. **Reduce precision where possible**: INT8 delivers 4× throughput vs FP32 with negligible accuracy loss for inference
4. **Exploit sparsity**: Real neural networks are 50-90% sparse; skipping zero operations is one of the highest-ROI optimizations
5. **Balance computation and memory**: The compute/memory ratio determines whether an accelerator is compute-bound or memory-bound

## Weekly Schedule

| Week | Topic | Source | Module Files |
|------|-------|--------|-------------|
| 1 | Systolic Arrays | ISCA 2017, MIT 6.5930 L1-L3 | systolic_array.h/c, demos/mini-systolic-array/ |
| 2 | TPU Architecture | ISCA 2017, MIT 6.5930 L4-L6 | tpu_isa.h/c, demos/mini-tpu-sim/ |
| 3 | Dataflow Taxonomy | MIT 6.5930 L7-L9, Eyeriss | dataflow.h/c, examples/dataflow_demo.c |
| 4 | MMA & Tiling | MIT 6.5930 L10-L12 | mma.h/c |
| 5 | Quantization (PTQ) | CS217 L11-L13 | quantization.h/c, examples/quantize_demo.c |
| 6 | Quantization (QAT, advanced) | CS217 L14-L16 | demos/mini-quantized-inference/ |
| 7 | Sparsity | CS217 L17-L19 | sparse_accel.h/c, examples/sparse_dot_demo.c |
| 8 | Project + Review | All sources | Full integration |
