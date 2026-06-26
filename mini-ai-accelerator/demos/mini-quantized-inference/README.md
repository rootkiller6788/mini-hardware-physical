# mini-quantized-inference — Quantization Techniques for AI Inference

## Overview

Quantization reduces the numerical precision of model weights and activations from 32-bit floating point (FP32) to lower bit widths (INT8, INT4, FP8). This reduces model size, memory bandwidth, and energy consumption, often with minimal accuracy loss — enabling deployment on resource-constrained edge devices and improving data center throughput.

> Reference: Krishnamoorthi, R., "Quantizing Deep Convolutional Networks for Efficient Inference: A Whitepaper", 2018

### Why Quantization?

| Metric | FP32 | INT8 | Reduction |
|--------|------|------|-----------|
| Model size | 4 bytes/param | 1 byte/param | 4× |
| Memory bandwidth | 4 bytes/MAC | 1 byte/MAC | 4× |
| Energy/MAC | ~4.6 pJ | ~0.2 pJ | 23× |
| Throughput (same area) | 1× | 4× | 4× |

For large models (GPT-3: 175B parameters), FP32 requires 700 GB of memory. INT8 reduces this to 175 GB — still large, but fitting on fewer GPUs.

## Theory of Quantization

### Affine Quantization (Asymmetric)

The most common INT8 quantization scheme maps a floating-point range [r_min, r_max] to [0, 255]:

```
q = round(r / S) + Z
r' = S × (q - Z)
```

Where:
- `r`: original float value
- `q`: quantized integer value
- `S`: scale factor (float)
- `Z`: zero point (integer)
- `r'`: dequantized (reconstructed) value

The scale `S` and zero point `Z` are computed from the data range:

```
S = (r_max - r_min) / (q_max - q_min) = (r_max - r_min) / 255
Z = round(q_min - r_min / S) = round(0 - r_min / S)
```

### Symmetric Quantization

A simplified scheme where Z = 0 and the range is symmetric around zero:

```
S = max(|r_max|, |r_min|) / 127
q = round(r / S)          // clamped to [-127, 127]
```

Symmetric quantization is simpler to implement in hardware (no zero-point addition) but may waste bits if the distribution is not symmetric.

### Per-Tensor vs Per-Channel Quantization

**Per-Tensor**: One scale and zero point for the entire weight tensor.

```
For W ∈ R^{(C_out × C_in × K_h × K_w)}:
  S = (max(W) - min(W)) / 255
  Z = round(-min(W) / S)
```

**Per-Channel**: Separate scale/zero-point for each output channel (row).

```
For W ∈ R^{(C_out × C_in × K_h × K_w)}:
  For each output channel c ∈ [0, C_out):
    S_c = (max(W[c,:,:,:]) - min(W[c,:,:,:])) / 255
    Z_c = round(-min(W[c,:,:,:]) / S_c)
```

Per-channel is more accurate because it accounts for channel-wise variation in weight ranges:

```
Weight Range per Channel (ResNet-50 conv1):
  Channel  0: [-0.047, 0.051]  → S=0.000384, Z=122
  Channel  1: [-0.023, 0.031]  → S=0.000212, Z=108
  Channel  2: [-0.089, 0.063]  → S=0.000596, Z=149
  ...
  Channel 63: [-0.015, 0.042]  → S=0.000224, Z=67
```

Per-tensor would use the global range [-0.089, 0.063], sacrificing precision for channels with narrow ranges.

### Calibration Methods

Before quantization, we need a "representative dataset" (typically 100-1000 samples) to determine activation ranges.

#### Method 1: Min-Max

```
r_min = min(all activation values)
r_max = max(all activation values)
```
Simple but sensitive to outliers. A single outlier stretches the range, wasting quantization levels.

#### Method 2: KL Divergence (NVIDIA TensorRT)

Find the threshold T that minimizes KL divergence between the full distribution and the quantized distribution:

```
T* = argmin_T KL(P_fp32 || P_quantized(T))

where P_fp32 = histogram of activations (2048 bins)
      P_quantized = P_fp32 with values > T clipped to T, then quantized to 128 bins
```

This gracefully handles outliers by clipping them.

#### Method 3: Percentile (99.99%)

```
r_max = percentile_99.99(activations)  // clip top 0.01%
```

