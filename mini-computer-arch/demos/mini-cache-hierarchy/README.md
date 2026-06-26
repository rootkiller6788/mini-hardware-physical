# mini-cache-hierarchy — 缓存层次结构设计与仿真

## 概述 (Overview)

现代计算机系统采用多级存储器层次结构来弥合处理器与主存之间的速度差距。本模块深入分析缓存设计的关键原理，包括映射方式、替换策略和写策略，并通过 C 语言实现完整的缓存仿真器。

The memory hierarchy is the fundamental design principle that makes modern computing fast and efficient. By exploiting spatial and temporal locality, small but fast caches can capture a large fraction of memory accesses, dramatically reducing average memory access time (AMAT).

## 理论基础 (Theory)

### 存储器层次结构的动机

处理器与主存的性能差距（Processor-Memory Gap）是计算机体系结构的核心挑战之一：
- 处理器周期：约 0.3–0.5 ns（以 2–3 GHz 时钟频率计）
- DRAM 访问：约 50–100 ns
- 差距：100–300 倍

这种差距意味着如果每次内存访问都需要等待 DRAM，处理器将在大多数时间处于空闲状态。缓存通过在处理器附近存储近期使用的数据来缓解这一问题。

### 局部性原理 (Principle of Locality)

| 类型 | 描述 | 示例 |
|------|------|------|
| 时间局部性 (Temporal) | 最近访问的数据可能再次被访问 | 循环变量、栈顶 |
| 空间局部性 (Spatial) | 地址附近的数据可能随后被访问 | 数组遍历、指令序列 |

### 平均存储访问时间 (AMAT)

AMAT = Hit Time + Miss Rate × Miss Penalty

其中：
- **Hit Time**: 缓存命中的访问时间（通常 1–2 个周期）
- **Miss Rate**: 缓存未命中的比例
- **Miss Penalty**: 从下一级存储器取数据的时间

### 缓存组织 (Cache Organization)

#### 地址分解

32 位地址被分解为三个字段：

```
+-------------------+------------------+------------+
|       Tag         |      Index       |   Offset   |
+-------------------+------------------+------------+
    t bits               s bits            b bits

Cache Size = 2^s × (Associativity) × (2^b bytes per line)
Total Address = t + s + b = 32 bits
```

- **Offset (b bits)**: 选择缓存行内的字节
- **Index (s bits)**: 选择缓存组
- **Tag (t bits)**: 与缓存行的标签进行比较，确定命中/未命中

#### 地址映射方式

| 映射方式 | 组数 | 每组路数 | 优点 | 缺点 |
|----------|------|----------|------|------|
| 直接映射 (Direct-Mapped) | N | 1 | 简单、快速 | 冲突未命中多 |
| 全相联 (Fully-Associative) | 1 | N | 灵活性最高 | 硬件开销大 |
| 组相联 (Set-Associative) | N/k | k | 平衡性能与成本 | 中等复杂度 |

其中 N 是总缓存行数，k 是相联度。

### 替换策略 (Replacement Policies)

当缓存组已满且发生缓存未命中时，需要选择一个缓存行进行替换。

#### LRU (Least Recently Used)

- **原理**: 替换最长时间未被访问的缓存行
- **实现**: 每个缓存行维护一个计数器，每次访问时更新
- **优点**: 较好地利用了时间局部性
- **缺点**: 硬件开销较大，需要跟踪访问顺序
- **复杂度**: O(k) 查找，其中 k 是相联度

#### FIFO (First-In-First-Out)

- **原理**: 替换最早进入缓存组的缓存行
- **实现**: 维护一个全局计数器，循环替换
- **优点**: 实现简单，硬件开销小
- **缺点**: 可能替换掉频繁使用的数据
- **与 LRU 的区别**: FIFO 基于加载时间，LRU 基于最后访问时间

#### LFU (Least Frequently Used)

- **原理**: 替换访问次数最少的缓存行
- **实现**: 每个缓存行维护一个访问计数器
- **优点**: 保留频繁访问的数据
- **缺点**: 可能保留不再使用的"旧热点"数据
- **改进**: 定期衰减计数器（如 LFRU）

#### 随机 (Random)

- **原理**: 随机选择一个缓存行进行替换
- **优点**: 实现最低开销，无异常行为
- **性能**: 在实践中接近 LRU，特别是对于高相联度缓存

### 写策略 (Write Policies)

#### 写直达 (Write-Through)

- 写入时同时更新缓存和下一级存储器
- 优点：一致性简单，不需要脏位
- 缺点：写入流量大，带宽需求高
- 缓冲：通常配合写缓冲 (Write Buffer) 使用

#### 写回 (Write-Back)

- 写入时仅更新缓存，标记为脏 (Dirty)
- 优点：减少写入流量
- 缺点：一致性复杂，需要处理脏数据写回
- 缺点：缓存未命中时可能需要两次访存（写回脏数据 + 读取新数据）

