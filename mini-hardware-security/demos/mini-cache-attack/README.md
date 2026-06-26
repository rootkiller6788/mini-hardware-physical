# Mini Cache Attack — Cache 侧信道攻击详解

> 本文档深入讲解缓存侧信道攻击 (Cache Side-Channel Attacks) 的原理与实现

---

## 1. 概述 (Overview)

缓存侧信道攻击利用现代处理器缓存 (Cache) 的访问时间差异来泄露秘密信息。
攻击者通过测量内存访问延迟，可以推断受害者进程访问了哪些缓存行 (Cache Line)，
从而间接推断秘密数据。

### 核心攻击类型

| 攻击类型 | 共享内存 | 适用场景 | 复杂度 |
|---------|---------|---------|-------|
| Flush+Reload | 需要 | 同主机进程间 | 低 |
| Prime+Probe | 不需要 | 跨VM/跨核心 | 中 |
| Evict+Time | 不需要 | 受害者自测 | 高 |

---

## 2. 缓存层次结构 (Cache Hierarchy)

现代处理器的缓存层次结构：

```
┌─────────────────────────────────────────┐
│  CPU Core                                │
│  ┌───────┐                                │
│  │  L1   │  32KB, 4 cycles latency        │
│  │ Cache │  8-way set associative          │
│  └───┬───┘                                │
│      │                                     │
│  ┌───┴───┐                                │
│  │  L2   │  256KB, 12 cycles latency       │
│  │ Cache │  8-way set associative          │
│  └───┬───┘                                │
│      │                                     │
│  ┌───┴───┐  (shared across cores)          │
│  │  L3   │  8MB, 40 cycles latency         │
│  │ Cache │  16-way set associative         │
│  └───┬───┘                                │
└──────┼───────────────────────────────────┘
       │
   ┌───┴───┐
   │ DRAM  │  100+ cycles latency
   │ 主存  │
   └───────┘
```

### 缓存映射 (Cache Mapping)

- **Direct Mapped**: 每个内存地址映射到唯一的缓存槽（高冲突）
- **Set Associative**: N-way 集合关联，每路可存一个缓存行
- **Fully Associative**: 任意地址可存任意位置（实现复杂）

---

## 3. Flush+Reload 攻击

### 3.1 原理

Flush+Reload 要求攻击者与受害者共享内存（同一物理页面）。攻击步骤：

1. **Flush**: 攻击者调用 `clflush` 指令，将共享内存的缓存行从整个缓存层次中驱逐
2. **等待**: 允许受害者进程执行，访问共享内存
3. **Reload**: 攻击者重新加载每个缓存行，测量访问时间
4. **推断**: 访问时间快的缓存行 = 受害者访问过 = 泄露秘密索引

### 3.2 时序分析

```
时间线:
  T0: 攻击者 clflush(probe_array[i])  for i in 0..255
  T1: --- 等待受害者访问 secret_index ---
  T2: 受害者访问 probe[secret_index]  -> cache HIT (数据已加载到缓存)
  T3: 攻击者 reload probe_array[0]:   t = 100ns (DRAM)   -> MISS
  T4: 攻击者 reload probe_array[42]:  t = 4ns   (L1 HIT)  -> HIT -> 秘密泄露!
  T5: 攻击者 reload probe_array[255]: t = 100ns (DRAM)   -> MISS
```

### 3.3 缓存行粒度

每个 `clflush` 刷新一个完整的 64 字节缓存行。因此，`probe_array` 的每个元素必须位于不同的缓存行中（通过 64 字节对齐实现）。

```c
// 每个元素 64 字节对齐，避免多个元素共享同一缓存行
uint8_t probe_array[256 * 64];  // 每个条目一个缓存行
```

### 3.4 实现要点

```c
// Flush 阶段
for (int i = 0; i < 256; i++) {
    clflush(&probe_array[i * 64]);  // 刷新缓存行
}

// Reload + 测量阶段
for (int i = 0; i < 256; i++) {
    time_t start = rdtsc();                 // 开始计时
    access(&probe_array[i * 64]);           // 访问目标
    time_t end = rdtsc();                   // 结束计时
    if (end - start < THRESHOLD) {
        // 缓存命中 -> 受害者访问过索引 i
        leaked_secret = i;
    }
}
```

---

## 4. Prime+Probe 攻击

### 4.1 原理

Prime+Probe 不需要共享内存，攻击者使用自己的内存区域探测缓存状态。

1. **Prime**: 攻击者用自己的数据填满感兴趣的缓存组
2. **等待**: 受害者执行，可能驱逐攻击者的数据
3. **Probe**: 攻击者重新测量自己数据的访问时间
4. **判断**: 如果某个缓存组访问变慢，说明受害者驱逐了攻击者的数据

