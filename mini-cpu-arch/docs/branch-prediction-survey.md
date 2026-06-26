# Branch Prediction Survey — 分支预测技术综述

> Comprehensive survey of branch prediction techniques: static, bimodal, two-level, gshare, tournament, perceptron, and TAGE. Includes accuracy data and design tradeoffs.

---

## Introduction / 引言

Branch prediction is the processor's attempt to guess the outcome of a branch instruction before it is resolved. Accurate prediction is critical because modern processors have pipeline depths of 15-30 stages, meaning a misprediction penalty can be 10-20+ wasted cycles. With ~20% of instructions being branches that occur every ~5 instructions, prediction accuracy directly impacts IPC (Instructions Per Cycle).

### The Cost of Misprediction

```
Pipeline depth = N
Branch frequency = 20%
Misprediction penalty = N cycles

Effective CPI = CPI_ideal + N × misprediction_rate × 0.20
```

For N=15, a 10% misprediction rate adds 0.3 CPI. At 95% accuracy, it's only 0.075 CPI.

---

## Static Prediction / 静态预测

### Always Not Taken

Predict every branch as not taken. Continue fetching PC+4.

- Accuracy: 30-40% (most branches are actually taken in typical code)
- Cost: 0 hardware bits
- Used in: simplest pipelines; fallback when no dynamic predictor

### Always Taken

Predict every branch as taken. Fetch from branch target immediately.

- Accuracy: 60-70% (because backward branches at loop ends are usually taken)
- Cost: Must compute branch target in IF stage

### BTFN (Backward Taken, Forward Not Taken)

| Direction | Prediction | Rationale |
|-----------|-----------|-----------|
| Backward (negative offset) | Taken | Loop back-edges |
| Forward (positive offset) | Not Taken | If-else style jumps |

- Accuracy: 65-80%
- Cost: Check sign bit of immediate

### Profile-Guided

Compiler profiles program execution and embeds "likely taken" hints in branch instructions.

- Accuracy: 75-85%
- Cost: Requires recompilation with profile data
- Used in: Itanium (predication), some ARM branches

---

## Dynamic Prediction / 动态预测

### 1-Bit Predictor

Stores the last outcome of each branch. Predicts the same as last time.

```
State machine:
  NOT TAKEN ---[T]---> TAKEN
      ^                 |
      +-----[NT]--------+
```

- Problem: Two mispredictions per loop (first and last iterations)
- For a loop executing 10 times: mispredicts entry AND exit = 2/10 = 80% accuracy
- For alternating pattern TNTNTN: 0% accuracy!

### 2-Bit Saturating Counter (Bimodal Predictor)

Adds hysteresis — requires two consecutive mispredictions to change prediction.

```
State machine:
  SN(00) --[T]--> WN(01) --[T]--> WT(10) --[T]--> ST(11)
    ^               |               |               |
    +-----[NT]------+-----[NT]------+-----[NT]------+
```

- For a loop executing 10 times: only mispredicts on exit = 1/10 = 90% accuracy
- For alternating pattern TNTNTN: still 0%, but can detect patterns
- Typical accuracy: 85-92%
- Hardware: 2 bits per branch × 256 entries = 64 bytes

### Implementation

```c
#define BHT_SIZE 256
uint8_t bht[BHT_SIZE];  // 2-bit per entry

bool bimodal_predict(uint32_t pc) {
    uint32_t idx = (pc >> 2) & (BHT_SIZE - 1);
    uint8_t counter = bht[idx];
    return (counter == WT || counter == ST);
}

void bimodal_update(uint32_t pc, bool taken) {
    uint32_t idx = (pc >> 2) & (BHT_SIZE - 1);
    if (taken && bht[idx] < ST) bht[idx]++;
    if (!taken && bht[idx] > SN) bht[idx]--;
}
```

---

## Two-Level Adaptive Predictors / 两级自适应预测

### Motivation

The behavior of a branch often correlates with the outcomes of other recent branches. For example:

```c
if (a == 2) a = 0;         // B1
if (b == 2) b = 0;         // B2
if (a != b) { /* ... */ }  // B3
```

