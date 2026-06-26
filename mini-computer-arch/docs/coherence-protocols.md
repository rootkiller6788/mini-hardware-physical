# Cache Coherence Protocols — 缓存一致性协议详解

## 1. 为什么需要缓存一致性？

### 问题场景

```
考虑两个核心共享变量 x (初始值 = 0):

Core 0:  x = x + 1;    // 读 x，加 1，写回
Core 1:  x = x + 1;    // 读 x，加 1，写回

期望结果: x = 2

无一致性的执行:
  T0: Core 0 读取 x=0 到私有缓存 (Cache0: x=0)
  T1: Core 1 读取 x=0 到私有缓存 (Cache1: x=0)
  T2: Core 0 写入 x=1 到私有缓存 (Cache0: x=1)
  T3: Core 1 写入 x=1 到私有缓存 (Cache1: x=1)
  T4: Cache0 写回内存 → x=1
  T5: Cache1 写回内存 → x=1

实际结果: x = 1  ← 错误！
```

**核心问题**: 缓存和内存可以有同一个地址的多个不同值的副本。

## 2. 三种经典协议

### 2.1 MSI 协议 (Modified, Shared, Invalid)

MSI 是最简单的一致性协议。

#### 状态定义

| 状态 | 含义 | 缓存行有效？ | 内存最新？ | 其他缓存有？ |
|------|------|-------------|-----------|-------------|
| **M**odified | 该行已被修改 | 是 | 否（过时） | 否 |
| **S**hared | 该行可被多个缓存持有 | 是 | 是 | 可能 |
| **I**nvalid | 该行无效 | 否 | — | — |

#### 状态转换

```
                    +-----------+
                    |           |
                    |    I      | Invalid
                    |           |
                    +-----------+
                     /    |    \
                    /     |     \
           PrRd/    |  PrWr/    | BusRdX/
           BusRd    |  BusRdX   | (snoop)
           →S       |  →M       | →I
                    /      |      \
                   v       v       v
              +--------+     +--------+
              |        |     |        |
              |   S    |     |   M    | Modified
              |        |     |        |
              +--------+     +--------+
                  |    \      |    \
           BusRdX/     \     |     \ BusRd/
           (snoop)      \    |      \ (snoop)
           →I            \   |       \ →S
           PrRd/          \  |        \
           →S              \ |         \
                            v|          v
                          +--------+
                          |   I    |
                          +--------+
```

#### 总线事务

| 事务 | 含义 | 触发场景 |
|------|------|----------|
| **BusRd** | 读取请求 | 读未命中 |
| **BusRdX** | 独占读取（写意图） | 写未命中 |

#### MSI 的缺点

1. **不必要的总线事务**: 处理器先读后写同一行时，即使该行没有被其他缓存持有（从 I → S 读，然后 S → M 写需要 BusRdX）
2. **无静默升级**: 从 S 到 M 总是需要总线事务

### 2.2 MESI 协议 (Modified, Exclusive, Shared, Invalid)

MESI 增加 Exclusive (E) 状态来解决上述问题。

#### 新增 E 状态

| 状态 | 含义 |
|------|------|
| **E**xclusive | 该行仅在本缓存中，未被修改，内存是最新的 |

**E 状态的价值**: 从 E 升级到 M 不需要总线事务（静默），因为只有本缓存持有该行。

#### 完整 MESI 状态转换表

**处理器事件**:

| 当前状态 | PrRd (处理器读命中) | PrWr (处理器写命中) |
|----------|---------------------|---------------------|
| **I** | 发送 BusRd → S 或 E | 发送 BusRdX → M |
| **S** | 不变 | 发送 BusRdX → M |
| **E** | 不变 | **静默** → M |
| **M** | 不变 | 不变 |

**总线监听**:

| 当前状态 | BusRd (其他处理器读) | BusRdX (其他处理器写) |
|----------|----------------------|------------------------|
| **S** | 不变 | → I |
| **E** | → S | → I |
| **M** | → S（写回内存） | → I（写回内存） |

**是否升级为 E 的判断**:

读未命中时，如果总线监听发现没有其他处理器的缓存持有该行（通过 shared signal），则进入 E 状态而非 S 状态。这需要总线支持 shared signal 机制。

```
总线上的 Shared Signal:
  ┌─────────────────────────────────────┐
  │ BusRd ADDR ────► 所有缓存监听 ADDR  │
  │                                     │
  │ 如果任何缓存持有该行 (S/E/M):       │
  │    ── 拉低 Shared Line ──► 进入 S   │
  │                                     │
  │ 如果没有缓存持有该行:                │
  │    ── Shared Line 保持高电平 ──► E  │
  └─────────────────────────────────────┘
```

#### MESI 相对于 MSI 的收益

