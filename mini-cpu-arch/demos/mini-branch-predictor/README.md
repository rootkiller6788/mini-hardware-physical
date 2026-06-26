# mini-branch-predictor — 分支预测器综述

> Branch prediction survey: why branches hurt pipelining, static vs dynamic prediction, bimodal, two-level adaptive, gshare, tournament predictors, perceptron, and TAGE.

---

## Overview / 概览

Branch instructions are the primary barrier to high instruction-level parallelism (ILP) in pipelined processors. A mispredicted branch flushes the pipeline, wasting 10-20+ cycles in modern deep pipelines. This demo explores branch prediction techniques from simple static heuristics to state-of-the-art neural predictors.

### Why Branches Are Hard / 为什么分支难预测

| Issue | Impact |
|-------|--------|
| Control dependency | Fetch stalls until branch resolved |
| Pipeline flush | Misprediction discards N instructions (N = pipeline depth) |
| Branch frequency | ~20% of instructions are branches |
| Indirect jumps | Target address not known at fetch |

---

## Architecture / 架构

### Branch Prediction Taxonomy

```
Branch Prediction
|
+-- Static Prediction
|   +-- Always Not Taken
|   +-- Always Taken
|   +-- Backward Taken, Forward Not Taken (BTFN)
|   +-- Profile-guided (compiler hints)
|
+-- Dynamic Prediction
    +-- 1-bit Predictor (last outcome)
    +-- 2-bit Saturating Counter (Bimodal)
    +-- Two-Level Adaptive (correlating)
    |   +-- GAg (Global history -> one PHT)
    |   +-- PAg (Per-address -> one PHT)
    |   +-- GAp (Global -> per-address PHT)
    |   +-- PAp (Per-address -> per-address PHT)
    +-- Gshare (Global history XOR PC)
    +-- Tournament (hybrid: bimodal + two-level)
    +-- Perceptron (neural network)
    +-- TAGE (tagged geometric history length)
```

### 2-Bit Saturating Counter State Machine

```
    Strong Not Taken (00)
         |
    [NT] |  [T]
         v
    Weak Not Taken (01) ----[T]----> Weak Taken (10)
         ^                            |
         |          [NT]              | [T]
         +--------------------------- v
                                Strong Taken (11)
```

State encoding:
| State | Value | Meaning |
|-------|-------|---------|
| SN | 00 | Strongly Not Taken |
| WN | 01 | Weakly Not Taken |
| WT | 10 | Weakly Taken |
| ST | 11 | Strongly Taken |

### Bimodal Predictor

```
PC ----+----> [ BHT: 256 x 2-bit counters ] ----> Predict
```

Index = (PC >> 2) % 256
Each entry is a 2-bit saturating counter.
Prediction: taken if counter >= 10 (WT or ST).

### Two-Level Adaptive Predictor

```
                   Pattern History Table
   Branch History  +-------------------+
        |          |   PHT[0][0..63]   |
   +----v----+     |   PHT[1][0..63]   |
   |   BHT   | --> |   PHT[2][0..63]   |
   | 256 x 2 |     |       ...         |
   +---------+     |   PHT[255][0..63] |
                   +-------------------+
```

First level (BHT): indexes into second level using branch address.
Second level (PHT): indexed by BHT entry's history pattern.
Each PHT entry is a 2-bit counter.

### Gshare Predictor

```
   PC[2:9] -----+
                 +---> [XOR] ----> PHT[256] ----> Predict
   GHR[0:7] -----+
```

Global History Register (GHR) XORed with PC bits provides better indexing,
reducing aliasing compared to direct indexing.

### Tournament Predictor

```
   PC ----> Bimodal ----+
                        +---> Meta-Predictor (2-bit) ----> Final Predict
   PC ----> Gshare  ----+
```

Uses a meta-predictor to choose between bimodal and gshare on a per-branch basis.
Adapts to branches that are more predictable by one method vs the other.

### Perceptron Predictor

```
   x0 (bias) --- w0 ----+
                        |
   x1 (GHR[0]) - w1 ---+
                        |
   x2 (GHR[1]) - w2 ---+---> y = w0 + sum(wi * xi)
                        |       predict taken if y >= 0
   ...                  |
                        |
   xn (GHR[n]) - wn ---+
```

A single-layer neural network with n inputs (GHR bits) and n+1 weights (including bias).
Training: if prediction wrong or |y| < threshold, update weights: wi += xi (if taken) or wi -= xi.

---

## Implementation Steps / 实现步骤