Similar to KL divergence but simpler.

#### Method 4: Moving Average (for training)

During quantization-aware training (QAT), statistics are tracked with exponential moving average:

```
r_min = EMA(r_min, current_min, momentum=0.01)
r_max = EMA(r_max, current_max, momentum=0.01)
```

### Quantization Error

The quantization error for a single value:

```
ε_i = r_i - r'_i = r_i - S × (round(r_i / S) + Z)
```

The maximum quantization error (assuming rounding to nearest):

```
|ε_i| ≤ S / 2
```

Mean squared error:

```
MSE = (1/N) × Σᵢ ε_i²

For uniform distribution, MSE = S² / 12   (quantization noise power)
```

Signal-to-quantization-noise ratio (SQNR):

```
SQNR = 10 × log₁₀(σ² / MSE)

where σ² is the variance of the original signal

For INT8: SQNR ≈ 48 dB (assuming full range usage)
For INT4: SQNR ≈ 24 dB
```

## Quantization-Aware Training (QAT)

Post-training quantization (PTQ) quantizes a pre-trained FP32 model. QAT inserts "fake quantization" nodes during training, so the model learns to be robust to quantization error.

### Fake Quantization Node

During forward pass:

```
def fake_quant(x, S, Z, qmin, qmax):
    x_q = quantize(x, S, Z)       # round(x/S + Z), clamp to [qmin, qmax]
    x_fq = dequantize(x_q, S, Z)   # S × (x_q - Z)
    return x_fq
```

During backward pass, the gradient passes through the quantization node (straight-through estimator, STE):

```
∂L/∂x = ∂L/∂x_fq × 1_{x ∈ [S×qmin, S×qmax]}
```

The STE ignores the quantization step in the backward pass, effectively treating it as an identity for gradient flow. This works surprisingly well in practice.

### QAT Workflow

```
1. Train FP32 model to convergence
2. Insert fake quantization nodes after weights and activations
3. Fine-tune for a few epochs (typically 10-30% of original training)
   - Lower learning rate (1/10 to 1/100 of original)
   - Freeze batch norm statistics early
4. Export quantized model (INT8 weights, INT8 activations)
```

## Advanced Topics

### INT4 Quantization

4-bit quantization (16 levels) dramatically reduces memory but is more challenging:

```
Range: [-8, 7] (signed) or [0, 15] (unsigned)
MSE floor: S²/12 where S is proportionally larger than INT8
```

Techniques for INT4:
- **GPTQ** (Frantar et al., 2023): Optimal Brain Damage-inspired weight quantization
- **AWQ** (Lin et al., 2023): Activation-aware weight quantization — protect salient weight channels
- **GGUF/GGML**: Mixed INT4/INT8 quantization with super-block scaling (16 weights share one scale)

### FP8 Quantization (IEEE 754-style)

```
Format: 1 sign | 4 exponent | 3 mantissa  (E4M3)
        or 1 sign | 5 exponent | 2 mantissa (E5M2)

Range:  E4M3: ±[2⁻⁶, 448], E5M2: ±[2⁻¹⁴, 57344]
```

FP8 provides non-uniform quantization — higher precision near zero, wider range at extremes. Better for activations with long-tailed distributions.

### Outlier-Aware Quantization

Large language models exhibit massive outliers in activations:

```
LLaMA-13B activation statistics:
  99.9th percentile: 15.2
  Maximum: 287.3  ← Outlier, 19× larger than 99.9th percentile!
```

Solutions:
- **SmoothQuant** (Xiao et al., 2023): Migrate quantization difficulty from activations to weights via scaling
- **LLM.int8()** (Dettmers et al., 2022): Mixed-precision — outlier features in FP16, rest in INT8

### SmoothQuant

The insight: weights are easy to quantize; activations (with outliers) are hard.

```
Given: Y = X × W

Transform: Y = (X × diag(s)⁻¹) × (diag(s) × W)

Choose s_j = max(|X_j|)^α / max(|W_j|)^(1-α)

α = 0.5 balances the difficulty between X and W
```

This "smooths" the activation magnitudes, moving outlier difficulty into the weight matrix.

## Implementation

### Quantization Data Flow