| 场景 | MSI | MESI | 省去 |
|------|-----|------|------|
| 单线程变量读 + 写 | I→S (BusRd) + S→M (BusRdX) | I→E (BusRd) + E→M (静默) | 1 次 BusRdX |

对于常见编程模式，该优化的收益显著：
- 循环计数器 `for (i = 0; i < n; i++)`
- 局部变量 `int sum = 0; sum += a[i];`
- 函数调用栈的顶部

### 2.3 MOESI 协议 (Modified, Owned, Exclusive, Shared, Invalid)

增加 **O**wned 状态来支持**脏共享 (Dirty Sharing)**。

#### O 状态的价值

当处于 M 状态的处理器收到 BusRd (其他处理器读取该行) 时：

**MESI 行为**: M → I（或 M → S），需要写回内存，其他处理器从内存读取
**MOESI 行为**: M → O，不写回内存，直接向请求处理器提供数据

```
场景: Core0 修改了变量 x，Core1 读取 x

MESI:
  Core0 (M): ──► 写回 x=1 到内存
              ──► → S
  Core1 (I): ──► 从内存读取 x=1 ──► → S
  耗时: 写回 + 读取 = 2× 内存延迟

MOESI:
  Core0 (M): ──► → O（持有脏数据）
              ──► Cache-to-Cache 传输 x=1
  Core1 (I): ──► 从 Core0 接收 x=1 ──► → S
  耗时: Cache-to-Cache 传输 << 内存延迟
```

**MOESI 完整状态转换**:

```
     ┌────────────────────────────────────────┐
     │                                        │
     ▼                                        │
┌─────────┐   BusRd (no sharers)   ┌─────────┐│
│    I    │───────────────────────►│    E    ││
└─────────┘                        └─────────┘│
     │                                   │     │
     │ BusRd (sharers exist)             │ PrWr│
     │                                   │(silent)│
     ▼                                   ▼     │
┌─────────┐   PrWr (BusRdX)     ┌─────────┐   │
│    S    │◄────────────────────│    M    │◄──┘
└─────────┘                     └─────────┘
     ▲                               │
     │                               │ BusRd (snoop)
     │                               │ → O (no writeback needed)
     │                               ▼
     │                          ┌─────────┐
     └──────────────────────────│    O    │
          BusRd (from O)        └─────────┘
          → S                       │
                                    │ BusRdX (snoop)
                                    ▼
                               ┌─────────┐
                               │    I    │
                               └─────────┘
```

#### MOESI 状态总结

| 状态 | 数据来源 | 内存最新？ | 其他缓存可共享？ | 静默升级？ |
|------|----------|-----------|-----------------|-----------|
| **M** | 本缓存 | 否 | 否 | — |
| **O** | 本缓存 | 否 | 是 | 特殊 |
| **E** | 内存 | 是 | 否 | → M |
| **S** | 内存/Owner | 是 | 是 | → M (BusRdX) |
| **I** | — | — | — | — |

## 3. 协议实现

### 3.1 监听总线 (Snooping Bus)

**实现原理**:

- 所有缓存在总线上监听 (Snoop) 事务
- 每个缓存维护自己的状态机
- 广播机制：所有事务对所有缓存可见

**监听控制器的基本结构**:

```c
typedef struct {
    CoherenceCacheLine lines[NUM_LINES];
    int cache_id;
} SnoopingCache;

// 监听函数
void snoop(SnoopingCache *cache, uint32_t address, BusTransaction txn) {
    int idx = find_line(cache, address);
    if (idx < 0) return;  // 没有该行

    switch (txn) {
    case BUS_RD:
        if (cache->lines[idx].state == M) {
            // 提供数据，写回内存
            writeback(cache, idx);
            cache->lines[idx].state = S;
        } else if (cache->lines[idx].state == E) {
            cache->lines[idx].state = S;
        }
        break;
    case BUS_RD_X:
        if (cache->lines[idx].state != I) {
            if (cache->lines[idx].state == M) writeback(cache, idx);
            cache->lines[idx].state = I;
        }
        break;
    }
}
```

### 3.2 目录 (Directory-Based)

**目录结构**:

```
┌─────────────────────────────────────┐
│   Directory Controller              │
│                                     │
│   Entry 0: Tag=0x1000, State=S,     │
│            Sharers: [Core0, Core3]   │
│   Entry 1: Tag=0x2000, State=M,     │
│            Owner: Core1             │
│   Entry 2: Tag=0x3000, State=E,     │
│            Owner: Core2             │
│   ...                               │
└─────────────────────────────────────┘
```

**目录的大小开销**:

| 协议 | 目录组织形式 | 开销 |
|------|-------------|------|
| 全映射 (Full-Map) | 每个缓存行的位向量 | P 位/条目 (P 为核数) |
| 有限指针 (Limited Ptr) | i 个指针 (i < P) | i × log₂P 位/条目 |
| 粗粒度 (Coarse) | k 个条目共用一个位向量 | P/(k) 位/条目 |

**目录操作流程**:

```
读取操作:
  1. 缓存发送 GetS 到目录
  2. 目录查看状态:
     - I: 目录请求内存 → 返回数据，标记为 S，记录请求者为 Sharer
     - S/E: 目录请求内存 → 返回数据，添加请求者为 Sharer
     - M: 目录请求 Owner (M) 写回 + 转发数据 → 
           发送者 → S, 请求者 → S

写入操作:
  1. 缓存发送 GetM 到目录
  2. 目录查看状态:
     - I: 目录请求内存 → 返回数据 + 独占权限，标记为 M, Owner = 请求者
     - S/E: 目录向所有 Sharers 发送 Invalidate → 
            收到所有 ACK 后 → 返回数据 + 独占权限
     - M: 目录向 Owner 发送 Invalidate + Writeback → 
           转发数据 + 独占权限给请求者
```

## 4. 协议比较

### 状态数量与复杂性

| 协议 | 状态数 | 状态转换 | 总线事务类型 | 实现复杂度 |
|------|--------|----------|-------------|-----------|
| MSI | 3 | 6 | 2 (BusRd, BusRdX) | 低 |
| MESI | 4 | 12 | 2 (BusRd, BusRdX) | 中 |
| MOESI | 5 | 18 | 3 (BusRd, BusRdX, BusUpgr) | 高 |

### 性能比较 (相同访问模式)

```
访问序列: R(0x1000), W(0x1000), R(0x1000), R(0x1000)  (单处理器)

MSI:
  R1 → I→S (BusRd)
  W1 → S→M (BusRdX)
  R2 → M (hit)
  R3 → M (hit)
  总线事务: 2

MESI:
  R1 → I→E (BusRd)
  W1 → E→M (silent)
  R2 → M (hit)
  R3 → M (hit)
  总线事务: 1  ← 减少 50%
```

```
访问序列: R(0x1000, Core0), R(0x1000, Core1), W(0x1000, Core0)

MESI:
  Core0: I→E (BusRd)
  Core1: I→S (BusRd), Core0: E→S
  Core0: S→M (BusRdX), Core1: S→I
  总线事务: 3

MOESI (改进不明显，因为 Core1 没有保持脏数据):
  同 MESI
```

### 工业应用

| 协议 | 处理器/架构 |
|------|-------------|
| MESI | Intel x86 (自 Pentium Pro), ARM Cortex-A 系列 |
| MOESI | AMD Opteron/Athlon (HyperTransport), ARM CHI |
| MESIF | Intel Nehalem+ (QPI/UPI) — F = Forward, 
|       | 额外的 Forward 状态优化 Cache-to-Cache 转发 |

## 5. 潜在问题

### 5.1 竞态条件 (Race Conditions)

```
问题: 两个处理器同时尝试对同一行进行写操作

Core0: 发送 BusRdX (0x1000) ──────────────►
Core1: 发送 BusRdX (0x1000) ──────────────►
                    │
              ┌─────┴─────┐
              │   总线     │  → 谁先获得总线？
              └─────┬─────┘
                    │
              (仲裁胜者获得总线)
```

**解决方案**: 总线仲裁 (如 Daisy Chain, Decentralized Arbitration)

### 5.2 死锁 (Deadlock)

```
问题: 请求和响应共享同一网络通道

解决方案:
  - 独立的请求/响应虚拟网络
  - 至少 2 个虚通道 (VC)
```

### 5.3 伪共享的根本原因

伪共享的根本原因是缓存行粒度远大于变量粒度：

```
struct counters {
    int core0_count;  // 4 bytes
    int core1_count;  // 4 bytes
};
// 两个变量在同一缓存行 (64 bytes)

// 填充解决方案:
struct counters {
    int core0_count;
    char pad0[60];    // 填充到下一个缓存行
    int core1_count;
    char pad1[60];
};
```

## 6. 参考资料

- Papamarcos & Patel, "A Low-Overhead Coherence Solution for Multiprocessors with Private Cache Memories" (ISCA 1984) — MESI
- Sweazey & Smith, "A Class of Compatible Cache Consistency Protocols and their Support" (ISCA 1986) — MOESI
- Censier & Feautrier, "A New Solution to Coherence Problems in Multicache Systems" (IEEE TC 1978) — Directory
- Sorin, Hill, Wood, "A Primer on Memory Consistency and Cache Coherence" (Synthesis Lectures)
- MIT 6.823, Lectures 14–16
