# mini-tpu-sim — TPU Architecture Simulator

## Overview

This module provides a cycle-level architectural simulator for a Google TPU-like accelerator. The simulation models the key hardware components of a TPU: the unified buffer (UB), systolic array, vector unit, and activation pipeline. It implements a simplified TPU instruction set architecture (ISA) with eight core operations.

> Reference: Jouppi, N.P. et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017

### What is a TPU?

The Tensor Processing Unit (TPU) is Google's custom ASIC for neural network inference. Unlike CPUs and GPUs, which are general-purpose, the TPU is a domain-specific architecture designed around matrix multiplication — the dominant operation in deep learning. Key innovations include:

1. **Systolic array**: A 256×256 grid of MAC units for dense matrix multiplication
2. **Unified Buffer (UB)**: 24 MB of on-chip SRAM acting as scratchpad memory
3. **Deterministic execution**: No caches, no branch prediction — CISC ISA with exposed hardware
4. **PCIe attached**: The TPU plugs into existing servers as a coprocessor

## Architecture

### Hardware Block Diagram

```
┌──────────────────────────────────────────────────────────────┐
│                        HOST CPU                               │
│  ┌────────────────────────────────────────────────────┐       │
│  │              TPU Driver / Runtime                   │       │
│  └──────────────────────┬─────────────────────────────┘       │
│                         │ PCIe Gen3 ×16                       │
│  ┌──────────────────────┴─────────────────────────────┐       │
│  │                  TPU Chip                            │       │
│  │  ┌──────────────────────────────────────────────┐   │       │
│  │  │          Host Interface (DMA)                  │   │       │
│  │  └──────────────┬───────────────────────────────┘   │       │
│  │                 │                                     │       │
│  │  ┌──────────────┴───────────────────────────────┐   │       │
│  │  │         Unified Buffer (UB) — 24 MB SRAM       │   │       │
│  │  │          (Weights + Activations)               │   │       │
│  │  └──────┬──────────────┬──────────────┬──────────┘   │       │
│  │         │              │              │               │       │
│  │  ┌──────┴──────┐ ┌─────┴──────┐ ┌────┴────────────┐ │       │
│  │  │  Systolic   │ │  Vector    │ │  Activation     │ │       │
│  │  │  Array      │ │  Unit      │ │  Unit           │ │       │
│  │  │  256×256    │ │ (ALU pool) │ │ (ReLU, Sigmoid, │ │       │
│  │  │  INT8 MACs  │ │            │ │  Tanh, Softmax) │ │       │
│  │  └──────┬──────┘ └─────┬──────┘ └────┬────────────┘ │       │
│  │         │              │              │               │       │
│  │  ┌──────┴──────────────┴──────────────┴──────────┐   │       │
│  │  │    Accumulator Buffer (4K × 32-bit float)      │   │       │
│  │  └───────────────────────────────────────────────┘   │       │
│  └──────────────────────────────────────────────────────┘       │
└──────────────────────────────────────────────────────────────┘
```

### Component Details

#### 1. Host Interface

The TPU communicates with the host CPU over PCIe Gen3 ×16 (≈12.8 GB/s bidirectional). The host sends instructions and data via DMA to the TPU's unified buffer. After computation, results are read back over PCIe.

**Key property**: The TPU receives the entire model's weights once, then processes many inferences (or training batches) without host intervention.

#### 2. Unified Buffer (UB)

The UB is a 24 MB software-managed scratchpad. It stores:
- Model weights (pre-loaded before inference)
- Input activations (from host or previous layer)
- Intermediate activations (between layers)
- The instruction queue

Unlike CPU caches, the UB is explicitly managed by the compiler/runtime. This deterministic behavior enables precise latency prediction — critical for serving SLAs.

```
UB Memory Layout:
┌──────────────────┐ 0x000000
│  Instructions    │ (variable)
├──────────────────┤
│  Weight Matrix   │ (variable, aligned)
├──────────────────┤
│  Activation Data │ (variable, aligned)
├──────────────────┤
│  Workspace       │ (remainder)
└──────────────────┘ 0x1800000 (24 MB)
```

#### 3. Systolic Array

The 256×256 systolic array performs the core matrix multiply:
- **Weight stationary dataflow**: Weights are pre-loaded into PEs
- **INT8 multiply with 32-bit accumulate**: Reduces energy vs FP32
- **Peak throughput**: 92 TOPS (int8) at 700 MHz

Formula:
```
Throughput = 2 × 65536 PEs × 700 MHz = 91.7 TOPS (int8)
```

#### 4. Vector Unit

Handles element-wise operations (add, multiply, ReLU) that follow the systolic array. Uses a pool of ALUs that operate on 256-element vectors from the accumulator buffer.

#### 5. Activation Unit

