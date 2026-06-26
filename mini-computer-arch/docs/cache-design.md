# Cache Design Principles — 缓存设计原理

## 1. 存储器层次结构的动机 (Motivation)

### 处理器—内存性能差距

```
速度对比 (ns):
┌────────────────────────────────────────────────────────┐
│ 寄存器      ████ 0.3 ns                                │
│ L1 Cache    ██████ 1 ns                                │
│ L2 Cache    ██████████ 4 ns                            │
│ L3 Cache    ██████████████████ 12 ns                   │
│ DRAM        ██████████████████████████████████ 50 ns   │
│ SSD         ████████████████████████████████████████   │
│                           ████████████████████████████  │
│             0    10    20    30    40    50    100000    │
└────────────────────────────────────────────────────────┘
```

性能差距意味着：
- 处理器每 0.3 ns 可以执行一条指令
- DRAM 访问需要 50–100 ns（相当于 150–300 条指令）
- 直观比喻：L1 Cache 是从口袋里取东西（1 秒），DRAM 是走到图书馆（5 分钟）

### 关键问题

**为什么不在 CPU 旁边放大量 SRAM？**

答案：成本、功耗、面积。

| 存储器类型 | 每字节成本 (相对) | 访问延迟 | 典型大小 |
|------------|-------------------|----------|----------|
| SRAM (L1) | 1000× | 1–2 ns | 32–64 KB |
| SRAM (L2) | 200× | 4–8 ns | 256–512 KB |
| SRAM (L3) | 60× | 12–20 ns | 4–16 MB |
| DRAM | 1× | 50–100 ns | 8–64 GB |
| SSD/NAND | 0.5× | 10–100 μs | 256 GB–2 TB |
| HDD | 0.05× | 5–10 ms | 2–20 TB |

## 2. 局部性原理 (Principle of Locality)

### 时间局部性 (Temporal Locality)

如果某个地址被访问，那么它很可能在不久的将来再次被访问。

**常见原因**:
- 循环变量：`for (i = 0; i < n; i++)` 中的 `i` 每次迭代都访问
- 栈操作：函数调用频繁访问栈顶
- 计数器：频繁递增的统计变量
- 指针间接引用：`node = node->next`

**缓存策略利用时间局部性**:
- LRU/LFU 替换策略保留最近使用/频繁使用的数据
- 缓存行在访问后保持有效

### 空间局部性 (Spatial Locality)

如果某个地址被访问，那么附近地址的数据很可能在不久的将来被访问。

**常见原因**:
- 数组遍历：`for (i = 0; i < n; i++) sum += a[i]`
- 指令执行：程序通常顺序执行指令
- 结构体访问：`struct { int a; int b; }` 成员连续存储
- 矩阵运算：行优先或列优先遍历

**缓存策略利用空间局部性**:
- 缓存行大小 > 1 字（64 字节）
- 缓存行加载包含相邻数据
- 预取 (Prefetching) 主动加载接下来的缓存行

### 定量分析

```
访问模式分析 (64B 缓存行，4 字节字):

顺序访问:
  a[0], a[1], a[2], ..., a[15]
  16 次访问，1 次未命中 (强制)
  命中率: 15/16 = 93.75%

步长访问 (步长 = 4 个字 = 16 字节):
  a[0], a[4], a[8], a[12]
  4 次访问，1 次未命中
  命中率: 3/4 = 75%

步长 = 16 个字 = 64 字节:
  a[0], a[16], a[32]
  3 次访问，3 次未命中
  命中率: 0%
```

## 3. 缓存组织 (Cache Organization)

### 三种映射方式

#### 直接映射 (Direct-Mapped)

```
地址: [ Tag (20 bits) | Index (10 bits) | Offset (2 bits) ]
                    ↓
        ┌──────────────────────┐
Set 0:  │ Tag | Data (4 bytes) │
Set 1:  │ Tag | Data           │
 ...    │ ...                  │
Set 1023│ Tag | Data           │
        └──────────────────────┘

每个地址只能映射到一个特定的缓存行: Set = (Address >> 2) & 0x3FF
```

**速度**: 最快（一次比较即可确定命中）
**冲突**: 最多（如果两个地址映射到同一 Set）
**面积**: 最小
**典型使用**: L1 数据缓存（Intel Skylake: 32KB, 8-way, 但早期处理器常用 DM）