#### 写分配与写不分配

| 策略 | 写命中 | 写未命中 |
|------|--------|----------|
| 写分配 (Write-Allocate) | 更新缓存 | 先加载到缓存，再写入 |
| 写不分配 (No-Write-Allocate) | 更新缓存 | 直接写入下一级，不加载到缓存 |

通常：写回 + 写分配，写直达 + 写不分配。

### 缓存性能分类 (Three C's of Cache Misses)

| 类型 | 描述 | 解决方案 |
|------|------|----------|
| 强制未命中 (Compulsory) | 首次访问数据 | 预取 (Prefetching) |
| 容量未命中 (Capacity) | 缓存不足以容纳工作集 | 增大缓存 |
| 冲突未命中 (Conflict) | 多地址映射到同一组 | 增加相联度 |

## 实现细节 (Implementation)

### 数据结构

```c
typedef struct {
    bool valid;          // 有效位
    bool dirty;          // 脏位 (写回模式)
    uint32_t tag;        // 标签
    uint8_t data[64];    // 数据块 (64 字节)
    uint64_t lru_counter; // LRU 计数器
    uint64_t access_count; // 访问计数 (LFU)
} CacheLine;

typedef struct {
    Cache cache;         // 缓存配置和状态
    uint64_t stats;      // 统计：命中、未命中、读、写
} CacheSimulator;
```

### 地址解析

对于 32KB、64B 行、8 路组相联缓存：
- 总行数 = 32768 / 64 = 512 行
- 组数 = 512 / 8 = 64 组
- Offset bits = log2(64) = 6 位
- Index bits = log2(64) = 6 位
- Tag bits = 32 - 6 - 6 = 20 位

### 查找流程

```
1. 分解地址 → (Tag, Index, Offset)
2. 选择组 Set[Index]
3. 遍历组内所有路的标签
4. 匹配成功 → 命中 (更新 LRU)
5. 匹配失败 → 未命中 (执行替换)
```

### 替换流程

```
1. 在组内查找无效行 → 直接使用
2. 所有行有效 → 根据策略选择牺牲行
3. 如果牺牲行是脏的 (写回模式) → 写回
4. 用新的标签和数据填充
5. 更新访问计数和状态
```

## 代码示例 (Code)

完整实现在 `examples/cache_sim_demo.c`，创建 32KB L1 缓存并运行内存访问轨迹。

```c
// 初始化 32KB L1 缓存
Cache l1;
cache_init(&l1, 32768, 64, 8, LRU, WRITE_BACK);

// 运行访问轨迹
uint32_t trace[] = {0x1000, 0x1040, 0x1080, 0x1000, ...};
for (int i = 0; i < TRACE_SIZE; i++) {
    bool hit = cache_read(&l1, trace[i], buffer);
    printf("[%d] Address 0x%X: %s\n", i, trace[i], hit ? "HIT" : "MISS");
}

// 打印统计
cache_print_stats(&l1);
```

## 输出示例 (Output)

```
========================================
  Cache Simulator Demo
  L1 Cache: 32KB, 64B lines, 8-way LRU
========================================

Running memory access trace (32 accesses)...

  [ 1] Addr=0x00001000  Tag=0x0000  Set=  0  Off= 0  -> MISS
  [ 2] Addr=0x00001040  Tag=0x0000  Set=  1  Off= 0  -> MISS
  [ 3] Addr=0x00001080  Tag=0x0000  Set=  2  Off= 0  -> MISS
  [ 4] Addr=0x00001000  Tag=0x0000  Set=  0  Off= 0  -> HIT

========================================
  Cache Statistics
========================================
Configuration:
  Total Size:     32768 bytes
  Line Size:      64 bytes
  Sets:           64
  Associativity:  8-way
  Replacement:    LRU
  Write Policy:   Write-Back
----------------------------------------
Results:
  Total Accesses: 32
  Hits:           12
  Misses:         20
  Evictions:      0
  Writebacks:     0
  Hit Rate:       37.50%
  Miss Rate:      62.50%
========================================
```

## 实验与练习 (Exercises)

1. **改变相联度**: 将 8 路改为 4 路和 16 路，观察命中率变化
2. **不同替换策略**: 对比 LRU、FIFO、LFU、Random 的性能差异
3. **访问模式影响**: 测试顺序访问、步长访问、随机访问的命中率
4. **缓存大小扫描**: 测试 8KB → 256KB 不同大小对命中率的影响
5. **写策略对比**: 模拟写回和写直达的写回次数
6. **AMAT 分析**: 计算 AMAT 并与理想命中率对比

## 参考资料 (References)

- Hennessy & Patterson, "Computer Architecture: A Quantitative Approach", Chapter 2
- MIT 6.823, Lecture 11–12: Memory Hierarchy and Caches
- CMU 18-447, Lecture 10–13: Cache Design
- Ulrich Drepper, "What Every Programmer Should Know About Memory"
