# mini-coherence-protocol — 缓存一致性协议

## 概述 (Overview)

在多核处理器中，每个核心都有自己的私有缓存，这带来了缓存一致性问题：当一个核心修改了某个缓存行的数据后，其他核心缓存中的该行副本就变成了过期数据。缓存一致性协议通过一套规则来保证所有核心看到的内存视图是一致的。

Cache coherence protocols ensure that all processors in a multiprocessor system observe a consistent view of memory. Without coherence, stale data in private caches can lead to incorrect program execution.

## 理论基础 (Theory)

### 一致性与连贯性

| 概念 | 定义 | 关注点 |
|------|------|--------|
| 一致性 (Coherence) | 单个内存位置的读写顺序 | 一个地址 |
| 连贯性 (Consistency) | 多个内存位置的操作顺序 | 全局视图 |

### 一致性不变量

1. **单写者多读者 (SWMR)**: 对于任何内存位置，在任意给定的时间内，要么有一个写者，要么有多个读者，但不能同时存在
2. **数据值传播 (Data-Value)**: 一个内存位置的值由其最近的写入操作决定

### 缓存状态的定义

缓存一致性协议为每个缓存行关联一个状态。以下是各协议的状态定义：

| 状态 | MSI | MESI | MOESI | 含义 |
|------|-----|------|-------|------|
| M (Modified) | ✓ | ✓ | ✓ | 数据已被修改，只有此缓存有有效副本，内存中的数据已过时 |
| E (Exclusive) | — | ✓ | ✓ | 数据未被修改，只有此缓存有有效副本，与内存一致 |
| O (Owned) | — | — | ✓ | 数据可能被修改，此缓存和内存可能不一致，但其他缓存可以有共享副本 |
| S (Shared) | ✓ | ✓ | ✓ | 数据可能被多个缓存持有，与内存一致（或由 O 状态缓存提供） |
| I (Invalid) | ✓ | ✓ | ✓ | 缓存行无效，数据不可用 |

### MSI 协议 (基本协议)

MSI 是最简单的基本一致性协议，只有三个状态。

```
         ┌─ BusRdX (write miss) ────┐
         │                          │
         ▼                          │
    ┌─────────┐  BusRdX (snoop)  ┌──┴──────┐
    │    I    │◄─────────────────│    M    │
    └─────────┘                  └──┬──────┘
         │                          │
         │  BusRd (read miss)       │ BusRd (snoop)
         ▼                          ▼
    ┌─────────┐  BusRd (snoop)   ┌─────────┐
    │    S    │◄─────────────────│    S    │
    └─────────┘                  └─────────┘
         │                          ▲
         │  BusRdX (write hit)     │
         └──────────────────────────┘
```

**MSI 状态转换 (处理器侧)**:

| 事件 | I | S | M |
|------|---|---|---|
| PrRd (处理器读) | BusRd → S | 不变 (S) | 不变 (M) |
| PrWr (处理器写) | BusRdX → M | BusRdX → M | 不变 (M) |

**MSI 状态转换 (总线监听侧)**:

| 总线事务 | I | S | M |
|----------|---|---|---|
| BusRd | 不变 | 不变 | → S (写回) |
| BusRdX | 不变 | → I | → I (写回) |

### MESI 协议 (增加 Exclusive 状态)

MESI 在 MSI 基础上增加了 Exclusive (E) 状态，优化的关键场景是：一个处理器先读后写同一行。

关键改进：从 E 状态可以直接升级到 M 状态而不需要总线事务，因为 E 状态保证了没有其他缓存持有该行。

**MESI 与 MSI 的差异**:

| 场景 | MSI 行为 | MESI 行为 |
|------|----------|-----------|
| 处理器首次读取 (无其他副本) | I → S (通过 BusRd) | I → E (通过 BusRd) |
| 处理器写入 E 状态的行 | — | E → M (静默，无需总线) |
| 处理器写入 S 状态的行 | S → M (BusRdX) | S → M (BusRdX) |

**MESI 状态机**:

```
                    BusRdX
              ┌── I ◄─────────┐
              │   │            │
     BusRd    │   │ BusRd      │ BusRdX
     (no      │   │ (others    │
     sharers) │   │  exist)    │
              ▼   ▼            │
         ┌──────┐ ┌──────┐    │
    ┌───►│  E   │ │  S   │◄───┘
    │    └──┬───┘ └──┬───┘
    │       │        │
    │  PrWr │   PrWr │ BusRdX
    │(silent)│(BusRdX)│
    │       ▼        ▼
    │    ┌──────────────┐
    └────│      M       │
         └──────┬───────┘
                │ BusRd (snoop)
                ▼
            ┌──────┐
            │  S   │
            └──────┘
```

### MOESI 协议 (增加 Owned 状态)

MOESI 在 MESI 基础上增加了 Owned (O) 状态，用于 AMD 的 HyperTransport 和 ARM 的 AMBA 总线。

**Owned 状态的动机**:

当处于 M 状态的处理器收到 BusRd 时：
- MESI: M → S → I（需要写回内存，然后转为 S 或 I）
- MOESI: M → O（不写回内存，但允许其他处理器有共享副本）

**MOESI 的完整状态转换**:

| 当前\事件 | PrRd | PrWr | BusRd | BusRdX |
|-----------|------|------|-------|--------|
| I | BusRd | BusRdX | — | — |
| S | — | BusRdX | 不变 | → I |
| E | — | → M | → S | → I |
| M | — | — | → O | → I |
| O | — | BusRdX | 不变 | → I |

### 一致性实现的两种范式

#### 监听总线 (Snooping Bus)

```
         ┌───────┐    ┌───────┐    ┌───────┐    ┌───────┐
         │ Core0 │    │ Core1 │    │ Core2 │    │ Core3 │
         │ L1 $  │    │ L1 $  │    │ L1 $  │    │ L1 $  │
         └───┬───┘    └───┬───┘    └───┬───┘    └───┬───┘
             │            │            │            │
             └────────────┼────────────┼────────────┘
                          │            │
                    ┌─────┴────────────┴─────┐
                    │     Shared Bus         │
                    └───────────┬────────────┘
                                │
                          ┌─────┴─────┐
                          │   Memory  │
                          └───────────┘
```

- **优点**: 低延迟、实现简单（2–8核）
- **缺点**: 总线带宽有限，不扩展
- **适用**: 小规模多核（2–8 核）

#### 目录 (Directory-Based)

```
    ┌───────┐         ┌───────┐         ┌───────┐
    │ Core0 │         │ Core1 │         │ Core2 │
    │ L1 $  │         │ L1 $  │         │ L1 $  │
    └───┬───┘         └───┬───┘         └───┬───┘
        │   Req/Grant     │   Req/Grant     │
        └────────┬────────┼────────┬────────┘
                 │        │        │
           ┌─────┴────────┴────────┴─────┐
           │     Directory Controller    │
           │   + Interconnect Router     │
           └─────────────┬───────────────┘
                         │
                   ┌─────┴─────┐
                   │   Memory  │
                   └───────────┘
```

- **优点**: 高扩展性（可扩展到数百核）
- **缺点**: 目录存储开销，间接延迟
- **目录组织**: 全映射 (Full-map)、有限指针 (Limited-pointer)、粗粒度 (Coarse)

### 目录结构

```c
typedef struct {
    uint32_t tag;
    CoherenceState state;
    uint8_t sharer_bits[MAX_CACHES/8];  // 位向量
    uint32_t owner_id;                    // 拥有者
} DirectoryEntry;
```

目录条目为每个缓存行维护：
- **状态**: 该行的全局一致性状态
- **共享者列表**: 哪些缓存持有该行的副本
- **拥有者**: 哪个缓存在 M/O 状态下持有该行

### 伪共享问题 (False Sharing)

**定义**: 两个核心各自修改不同变量，但这些变量恰好位于同一个缓存行中，导致不必要的缓存行无效化。