#### 全相联 (Fully-Associative)

```
地址: [ Tag (30 bits) | Offset (2 bits) ]
                │
                ├──── 比较器 0 ──── Tag 0 ──[命中?] 
                ├──── 比较器 1 ──── Tag 1 ──[命中?]
                ├──── ... 
                └──── 比较器 N-1 ── Tag N-1 [命中?]

任何地址可以映射到任意缓存行
```

**速度**: 最慢（需要 N 路并行比较）
**冲突**: 无
**面积**: 最大（N 个比较器）
**典型使用**: 小型 TLB (Translation Lookaside Buffer)

#### 组相联 (Set-Associative)

```
地址: [ Tag (22 bits) | Index (8 bits) | Offset (2 bits) ]
                    ↓
        ┌──────────────────────────────────┐
Set 0:  │ Way0 [Tag|Data] │ Way1 [Tag|Data] ... │ Way7 │
Set 1:  │ Way0 [Tag|Data] │ Way1 [Tag|Data] ... │ Way7 │
 ...    │ ...                                 ... │ ...  │
Set 255 │ Way0 [Tag|Data] │ Way1 [Tag|Data] ... │ Way7 │
        └──────────────────────────────────┘
```

这是直接映射和全相联的折中方案，是当前处理器的主要选择。

### 缓存参数计算

以 32KB 缓存、64B 行、8 路组相联为例：

```
总行数 = 32768 / 64 = 512
组数 = 512 / 8 = 64
Offset bits = log2(64) = 6
Index bits = log2(64) = 6
Tag bits = 32 - 6 - 6 = 20

每组的存储开销（不计 Tag）:
  8 × 64B = 512B data
  8 × 20bits = 160b tag
  8 × 1bit = 8b valid
  8 × 1bit = 8b dirty (write-back)
  8 × 16bits = 128b LRU counter
  Total ≈ 512B + 38B = 550B/set
  开销比率 ≈ 7.4%
```

### Tag/Index/Offset 分解代码

```c
void cache_decompose_address(Cache *cache, uint32_t address,
                             uint32_t *tag, uint32_t *index, uint32_t *offset) {
    uint32_t offset_bits = log2(cache->line_size);
    uint32_t index_bits  = log2(cache->num_sets);

    *offset = address & ((1u << offset_bits) - 1);
    *index  = (address >> offset_bits) & ((1u << index_bits) - 1);
    *tag    = address >> (offset_bits + index_bits);
}
```

## 4. 替换策略比较

### 理论分析

对于 k 路组相联缓存，不同替换策略在最坏情况下的竞争力：

| 策略 | 竞争比 (vs. OPT) | 实现复杂度 | 命中率 (典型) |
|------|------------------|------------|---------------|
| LRU | k (最优离线) | O(k) 搜索 | 最佳 |
| LFU | 无界 | O(k) 搜索 | 接近 LRU |
| FIFO | k | O(1) | 可变 |
| Random | 约 k/(ln k) | O(1) | 对 k≥8 接近 LRU |

**注**: 竞争比是 online 算法与最佳 offline 算法 (OPT) 相比的最大性能损失倍数。

### 实际缓存中的选择

| 缓存级别 | 常见替换策略 | 原因 |
|----------|--------------|------|
| L1 缓存 | LRU 或伪 LRU | 低相联度 (4–8 way)，精确 LRU 可负担 |
| L2 缓存 | 伪 LRU 或 RRIP | 相联度 8–16 way，需要更好预测 |
| L3 缓存 | 自适应替换 | 大相联度，工作负载多样 |
| 虚拟内存 | Clock (NRU 近似) | 软硬件接口，开销低 |

## 5. 写策略深入

### 写直达 (Write-Through)

```
处理器写操作:
  ┌─────────┐    数据     ┌─────────┐
  │  Cache  │◄───────────│   CPU    │
  └────┬────┘             └─────────┘
       │ 数据 (同时写入)
       ▼
  ┌─────────┐
  │  Memory │
  └─────────┘

优势:
  ✓ 内存始终是最新的
  ✓ 缓存可以轻易丢弃（无脏数据）
  ✓ 一致性更简单

缺陷:
  ✗ 高写入带宽
  ✗ 每条写都需要总线和内存

改进: 写缓冲 (Write Buffer)
  CPU ──► [Write Buffer] ──► Memory
  处理器写入后立即继续，写缓冲异步写回内存
```