Applies non-linear functions:
- **ReLU(x)** = max(0, x) — simple threshold, most common
- **Sigmoid(x)** = 1/(1+e⁻ˣ) — implemented via lookup table in hardware
- **Tanh(x)** = (e²ˣ−1)/(e²ˣ+1) — also via LUT

#### 6. Accumulator Buffer

4K × 32-bit registers holding the systolic array output before optional activation. TPUv1 uses 32-bit float; later versions add bfloat16 support.

## TPU Instruction Set Architecture (ISA)

The TPU uses a CISC ISA with fewer than 20 instructions. Each instruction is a single "command" that the TPU executes atomically. This contrasts with GPU SIMT models (thousands of threads).

### Instruction Format

```
┌─────────┬────────────┬──────────┬──────────┬───────────────┐
│ Opcode  │ Src Addr   │ Dst Addr │ Size     │ Optional Args │
│ (8 bits)│ (32 bits)   │ (32 bits)│ (32 bits)│ (32×3 bits)   │
└─────────┴────────────┴──────────┴──────────┴───────────────┘
```

### Core Instructions

| Instruction | Opcode | Description | Operands |
|------------|--------|-------------|----------|
| MATMUL | 0 | Matrix Multiply | src_addr, dst_addr, M, N, K |
| VECTOR_ADD | 1 | Element-wise vector add | src_addr, dst_addr, size |
| VECTOR_MUL | 2 | Element-wise vector multiply | src_addr, dst_addr, size |
| ACTIVATION | 3 | Apply activation function | activation_type, size |
| LOAD_WEIGHT | 4 | Load weights into UB | src_addr, size |
| LOAD_ACT | 5 | Load activations into UB | src_addr, size |
| STORE | 6 | Store accumulator to UB | dst_addr, size |
| SYNC | 7 | Synchronization barrier | — |

### Execution Model

The TPU executes instructions sequentially from a queue. There is no instruction-level parallelism within a TPU core — the systolic array IS the parallelism.

```
while (true) {
    inst = fetch_next_instruction();
    switch (inst.opcode) {
        case MATMUL:    systolic_array.compute(inst); break;
        case VECTOR_ADD: vector_unit.add(inst);        break;
        case ACTIVATION: activation_unit.apply(inst);   break;
        case STORE:     ub.write(inst);                break;
        case SYNC:      signal_host(); break;
    }
}
```

## End-to-End Inference: 2-Layer MLP

### Problem Setup

Model a simple 2-layer fully-connected network:
- Input: 256-dimensional vector
- Hidden layer: 256×256 weight matrix + ReLU
- Output layer: 256×10 weight matrix

### Weight and Activation Layouts

```
UB Address Map:
  0x000000: W1 weights (256×256 × 1 byte int8 = 64 KB)
  0x010000: W2 weights (256×10 × 1 byte int8 = 2.5 KB)
  0x020000: Input activations (256 × 1 byte = 256 B)
  0x030000: Hidden activations (256 × 1 byte = 256 B)
  0x040000: Output logits (10 × 4 bytes FP32)
```

### Instruction Sequence

```assembly
; --- Layer 1: Hidden = ReLU(Input × W1) ---
LOAD_WEIGHT   addr=0x000000, size=65536    ; Load W1 to systolic array
LOAD_ACT      addr=0x020000, size=256       ; Load input activations
MATMUL        M=256, N=256, K=1             ; Compute hidden (1×256 * 256×256)
ACTIVATION    type=RELU, size=256           ; Apply ReLU
STORE         dst=0x030000, size=256        ; Store hidden activations

; --- Layer 2: Output = Hidden × W2 ---
LOAD_WEIGHT   addr=0x010000, size=2560      ; Load W2 (10 output neurons, 256 inputs each)
LOAD_ACT      addr=0x030000, size=256        ; Load hidden activations
MATMUL        M=10, N=256, K=1              ; Compute output (1×256 * 256×10)
STORE         dst=0x040000, size=40          ; Store output (10 FP32 values)

SYNC                                          ; Signal completion
```

### Execution Timeline

```
Step  │ Cycle(s)   │ Operation           │ State
──────┼────────────┼─────────────────────┼────────────────────────
    0 │ —          │ Host sends program +│ UB populated
      │            │ data to TPU via PCIe│
    1 │ 1-64000    │ LOAD_WEIGHT W1      │ Weights in systolic array
    2 │ 1          │ LOAD_ACT input      │ Input in activation pipeline
    3 │ 1-256      │ MATMUL (pipelined)  │ Systolic array computes
    4 │ 1          │ ACTIVATION ReLU     │ Non-linearity applied
    5 │ 1          │ STORE hidden        │ Hidden activations in UB
    6 │ 1-2500     │ LOAD_WEIGHT W2      │ New weights in systolic array
    7 │ 1          │ LOAD_ACT hidden     │ Hidden in activation pipeline
    8 │ 1-256      │ MATMUL (pipelined)  │ Output computed
    9 │ 1          │ STORE output        │ Logits in UB
   10 │ 1          │ SYNC                │ Host signaled

Total: ~67,000 TPU cycles ≈ 96 µs at 700 MHz
```