### Step 1: 2-Bit Counter

```c
typedef enum { SN = 0, WN = 1, WT = 2, ST = 3 } BPState;

void bp_update_counter(uint8_t* counter, bool taken) {
    if (taken) {
        if (*counter < ST) (*counter)++;  // strengthen taken
    } else {
        if (*counter > SN) (*counter)--;  // strengthen not-taken
    }
}

bool bp_predict_counter(uint8_t counter) {
    return (counter == WT || counter == ST);
}
```

### Step 2: Bimodal Predictor

```c
#define BHT_SIZE 256
uint8_t bht[BHT_SIZE];  // all initialized to WN

bool bimodal_predict(uint32_t pc) {
    uint32_t idx = (pc >> 2) % BHT_SIZE;
    return (bht[idx] >= WT);
}

void bimodal_update(uint32_t pc, bool taken) {
    uint32_t idx = (pc >> 2) % BHT_SIZE;
    bp_update_counter(&bht[idx], taken);
}
```

### Step 3: Two-Level Predictor

```c
#define PHT_COLS 64
uint8_t bht[BHT_SIZE];
uint8_t pht[BHT_SIZE][PHT_COLS];
uint8_t ghr;  // 6-bit global history

bool twolevel_predict(uint32_t pc) {
    uint32_t bht_idx = (pc >> 2) % BHT_SIZE;
    uint8_t pattern = bht[bht_idx];  // or ghr
    return (pht[bht_idx][pattern] >= WT);
}

void twolevel_update(uint32_t pc, bool taken) {
    uint32_t bht_idx = (pc >> 2) % BHT_SIZE;
    uint8_t old_pattern = bht[bht_idx];
    bp_update_counter(&pht[bht_idx][old_pattern], taken);
    bp_update_counter(&bht[bht_idx], taken);
}
```

### Step 4: Gshare Predictor

```c
bool gshare_predict(uint32_t pc) {
    uint32_t hash = ((pc >> 2) ^ ghr) % BHT_SIZE;
    return (pht[hash][0] >= WT);
}

void gshare_update(uint32_t pc, bool taken) {
    uint32_t hash = ((pc >> 2) ^ ghr) % BHT_SIZE;
    bp_update_counter(&pht[hash][0], taken);
    ghr = ((ghr << 1) | (taken ? 1 : 0)) & 0x3F;  // 6-bit shift
}
```

---

## Accuracy Comparison / 准确率对比

Typical prediction accuracies for various predictors on SPEC benchmarks:

| Predictor | Integer (%) | FP (%) | Hardware Cost |
|-----------|------------|--------|---------------|
| Always Taken | 60-65 | 70-75 | 0 bits |
| Always Not Taken | 35-40 | 25-30 | 0 bits |
| Bimodal (2K entries) | 85-92 | 90-95 | 4 Kbits |
| Two-Level (1K BHT + 1K PHT) | 88-94 | 92-97 | 8 Kbits |
| Gshare (8K entries) | 90-95 | 93-98 | 16 Kbits |
| Tournament (8K) | 92-96 | 95-99 | 24 Kbits |
| Perceptron (256 weights) | 93-96 | 96-99 | 4 KB |
| TAGE (multi-length) | 95-98 | 97-99.5 | 30+ KB |

---

## Key Insights / 关键要点

1. **2-bit counters** resist single "flips" — a TNTNTN pattern gives 50% accuracy
2. **Correlating predictors** capture patterns like alternating branches
3. **Gshare** reduces aliasing: two branches sharing a BHT entry get different PHT entries via XOR
4. **Tournament** adapts to which predictor works better per-branch
5. **Perceptron** can learn linearly separable patterns; limited by non-linear branch behavior
6. **TAGE** uses multiple history lengths; longer histories for regular patterns, shorter for irregular

## References / 参考

- McFarling, "Combining Branch Predictors" (1993 WRL Technical Note) — tournament predictor
- Yeh & Patt, "Two-Level Adaptive Branch Prediction" (1991 MICRO)
- Jimenez & Lin, "Dynamic Branch Prediction with Perceptrons" (2001 HPCA)
- Seznec & Michaud, "A Case for (Partially) Tagged Geometric History Length Branch Prediction" (2006 JILP) — TAGE
- MIT 6.175 RISC-V Branch Predictor Lab
- Stanford EE282 Branch Prediction Lecture

---

## Build & Run / 构建与运行

```bash
make
./bin/branch_pred_demo
```

Expected output: accuracy comparison of all 5 predictor types on a repeating pattern.