B3 is perfectly predictable if we know B1 and B2. B3 is taken exactly once when B1 is taken XOR B2 is taken.

### Taxonomy (Yeh & Patt, 1991)

| Type | Branch History | Pattern History | Example |
|------|---------------|-----------------|---------|
| GAg | Global | Per-global | One set of 2-bit counters per GHR value |
| GAp | Global | Per-address | Separate PHT for each branch, indexed by GHR |
| PAg | Per-address | Per-global | BHT selects which GHR bits index the PHT |
| PAp | Per-address | Per-address | Full per-branch history and prediction |

### GAg Implementation

```c
uint8_t pht[PHT_SIZE];  // 64 entries × 2-bit
uint8_t ghr;             // 6-bit global history

bool gag_predict() {
    return pht[ghr] >= WT;
}

void gag_update(bool taken) {
    if (taken && pht[ghr] < ST) pht[ghr]++;
    if (!taken && pht[ghr] > SN) pht[ghr]--;
    ghr = ((ghr << 1) | (taken ? 1 : 0)) & 0x3F;
}
```

---

## Gshare Predictor / Gshare 预测器

Gshare XORs the branch PC with the GHR before indexing the PHT, reducing destructive aliasing compared to GAg.

```
  PC[2:9] -------+
                  +---> [XOR] ----> Index into PHT
  GHR[7:0] -------+
```

### Why XOR?

Without XOR, two branches with the same PC index always share PHT entries.
With XOR, different GHR values lead to different PHT indices for the same PC, allowing independent learning.

### Implementation

```c
uint32_t ghr;
uint8_t pht[PHT_ROWS];  // 256 entries

bool gshare_predict(uint32_t pc) {
    uint32_t hash = ((pc >> 2) ^ ghr) & (PHT_ROWS - 1);
    return pht[hash] >= WT;
}

void gshare_update(uint32_t pc, bool taken) {
    uint32_t hash = ((pc >> 2) ^ ghr) & (PHT_ROWS - 1);
    if (taken && pht[hash] < ST) pht[hash]++;
    if (!taken && pht[hash] > SN) pht[hash]--;
    ghr = ((ghr << 1) | (taken ? 1 : 0)) & GHR_MASK;
}
```

- Accuracy: 90-95%
- Hardware: 256 × 2-bit PHT + N-bit GHR

---

## Tournament Predictor / 锦标赛预测器

Combines bimodal and gshare (or other pair) using a meta-predictor (chooser table) that selects which predictor to trust for each branch.

```
  PC --> Bimodal ----+
                      +---> 2-bit Meta-Predictor ----> Final Prediction
  PC --> Gshare  -----+
```

### Meta-Predictor States

| State | Meaning |
|-------|---------|
| 00 | Strongly prefer bimodal |
| 01 | Weakly prefer bimodal |
| 10 | Weakly prefer gshare |
| 11 | Strongly prefer gshare |

### Update Rule

If both predictors agree, don't change the meta-predictor.
If they disagree, update the meta-predictor toward the correct one:

```c
void tournament_update(uint32_t pc, bool taken) {
    bool bimodal_pred = bimodal_predict(pc);
    bool gshare_pred  = gshare_predict(pc);
    bool final_pred   = meta_predict();

    if (bimodal_pred != gshare_pred) {
        if (bimodal_pred == taken) {
            if (meta_table[idx] > 0) meta_table[idx]--;
        } else {
            if (meta_table[idx] < 3) meta_table[idx]++;
        }
    }
}
```

- Accuracy: 92-96%
- Hardware: Bimodal + Gshare + 2-bit chooser per entry
- Used in: Alpha 21264, Intel Pentium Pro/II/III

---

## Perceptron Predictor / 感知器预测器

Uses a neural network (single-layer perceptron) with inputs from the GHR.

```
y = w0 + sum_{i=1}^{N} wi × xi

where:
  xi = +1 if GHR[i] = 1 (taken), -1 if GHR[i] = 0 (not taken)
  wi = weights (trained by perceptron learning rule)
  y >= 0 -> predict taken
  y < 0  -> predict not taken
```

### Training Algorithm

