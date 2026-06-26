# Quantization Techniques — Survey and Taxonomy

## Introduction

Quantization maps continuous floating-point values to discrete integer representations. For neural networks, this reduces model size, memory bandwidth, and energy consumption — often with minimal accuracy degradation.

### Motivation by Numbers

| Metric | FP32 | FP16 | BFLOAT16 | INT8 |
|--------|------|------|----------|------|
| Bits per value | 32 | 16 | 16 | 8 |
| Dynamic range | ±3.4×10³⁸ | ±65504 | ±3.4×10³⁸ | ±128 (with scale) |
| Precision (near 0) | ~10⁻⁷ | ~10⁻³ | ~10⁻² | ~10⁻² (with scale) |
| Memory (1B params) | 4 GB | 2 GB | 2 GB | 1 GB |
| Energy per MAC | 4.6 pJ | 1.1 pJ | 1.0 pJ | 0.2 pJ |
| Throughput (A100) | 19.5 TFLOPS | 312 TFLOPS | 312 TFLOPS | 624 TOPS |

## Quantization Taxonomy

### By Bit Width

| Format | Bits | Levels | Typical Use |
|--------|------|--------|-------------|
| FP32 | 32 | ~4.3×10⁹ | Training reference |
| FP16 | 16 | ~65K | Mixed precision training |
| BFLOAT16 | 16 | ~65K | TPU training/inference |
| INT8 | 8 | 256 | Inference (universal) |
| FP8 (E4M3) | 8 | 256 | H100 training/inference |
| INT4 | 4 | 16 | Edge inference, LLM compression |
| FP4 (E2M1) | 4 | 16 | Experimental |
| INT2 / Ternary | 2-3 | 3-8 | Extreme compression |

### By Symmetry

#### Symmetric Quantization

```
q = round(x / S)
x' = S × q

Range: [-127, 127] for INT8
Zero point Z = 0 (simplifies hardware)
```

**Pros**: No zero-point addition in MAC pipeline
**Cons**: Wastes half of quantization range for positive-only distributions

#### Asymmetric Quantization

```
q = round(x / S) + Z
x' = S × (q - Z)

Range: [0, 255] for INT8 unsigned
Zero point Z maps real 0 to quantization level
```

**Pros**: Better utilization of quantization range
**Cons**: Requires zero-point correction in convolution/matmul accumulation

### By Granularity

| Granularity | Scale Count | Example | Accuracy vs INT8 |
|------------|-------------|---------|-------------------|
| Per-Tensor | 1 per tensor | W ∈ R^{C_out×C_in}: 1 scale | Baseline |
| Per-Channel | 1 per output channel | W ∈ R^{C_out×C_in}: C_out scales | +0.3% |
| Per-Group | 1 per 128 elements | Split channel dim into groups | +0.5% |
| Per-Token | 1 per token | Activations: 1 scale per sentence token | +0.1% |
| Per-Element | 1 per element | Not used (too much overhead) | — |

#### Per-Channel Example

```
Weight matrix W ∈ R^{4×4}:

Channel 0: [-0.047, 0.051, -0.010, 0.003]  → S₀=0.000384, Z₀=122
Channel 1: [-0.023, 0.031, 0.002, -0.008]  → S₁=0.000212, Z₁=108
Channel 2: [-0.089, 0.063, -0.015, 0.025] → S₂=0.000596, Z₂=149
Channel 3: [-0.015, 0.042, 0.000, -0.006]  → S₃=0.000224, Z₃=67

Per-Tensor:
  Global min/max: -0.089 to 0.063
  S = 0.000596, Z = 149

Per-Channel vs Per-Tensor:
  Channel 1 range: 0.054, Tensor range: 0.152
  Quantization step for channel 1:
    Per-tensor: S/255 = 0.000596/255 ≈ 2.34×10⁻⁶
    Per-channel: S₁/255 = 0.000212/255 ≈ 8.31×10⁻⁷
  Per-channel is ~3× finer for narrow-range channels!
```

## Calibration Methods

### Min-Max Calibration