### Performance Analysis

```
Total MAC operations:
  Layer 1: 256 inputs × 256 hidden = 65,536 MACs
  Layer 2: 256 hidden × 10 outputs = 2,560 MACs
  Total: 68,096 MACs

Systolic array utilization:
  Layer 1: 256/256 = 100% (perfect fit for 256×256 array)
  Layer 2: 10/256 = 3.9% (severe underutilization)

Time:
  Compute cycles: 256 (MATMUL pipelined, dominates)
  Load cycles: ~66,500 (weight loading, PCIe bandwidth limited)
  Wall clock: ~96 µs
```

The bottleneck here is weight loading, not compute. This motivates:
1. **Weight pre-loading**: All weights loaded before inference starts
2. **Batch processing**: Amortize weight load across many inputs
3. **Systolic array tiling**: Match weight matrix to array dimensions

## Comparison: TPU vs GPU vs CPU

| Metric | TPUv1 | NVIDIA K80 GPU | Intel Haswell CPU |
|--------|-------|----------------|-------------------|
| Peak TOPS (int8) | 92 | — | — |
| Peak TFLOPS (fp32) | — | 8.73 | 0.46 |
| Die area (mm²) | 331 | 561 | 662 |
| TDP (W) | 75 | 300 | 145 |
| TOPS/W | 1.23 | — | — |
| On-chip memory | 28 MB | 2 MB L2 | 8 MB L3 |
| Memory BW | 34 GB/s | 480 GB/s | 68 GB/s |

The TPU trades absolute performance for efficiency: lower TDP, higher TOPS/W, deterministic execution.

## Implementation Notes

### UB Memory Model

The simulator models the UB as a flat byte array with a simple bump allocator:

```c
uint32_t tpu_ub_alloc(TPUCore *tpu, uint32_t size_bytes) {
    uint32_t aligned = (size_bytes + 63) & ~63u;  // 64-byte aligned
    uint32_t addr = tpu->ub_alloc_ptr;
    tpu->ub_alloc_ptr += aligned;
    return addr;
}
```

Real TPUs use a more sophisticated memory manager, but the bump allocator captures the deterministic allocation pattern.

### Instruction Execution

Each instruction is executed atomically in `tpu_execute()`. The instruction reads from the unified buffer, operates on the systolic array or vector unit, and writes results back.

### Program Loading

The `tpu_load_program()` function copies a program from host memory into the TPU's unified buffer. The `tpu_step()` function fetches and executes the next instruction.

```c
void tpu_step(TPUCore *tpu) {
    TPUInstruction *program = (TPUInstruction *)tpu->unified_buffer;
    tpu_execute(tpu, &program[tpu->pc]);
    tpu->pc++;
    if (program[tpu->pc - 1].opcode == OP_SYNC)
        tpu->running = false;
}
```

## Expected Output

Running a TPU simulation produces:

```
TPU Core State:
  PC: 0, Running: yes
  UB: 24 MB, alloc_ptr: 0
  Accumulator buffer (first 8): 0.0000 0.0000 0.0000 ...

  TPU step 0: op=0 (MATMUL)
  [Systolic array output appears in accumulator buffer]
  TPU step 1: op=3 (ACTIVATION, RELU)
  [ReLU applied to accumulator buffer]
  TPU step 2: op=6 (STORE)
  [Results written to UB]
  TPU step 3: op=7 (SYNC)
  Running: no
```

## Build and Run

```bash
cd mini-ai-accelerator
make
./bin/tpu_demo
```

## References

1. Jouppi, N.P. et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017
2. Jouppi, N.P. et al., "Ten Lessons from Three Generations Shaped Google's TPUv4i", ISCA 2021
3. Google Cloud TPU Documentation, https://cloud.google.com/tpu/docs
4. MIT 6.5930: Hardware Architecture for Deep Learning, Lecture 7 — TPU Architecture
5. Dean, J. et al., "Large Scale Distributed Deep Networks", NIPS 2012

## Appendix: CISC vs RISC in Accelerators

The TPU's CISC ISA is a deliberate design choice:

| CISC (TPU) | RISC (RISC-V) | VLIW (DianNao) | SIMT (GPU) |
|------------|--------------|----------------|------------|
| 20 instructions | 100+ instructions | Wide bundles | ~30 instructions |
| Deterministic latency | Variable (cache) | Deterministic | Variable (warp) |
| Exposed memory | Cached memory | Scratchpad | Both |
| Simple control | Complex decode | Complex compiler | Warp scheduler |
| Domain-specific | General-purpose | Domain-specific | General-purpose |

The TPU ISA is optimized for the narrow domain of dense linear algebra — simplicity is a feature, not a bug.