```c
// 1. Calibrate: find quantization parameters
QuantParams params = quant_find_params(float_data, len, QUANT_INT8);
// params = {scale: 0.0412, zero_point: 72, min: -3.0, max: 7.5}

// 2. Quantize: float → uint8
uint8_t *quantized = malloc(len * sizeof(uint8_t));
quant_quantize(float_data, len, &params, quantized);
// float_data[0] = 2.5 → quantized[0] = round(2.5/0.0412) + 72 = 133

// 3. Dequantize: uint8 → float (for verification or activation)
float *dequantized = malloc(len * sizeof(float));
quant_dequantize(quantized, len, &params, dequantized);
// quantized[0] = 133 → dequantized[0] = (133-72) × 0.0412 = 2.513
```

### Per-Channel Implementation

For a weight matrix W ∈ R^{C_out × C_in}:

```c
QuantParams *ch_params = quant_per_channel_find_params(W, C_out, C_in, QUANT_INT8);

// Quantize each row separately
for (int c = 0; c < C_out; c++) {
    quant_quantize(&W[c * C_in], C_in, &ch_params[c],
                   &quantized[c * C_in]);
}
```

### Error Metrics

```c
float mse = quant_compute_mse(original, quantized, len, &params);
float max_err = quant_compute_max_error(original, quantized, len, &params);
// Example output:
// MSE: 0.000438
// Max Error: 0.123456
```

## Expected Output from Quantize Demo

```
Original float data:
  -3.000  -2.000  -1.000   0.000   1.000   2.000   3.000   4.000
   5.000   6.000   7.000   7.500  -1.500   0.500   2.500   4.500

=== Per-Tensor Quantization ===
  Scale: 0.041176
  Zero point: 72
  Range: [-3.0000, 7.5000]

Quantized values (uint8):
    0    24    48    72    96   120   144   168
  193   217   241   254    36    84   132   181

Dequantized back to float:
  -2.965  -1.976  -0.988   0.000   0.988   1.976   2.965   3.953
   4.982   5.971   6.959   7.496  -1.483   0.494   2.471   4.488

Quantization Error Analysis:
  Scale: 0.041176, Zero point: 72
  MSE: 0.000617
  Max Error: 0.020588
```

The maximum error of ~0.02 corresponds to S/2 = 0.0206, which is exactly the theoretical maximum for uniform quantization.

## Build and Run

```bash
cd mini-ai-accelerator
make quantize_demo
./bin/quantize_demo
```

## References

1. Krishnamoorthi, R., "Quantizing Deep Convolutional Networks for Efficient Inference: A Whitepaper", arXiv:1806.08342, 2018
2. Nagel, M. et al., "A White Paper on Neural Network Quantization", arXiv:2106.08295, 2021
3. Jacob, B. et al., "Quantization and Training of Neural Networks for Efficient Integer-Arithmetic-Only Inference", CVPR 2018
4. Dettmers, T. et al., "LLM.int8(): 8-bit Matrix Multiplication for Transformers at Scale", NeurIPS 2022
5. Xiao, G. et al., "SmoothQuant: Accurate and Efficient Post-Training Quantization for Large Language Models", ICML 2023
6. Frantar, E. et al., "GPTQ: Accurate Post-Training Quantization for Generative Pre-trained Transformers", ICLR 2023
7. Lin, J. et al., "AWQ: Activation-aware Weight Quantization for LLM Compression and Acceleration", MLSys 2024
8. Micikevicius, P. et al., "Mixed Precision Training", ICLR 2018

## Summary

| Technique | Granularity | Bits | Accuracy Impact | Hardware Support |
|-----------|------------|------|-----------------|------------------|
| PTQ INT8 per-tensor | Tensor | 8 | < 0.5% drop | Universal (VNNI, TensorRT) |
| PTQ INT8 per-channel | Channel | 8 | < 0.1% drop | GPU, TPU (not all edge) |
| QAT INT8 | Tensor | 8 | < 0.1% drop | Universal |
| INT4 (GPTQ/AWQ) | Group (128) | 4 | 1-3% drop | GPU (Turing+) |
| FP8 (E4M3) | Tensor | 8 | < 0.1% drop | H100, MI300X |
| SmoothQuant | Tensor | 8 | < 0.5% drop | GPU, CPU |

Quantization is not free — it requires careful calibration and sometimes re-training. But for deployment at scale, the 4× memory reduction and 23× energy reduction make it essential.