```
S = (r_max - r_min) / (q_max - q_min)
Z = round(q_min - r_min / S)
```

Simple and fast. Problem: sensitive to outliers — a single outlier stretches the quantization range, increasing quantization error for all values.

### KL Divergence Calibration

Used by NVIDIA TensorRT:

```
1. Collect activation histograms (2048 bins) over calibration dataset (100-1000 samples)
2. For each possible threshold T:
   a. Clip all values > T to T
   b. Quantize histogram to INT8 range (256 or 128 bins)
   c. Dequantize histogram back
   d. Compute KL(P_fp32 || P_8bit)
3. Select T* minimizing KL divergence
```

Mathematically:

```
P_fp32[i] = count[i] / total_count          (original distribution)
T* = argmin_T Σᵢ P_fp32[i] × log(P_fp32[i] / Q_T[i])

where Q_T is the quantized distribution with clipping at T
```

KL divergence is asymmetric — it penalizes quantization errors in high-probability regions more heavily.

### Percentile Calibration

```
r_max = percentile_{99.99}(|x|)    // clip top 0.01% of activations
r_min = -r_max                      // symmetric
```

Simpler than KL, works well for long-tailed distributions.

### Moving Average (Training)

```
r_min = β × r_min + (1-β) × current_min
r_max = β × r_max + (1-β) × current_max

Typical β = 0.99
```

Used during QAT to track activation ranges with exponential smoothing.

## Post-Training Quantization (PTQ)

PTQ takes a pre-trained FP32 model and directly quantizes it without retraining.

### PTQ Pipeline

```
1. FP32 Model (pre-trained)
2. Insert quantization observers (for activation ranges)
3. Run calibration dataset (100-1000 batches)
   → Collect activation min/max per tensor
4. Compute quantization parameters (S, Z) per tensor/channel
5. Replace FP32 ops with INT8 ops
6. Verify accuracy < threshold (typically ≤ 1% top-1 drop)
```

### PTQ Accuracy Drop Sources

| Source | Magnitude | Mitigation |
|--------|-----------|------------|
| Weight quantization error | ~0.1-0.5% | Per-channel quantization |
| Activation quantization error | ~0.2-1.0% | KL calibration, QAT |
| Bias shift (from weight rounding) | ~0.05% | Absorb bias shift |
| BatchNorm folding error | ~0.1% | Fold before quantization |

### BatchNorm Folding

Before quantization, BatchNorm parameters are folded into preceding convolution weights:

```
Conv: y = W × x + b
BN:   z = γ × (y - μ) / σ + β

Folded:
  W' = γ × W / σ
  b' = γ × (b - μ) / σ + β

Then quantize W' and b' directly.
```

This removes BatchNorm at inference time — a standard optimization in all quantized inference engines.

## Quantization-Aware Training (QAT)

QAT simulates quantization during training, allowing the model to adapt.

### Fake Quantization

During forward pass, weights and activations pass through "fake quantization" nodes:

```
def fake_quantize(x, S, Z):
    x_int = round(x / S + Z)
    x_int = clamp(x_int, q_min, q_max)
    x_fq = S × (x_int - Z)
    return x_fq
```

During backward pass, use the Straight-Through Estimator (STE):

```
∂L/∂x = ∂L/∂x_fq × 1_{q_min ≤ x/S+Z ≤ q_max}
```

The STE treats the rounding operation as identity for gradient computation. This works because:
1. For most parameters, the gradient direction is correct even if magnitude is approximate
2. The model can compensate for the approximation with its remaining degrees of freedom

### QAT Training Schedule

```
Phase 1: FP32 pre-training (90% of total epochs)
Phase 2: Insert fake quant, freeze BN statistics (1 epoch)
Phase 3: QAT fine-tuning (10% of epochs)
  - Learning rate: 1/10 to 1/100 of original
  - Sometimes use cosine decay
  - Freeze learned quantization parameters after warm-up
Phase 4: Export INT8 model
```

## Advanced Quantization Techniques

### Mixed Precision Quantization

Different layers use different precisions:

```
Layer         | Precision | Reason
──────────────┼───────────┼────────────────────
Conv1         | INT8      | Standard convolution
Conv2-Depthwise| INT8      | Lightweight, sensitive
Self-Attention| FP16      | Often sensitivity bottleneck
FFN           | INT8      | Heavy, robust to quantization
LM Head       | FP16      | Classification precision
Embedding     | INT4      | Huge memory savings, robust
```

### SmoothQuant: Smoothing Activation Outliers

Problem: LLM activations have massive outliers:

```
LLaMA-13B, activation channel 1432:
  Other channels: |x| < 20
  Channel 1432: |x| up to 287  ← 14× outlier!
  
Quantization error for channel 1432:
  S = 287 / 127 = 2.26
  ε_max = S/2 = 1.13  ← huge error!
```

Solution: Scale transformation:

```
Y = X × W
  = (X · diag(Λ)⁻¹) × (diag(Λ) · W)
  = X̂ × Ŵ

Choose Λ:
  λⱼ = max(|Xⱼ|)^α / max(|Wⱼ|)^(1-α)

  α = 0.5: Equal migration to X and W
  α < 0.5: Migrate more to W (weights easier to quantize)
  α > 0.5: Migrate to X only
```

After smoothing, both X̂ and Ŵ have more uniform dynamic ranges — easier to quantize.

### LLM.int8(): Mixed Precision for Outliers

Dettmers et al. (2022) observe that < 0.1% of activation features have outlier magnitude, but cause the majority of quantization error.

Solution:
```
def matmul_int8(X, W):
    # Identify outlier columns in X
    outliers = where(max(|X_col|) > threshold)
    
    # Split computation
    Y_normal = INT8_matmul(X_normal, W_normal)     # INT8 matmul (99.9%)
    Y_outlier = FP16_matmul(X_outlier, W_outlier)   # FP16 matmul (0.1%)
    
    return Y_normal + Y_outlier
```

This achieves FP16 accuracy with ~2× throughput of FP16 FP16 matmul.

### GPTQ: Optimal Brain Damage Quantization

GPTQ (Frantar & Alistarh, 2023) quantizes weights column-by-column using the Hessian-based OBS framework:

```
For each column j:
    1. Quantize wⱼ → ŵⱼ (round to nearest)
    2. Compute quantization error: εⱼ = wⱼ - ŵⱼ
    3. Update remaining columns: w_{>j} -= (εⱼ / [H⁻¹]ⱼⱼ) × [H⁻¹]_{j,>j}

H ≈ 2 × XᵀX / n   (approximate Hessian from calibration data)
```

This compensates for quantization error in column j by adjusting subsequent columns, minimizing the overall output error.

## Error Analysis

### Quantization Error Distribution

For uniform quantization with rounding-to-nearest:

```
Error ε = x - x̂ is uniformly distributed on [-S/2, S/2]

E[ε] = 0                              (zero mean for rounding-to-nearest)
Var[ε] = S² / 12                      (quantization noise power)
```

For truncation (floor): E[ε] = S/2, Var[ε] = S²/12 (biased)

Signal-to-Quantization-Noise Ratio (SQNR):

```
SQNR = 10 × log₁₀(σ² / (S²/12))

For full-range usage with INT8:
  σ ≈ DynamicRange / 6.6 (= range covering 99.9% of Gaussian)
  S ≈ DynamicRange / 256
  SQNR ≈ 10 × log₁₀((DR/6.6)² / (DR/256)² / 12)
       = 10 × log₁₀(256² / (6.6² × 12))
       = 10 × log₁₀(1024)
       ≈ 30 dB (symmetric)
       ≈ 48 dB (full-range uniform)
```

INT4: SQNR ≈ 10 × log₁₀(16²/12) ≈ 13 dB (uniform) to 24 dB (Gaussian). Only 4-6 bits of effective precision, requiring careful calibration.

### Layer-Wise Error Propagation

Quantization errors propagate through layers:

```
Layer i output error: δⱼ = f(δ_{j-1} + εⱼ)

where εⱼ is the quantization error introduced at layer j

Total error after L layers: Δ_L ≈ Σ_{j=1}^{L} (Π_{k=j+1}^{L} ||Wₖ||₂) × εⱼ
```