### 4.2 驱逐集构造

```c
// 构造驱逐集：收集映射到同一缓存组的内存地址
int eviction_set[8];  // 8-way 关联，需要 8 个地址才能驱逐某一组的所有路
for (int i = 0; i < 8; i++) {
    eviction_set[i] = find_address_in_set(target_set);
}

// Prime 阶段：填满目标缓存组
for (int i = 0; i < 8; i++) {
    access(eviction_set[i]);
}

// Probe 阶段：测量驱逐集中每个地址的访问时间
for (int i = 0; i < 8; i++) {
    int time = measure_access_time(eviction_set[i]);
    if (time > THRESHOLD) {
        // 该缓存路的数据被驱逐 -> 受害者访问了该地址
    }
}
```

### 4.3 优势与局限

| 优势 | 局限 |
|-----|-----|
| 不需要共享内存 | 需要构造精确的驱逐集 |
| 可跨虚拟机攻击 | 缓存分区 (Cache Allocation) 可防御 |
| 不需要内核权限 | 高噪声环境检测困难 |

---

## 5. Evict+Time 攻击

### 5.1 原理

1. 测量受害者操作的正常执行时间（缓存热状态）
2. 攻击者释放缓存（填满缓存，驱逐受害者数据）
3. 再次测量受害者操作的执行时间
4. 如果时间显著增加，说明攻击者的数据驱逐了受害者的缓存行 -> 受害者使用了该数据

### 5.2 代码示例

```c
int baseline_time = measure_victim_operation();  // 热身

flush_cache();  // 攻击者刷新缓存

int post_flush_time = measure_victim_operation();

if (post_flush_time > baseline_time * 2) {
    // 受害者数据被驱逐 -> 受害者正在访问某个目标
}
```

---

## 6. 高级攻击变体

### 6.1 Flush+Flush

基于 `clflush` 指令本身的执行时间。如果数据已在缓存中，`clflush` 执行更快。

### 6.2 Cache Collision Attack

利用多个进程映射到同一缓存组的冲突。通过 Prime+Probe 检测哪些缓存组发生了冲突。

### 6.3 Rowhammer + Cache

结合 Rowhammer（内存行锤击）与缓存攻击，可跨越缓存分离实现攻击。

---

## 7. 防御措施 (Countermeasures)

| 防御 | 针对 | 效果 |
|-----|------|-----|
| 缓存分区 (CAT) | Prime+Probe | 高 |
| 预取防御 | Flush+Reload | 中 |
| 噪声注入 | 所有 | 中 |
| 检测异常缓存命中 | Flush+Reload | 低 |
| 常数时间编程 | 所有 | 高 |
| 硬件隔离 (SGX) | 所有 | 高 |

---

## 8. 防御代码示例

### 8.1 常数时间比较

```c
int constant_time_compare(const uint8_t *a, const uint8_t *b, int len) {
    int result = 0;
    for (int i = 0; i < len; i++) {
        result |= (a[i] ^ b[i]);  // 无分支
    }
    return result == 0;
}
```

### 8.2 索引掩码防御 (Spectre)

```c
int safe_index(int x, int array_size) {
    int mask = array_size - 1;
    return x & mask;  // 基于算术的边界检查，无分支
}
```

---

## 9. 本模块实现

本模块 (`cache_attack.h/c`) 提供了：

- `cache_attack_flush_reload()`: 完整 Flush+Reload 仿真
- `cache_attack_prime_probe()`: Prime+Probe 仿真
- `cache_attack_evict_time()`: Evict+Time 仿真
- 64 组 × 8 路缓存模型
- 可配置的缓存线大小和时序参数

### 仿真参数

| 参数 | 值 |
|-----|---|
| 缓存组数 (Sets) | 64 |
| 关联度 (Ways) | 8 |
| 缓存行大小 | 64 bytes |
| L1 命中延迟 | 4 ns |
| L2 命中延迟 | 12 ns |
| L3 命中延迟 | 40 ns |
| DRAM 延迟 | 100 ns |
| 命中阈值 | 50 ns |

---

## 10. 运行演示

```bash
make cache_timing_demo
./bin/cache_timing_demo
```

输出展示完整的 Flush+Reload 攻击流程和时序分析。

---

## 参考资料

- Yarom & Falkner (2014): FLUSH+RELOAD: A High Resolution, Low Noise, L3 Cache Side-Channel Attack
- Liu et al. (2015): Last-Level Cache Side-Channel Attacks are Practical
- MIT 6.5950: Hardware Security, Lecture 5-7