```
缓存行 (64 字节):
┌───────────────┬───────────────┬─────────────────────────────┐
│  Core0 的变量  │  Core1 的变量  │        未使用               │
│  (4 bytes)     │  (4 bytes)     │      (56 bytes)             │
└───────────────┴───────────────┴─────────────────────────────┘
```

- Core 0 写入 → Core 1 的缓存行被无效化
- Core 1 写入 → Core 0 的缓存行被无效化
- 导致"乒乓效应"

**解决方案**:
1. 数据结构填充：在变量间添加 `char padding[64]`
2. 线程局部存储
3. 编译器 `__attribute__((aligned(64)))`

### 协议比较

| 特性 | MSI | MESI | MOESI |
|------|-----|------|-------|
| 状态数 | 3 | 4 | 5 |
| 静默 E→M 升级 | ❌ | ✅ | ✅ |
| 脏共享 (Dirty Sharing) | ❌ | ❌ | ✅ |
| 实现复杂度 | 低 | 中 | 高 |
| 总线事务 (典型) | 较多 | 较少 | 最少 |
| 使用场景 | 教学 | Intel x86 | AMD, ARM |

## 实现细节 (Implementation)

### 一致性控制器

```c
typedef struct {
    CoherenceProtocol protocol;
    CoherenceCache caches[MAX_CACHES];    // 每个核的私有缓存
    DirectoryEntry directory[DIR_ENTRIES]; // 目录（目录协议）
    uint64_t bus_transactions;             // 总线事务计数
    uint64_t invalidations;                // 无效化计数
    uint64_t writebacks;                   // 写回计数
} CoherenceController;
```

### 读操作流程 (MESI)

```
cache_read(cache_id, address):
    1. 查找本地缓存
    2. 命中 → 返回数据
    3. 未命中:
       a. 检查其他缓存的副本:
          - 如果其他缓存有 M 状态 → 写回 + 转换为 S，新缓存为 S
          - 如果其他缓存有 E/S 状态 → 转换为 S，新缓存为 S
          - 如果没有其他副本 → 新缓存为 E
       b. 更新目录
```

### 写操作流程 (MESI)

```
cache_write(cache_id, address, data):
    1. 查找本地缓存
    2. 命中且 (M 或 E):
       - 写入数据，状态升级为 M
       - 无需总线事务（静默写入）
    3. 命中且 S:
       - 需要 BusRdX（升级到 M）
       - 无效化其他所有缓存
    4. 未命中:
       - BusRdX → 状态为 M
       - 无效化其他所有缓存
```

## 代码示例 (Code)

```c
CoherenceController ctrl;
coherence_init(&ctrl, PROTO_MESI, 2);

// Core 0 reads address 0x1000
coherence_read(&ctrl, 0, 0x1000, buf);
// Core 0: E (Exclusive)

// Core 1 reads address 0x1000
coherence_read(&ctrl, 1, 0x1000, buf);
// Core 0: S (Shared), Core 1: S (Shared)

// Core 0 writes address 0x1000
coherence_write(&ctrl, 0, 0x1000, data);
// Core 0: M (Modified), Core 1: I (Invalidated)
```

## 输出示例 (Output)

完整的状态转换在 `examples/coherence_demo.c` 中展示。

## 实验与练习 (Exercises)

1. **协议比较**: 在同一访问序列上比较 MSI 和 MESI 的总线事务数量
2. **伪共享检测**: 模拟两个核心访问同一缓存行的不同变量
3. **目录 vs 监听**: 对于 2/4/8/16 核的比较
4. **写回 vs 写直达**: 在一致性上下文中比较两种写策略
5. **状态机验证**: 手动绘制完整的 MESI 状态转换图
6. **扩展 MOESI**: 在 MESI 实现基础上添加 O 状态支持

## 参考资料 (References)

- Sorin, Hill, Wood, "A Primer on Memory Consistency and Cache Coherence"
- MIT 6.823, Lecture 14–16: Cache Coherence
- CMU 18-447, Lecture 18–21: Snooping and Directory Protocols
- AMD64 Architecture Programmer's Manual, Volume 2: MOESI Protocol
- Intel 64 and IA-32 Architectures Software Developer's Manual: MESI Protocol