Deeper networks accumulate more error. Residual connections (ResNet) mitigate this by providing direct gradient paths.

## Hardware Considerations

### INT8 GEMM Computation

```
C = A (INT8) × B (INT8) + bias (INT32)

Dimension: A ∈ R^{M×K}, B ∈ R^{K×N}, C ∈ R^{M×N}

Computation in hardware:
  for i in range(M):
      for j in range(N):
          C_int32[i][j] = bias[i][j]
          for k in range(K):
              C_int32[i][j] += (int32)A[i][k] × (int32)B[k][j]
      
  // Requantize: int32 → int8 or fp32
  C_fp32[i][j] = (float)(C_int32[i][j] - Z_out) × S_out
```

Zero-point correction in matrix multiply:

```
C_int32[i][j] = Σₖ (A_q[i][k] - Z_A) × (B_q[k][j] - Z_B)
              = Σₖ A_q[i][k]×B_q[k][j]
              - Z_A × Σₖ B_q[k][j]
              - Z_B × Σₖ A_q[i][k]
              + K × Z_A × Z_B

The last 3 terms are the "zero-point correction" — needs additional computation.
Symmetric quantization (Z=0) simplifies this to just Σ A_q×B_q.
```

### Vectorized INT8 Operations

Intel VNNI (AVX-512_VNNI), ARM NEON SDOT, and NVIDIA DP4A provide INT8 dot-product instructions:

```
VNNI: VPDPBUSD zmm, zmm, zmm
  // 4 × multiply-accumulate per lane: int8×uint8 → int32 accumulate to 32-bit in ZMM

Throughput:
  AVX-512 VNNI: 256 INT8 MACs/cycle/core (2 VNNI units × 512b × 1/4 bytes × 2 ops/MAC)
  NVIDIA SM (DP4A): 512 INT8 MACs/cycle/SM (A100, 8 DP4A per warp scheduler)
```

## Summary Table

| Technique | When | Accuracy Cost | Speedup | Memory Reduction |
|-----------|------|--------------|---------|-----------------|
| PTQ INT8 per-tensor | Always first try | < 0.5% | 2-4× | 4× |
| PTQ INT8 per-channel | Per-tensor fails | < 0.1% | 2-4× | 4× |
| QAT INT8 | Accuracy-critical | < 0.1% | 2-4× | 4× |
| PTQ INT4 | Memory-constrained | 1-5% | 4-8× | 8× |
| GPTQ (INT4) | LLM inference | 0.5-2% | 2-4× | 8× |
| SmoothQuant INT8 | LLM with outliers | < 0.5% | 2-4× | 4× |
| FP8 training | Transformer training | < 0.1% | 2× | 4× |
| Mixed-precision | Layer-specific needs | varies | target-dependent | target-dependent |

## References

1. Jacob, B. et al., "Quantization and Training of Neural Networks for Efficient Integer-Arithmetic-Only Inference", CVPR 2018
2. Krishnamoorthi, R., "Quantizing Deep CNNs for Efficient Inference: A Whitepaper", arXiv:1806.08342
3. Nagel, M. et al., "A White Paper on Neural Network Quantization", arXiv:2106.08295
4. Dettmers, T. et al., "LLM.int8(): 8-bit Matrix Multiplication for Transformers at Scale", NeurIPS 2022
5. Xiao, G. et al., "SmoothQuant: Accurate and Efficient Post-Training Quantization for LLMs", ICML 2023
6. Frantar, E. et al., "GPTQ: Accurate PTQ for GPTs", ICLR 2023
7. Lin, J. et al., "AWQ: Activation-aware Weight Quantization", MLSys 2024
8. Gholami, A. et al., "A Survey of Quantization Methods for Efficient Neural Network Inference", arXiv:2103.13630
9. Micikevicius, P. et al., "Mixed Precision Training", ICLR 2018
10. NVIDIA, "INT8 Inference with TensorRT", https://developer.nvidia.com/tensorrt