```c
void perceptron_update(bool taken) {
    int y = w0;
    for (int i = 0; i < N; i++) {
        y += weights[i] * (ghr_bit[i] ? 1 : -1);
    }

    bool predict = (y >= 0);

    // Train if mispredicted or not confident
    if (predict != taken || abs(y) < THRESHOLD) {
        int t = taken ? 1 : -1;
        w0 += t;
        for (int i = 0; i < N; i++) {
            weights[i] += t * (ghr_bit[i] ? 1 : -1);
        }
    }
}
```

- Accuracy: 93-96% (with ~256 weights)
- Advantage: Learns linearly separable branch patterns with long history
- Limitation: Cannot learn non-linear (XOR-like) patterns
- Complexity: O(N) multiply-add per prediction

---

## TAGE Predictor / TAGE 预测器

TAGE (TAgged GEometric history length) uses multiple predictor tables with geometric history lengths.

| Table | History Length | Size |
|-------|---------------|------|
| T0 (bimodal) | 0 (base) | 2K entries |
| T1 | 2 | 2K entries |
| T2 | 4 | 2K entries |
| T3 | 8 | 2K entries |
| T4 | 16 | 2K entries |
| T5 | 32 | 2K entries |
| T6 | 64 | 1K entries |
| T7 | 128 | 1K entries |
| T8 | 256 | 512 entries |

### How TAGE Works

1. Each entry has: 3-bit saturating counter + partial tag + useful bit
2. For a prediction, all tables are queried in parallel
3. The longest-history matching entry provides the prediction
4. On misprediction: allocate new entry in a longer-history table
5. Useful bits control allocation/replacement: entries that were helpful persist

### Key Innovation

Short-history tables learn quickly but have aliasing.
Long-history tables have less aliasing but require long training.
TAGE automatically selects the right history length per branch.

- Accuracy: 95-98% (Championship Branch Prediction winners since 2006)
- Hardware: 30-100 KB depending on configuration

---

## Accuracy Comparison / 准确率对比

Benchmark data (simulated with SPEC CPU 2006, 8K budget):

| Predictor | INT Avg | FP Avg | Mixed Avg | Hardware |
|-----------|---------|--------|-----------|----------|
| Always Taken | 62.3% | 71.4% | 66.9% | 0 bits |
| BTFN | 72.1% | 78.3% | 75.2% | 0 bits |
| Bimodal (4K) | 87.4% | 92.1% | 89.8% | 8 Kbits |
| GAg (4K) | 89.2% | 94.5% | 91.9% | 8 Kbits + GHR |
| Gshare (8K) | 91.7% | 96.2% | 94.0% | 16 Kbits + GHR |
| Tournament (12K) | 93.1% | 97.0% | 95.1% | 24 Kbits |
| Perceptron (1K weights) | 93.8% | 97.3% | 95.6% | ~16 KB |
| TAGE (32K) | 96.2% | 98.1% | 97.2% | ~32 KB |

---

## Tradeoffs / 设计权衡

| Dimension | Bimodal | Gshare | Tournament | Perceptron | TAGE |
|-----------|---------|--------|------------|------------|------|
| Accuracy | 85-92% | 90-95% | 92-96% | 93-96% | 95-98% |
| Hardware cost | Low | Medium | Medium-High | High | Very High |
| Training time | Fast | Fast | Medium | Slow | Medium |
| Power | Low | Low | Medium | High | High |
| Latency | 1 cycle | 1-2 cycles | 1-2 cycles | 2-3 cycles | 2-3 cycles |

---

## References / 参考

1. Yeh & Patt (1991). "Two-Level Adaptive Training Branch Prediction." MICRO-24.
2. McFarling (1993). "Combining Branch Predictors." WRL Technical Note TN-36.
3. Jimenez & Lin (2001). "Dynamic Branch Prediction with Perceptrons." HPCA-7.
4. Seznec & Michaud (2006). "A Case for (Partially) Tagged Geometric History Length Branch Prediction." JILP.
5. Mittal (2019). "A Survey of Techniques for Dynamic Branch Prediction."
6. Championship Branch Prediction (CBP) proceedings: https://www.jilp.org/cbp/