### 写回 (Write-Back)

```
处理器写操作:
  ┌─────────┐    数据     ┌─────────┐
  │  Cache  │◄───────────│   CPU    │
  │ (Dirty) │             └─────────┘
  └────┬────┘
       │ 只在替换时写入
       ▼
  ┌─────────┐
  │  Memory │  ← 可能过时
  └─────────┘

优势:
  ✓ 减少内存流量
  ✓ 一次替换可以合并多次写入

缺陷:
  ✗ 内存可能过时
  ✗ 一致性更复杂
  ✗ 替换时需要额外的写回延迟
```

### 写未命中策略

| 策略 | 写直达 | 写回 |
|------|--------|------|
| 写分配 (Write-Allocate) | 不自然（为什么要加载？） | ✅ 标准组合 |
| 写不分配 (No-Write-Allocate) | ✅ 标准组合 | 不自然 |

## 6. AMAT 模型

### 单级缓存

```
AMAT = Hit Time + Miss Rate × Miss Penalty

例如:
  Hit Time = 1 ns
  Miss Rate = 10%
  Miss Penalty = 100 ns
  AMAT = 1 + 0.1 × 100 = 11 ns
```

### 多级缓存

```
AMAT = Hit Time_L1 + Miss Rate_L1 × 
       (Hit Time_L2 + Miss Rate_L2 × 
        (Hit Time_L3 + Miss Rate_L3 × Miss Penalty_DRAM))

例如:
  L1: 1ns hit, 5% miss
  L2: 5ns hit, 20% miss (of L1 misses)
  L3: 15ns hit, 30% miss (of remaining misses)
  DRAM: 60ns miss penalty

  L1 misses:  5% × 1000 accesses = 50
  L2 hits:    50 × 0.8 = 40
  L2 misses:  50 × 0.2 = 10
  L3 hits:    10 × 0.7 = 7
  L3 misses:  10 × 0.3 = 3

  AMAT = 1 + 0.05 × (5 + 0.2 × (15 + 0.3 × 60))
       = 1 + 0.05 × (5 + 0.2 × 33)
       = 1 + 0.05 × (5 + 6.6)
       = 1 + 0.05 × 11.6
       = 1 + 0.58
       = 1.58 ns
```

### 命中率敏感度

```
AMAT 随 Miss Rate 的变化:
  MR = 1%:  AMAT = 1 + 0.01 × 100 = 2.0 ns
  MR = 5%:  AMAT = 1 + 0.05 × 100 = 6.0 ns
  MR = 10%: AMAT = 1 + 0.10 × 100 = 11.0 ns
  MR = 50%: AMAT = 1 + 0.50 × 100 = 51.0 ns

关键洞察: 将 miss rate 从 5% 降低到 1% 的收益 (AMAT -4 ns)
           比从 50% 降低到 10% 的收益 (AMAT -40 ns) 小得多。
           但在实践中，低 miss rate 的优化更难实现。
```

## 7. 缓存设计空间探索

| 参数 | 变化方向 | 效果 |
|------|----------|------|
| 缓存大小 ↑ | 增大 | 减少容量未命中，增加访问时间 |
| 块大小 ↑ | 增大 | 减少强制未命中（空间局部性），增加未命中惩罚 |
| 相联度 ↑ | 增大 | 减少冲突未命中，增加访问时间 |
| 替换策略 | LRU → FIFO | 可能增加冲突未命中 |

## 8. 参考资料

- Hennessy & Patterson, "Computer Architecture: A Quantitative Approach", 6th Edition, Chapter 2
- Hill & Smith, "Evaluating Associativity in CPU Caches" (IEEE TC 1989)
- Qureshi et al., "Adaptive Insertion Policies for High Performance Caching" (ISCA 2007)
- Jaleel et al., "High Performance Cache Replacement Using Re-Reference Interval Prediction" (ISCA 2010)
- MIT 6.823 Lecture Notes, Lectures 11–13
