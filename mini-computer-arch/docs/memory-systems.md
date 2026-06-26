# Memory Systems — 完整内存系统视图

## 1. 存储器层次结构全景

### 层次模型

将现代内存系统看作一个从上到下速度和容量呈指数变化的层次结构：

```
┌──────────────────────────────────────────────────────────────┐
│  级别       │ 典型大小    │ 延迟      │ 带宽         │ 技术   │
├──────────────────────────────────────────────────────────────┤
│  Register   │ ~1 KB      │ 0.3 ns    │ ~100 GB/s    │ FF     │
│  L1 Cache   │ 32-64 KB   │ 1 ns      │ ~100 GB/s    │ SRAM   │
│  L2 Cache   │ 256-512 KB │ 4 ns      │ ~50 GB/s     │ SRAM   │
│  L3 Cache   │ 4-16 MB    │ 12 ns     │ ~30 GB/s     │ SRAM   │
│  DRAM       │ 8-64 GB    │ 50 ns     │ ~20 GB/s     │ DRAM   │
│  SSD/NAND   │ 256 GB-2TB │ 10 µs     │ ~3 GB/s      │ NAND   │
│  HDD        │ 2-20 TB    │ 10 ms     │ ~150 MB/s    │ Magn   │
└──────────────────────────────────────────────────────────────┘
```

### 访问模式

```
理想化迭代（无缓存未命中）:
  for (i = 0; i < N; i++)
      sum += a[i];
  → 1 次 DRAM 未命中 (强制) + N-1 次 L1 命中

一次未命中的代价:
  L1 Miss → L2:   4 个周期
  L2 Miss → L3:   12 个周期
  L3 Miss → DRAM: 50+ 个周期

典型微基准:
  - 顺序扫描 (stream):  L1 命中率 ~4 次访问/行 (步长=1 字)
  - 随机访问 (random):   L1 命中率 → 0 (除非工作集适合 L1)
  - 指针追赶 (pointer):  延迟限制，MLP 无法隐藏
```

## 2. 虚拟内存与地址转换

### 完整的地址转换路径

```
    虚拟地址 (32-bit)
         │
    ┌────┴────┐
    │   VPN   │ Offset
    └────┬────┘     │
         │          │
    ┌────┴────┐     │
    │   TLB   │ ◄── TLB 查找 (快速)
    └────┬────┘
         │ TLB Miss?
         ▼
    ┌──────────┐
    │ Page     │ ◄── 硬件页表遍历
    │ Table    │       (x86: 4 级页表)
    └────┬─────┘
         │ Page Fault?
         ▼
    ┌──────────┐
    │ OS       │ ◄── 页故障处理程序
    │ Handler  │       (软件介入)
    └────┬─────┘
         │
         ▼
    物理地址 (PPN + Offset)
         │
    ┌────┴────┐
    │  Cache  │ ◄── 缓存访问
    └─────────┘
```

### 地址转换加速器

| 技术 | 位置 | 作用 |
|------|------|------|
| TLB (Translation Lookaside Buffer) | 在 L1 访问之前 | 缓存 VPN→PPN 映射 |
| 页表缓存 (MMU Cache) | MMU | 缓存中间级页表条目 |
| 超大页 (Huge Pages) | TLB | 1 个条目覆盖 2MB 或 1GB |
| TLB 预取 | 硬件 | 基于步长预测下一页 |
| ASIDs (Address Space ID) | TLB | 避免上下文切换时刷新 TLB |

### TLB 参数 (典型)

| 参数 | L1 TLB (指令) | L1 TLB (数据) | L2 TLB |
|------|--------------|--------------|--------|
| 条目数 | 128 | 64–128 | 1024–1536 |
| 相联度 | 4–8 way | 4–8 way | 8–12 way |
| 页大小 | 4KB/2MB | 4KB/2MB | 4KB/2MB |
| 访问延迟 | 1 cycle | 1 cycle | 6–8 cycles |

### 页表结构演变

```
单级页表 (32-bit):
  VPN[20 bits] │ Offset[12 bits]
  2^20 = 1M entries × 4B = 4MB per process (常驻)

两级页表 (32-bit PAE):
  PD[10] │ PT[10] │ Offset[12]
  仅分配实际使用的页表 → 工作集 < 4MB

四级页表 (64-bit, x86-64):
  PML4[9] │ PDP[9] │ PD[9] │ PT[9] │ Offset[12]
  48-bit 虚拟地址 (256 TB)
  每级 512 entries, 8B 每 entry

五级页表 (x86-64, 57-bit):
  PML5[9] │ PML4[9] │ PDP[9] │ PD[9] │ PT[9] │ Offset[12]
  57-bit 虚拟地址 (128 PB)
```

## 3. 多级缓存策略

### 包含性策略

| 策略 | L2 包含 L1？ | 优点 | 缺点 |
|------|-------------|------|------|
| 包含 (Inclusive) | 是 | 一致性简单 (只需查 L2) | L2 浪费空间 |
| 互斥 (Exclusive) | 否 | L1+L2 有效总容量最大 | 一致性复杂 |
| 非包含非互斥 (NINE) | 不一定 | 灵活性高 | 需要两者的技术 |

Intel Skylake: L2 是 NINE，L3 是包含 (Inclusive)
AMD Zen: L2 是包含 (Inclusive)，L3 是 NINE (受害者缓存)

### 多级缓存的 AMAT 链

```
AMAT = Hit_L1 + MR_L1 × (Hit_L2 + MR_L2 × (Hit_L3 + MR_L3 × Penalty_DRAM))

参数示例:
  L1: 1ns, MR=5%
  L2: 5ns, MR=20%
  L3: 15ns, MR=40%
  DRAM: 60ns

  计算结果:
  L1 Miss -> L2 代价: 5ns
  L2 Miss -> L3 代价: 15ns
  L3 Miss -> DRAM 代价: 60ns

  AMAT_L1_miss = 5 + 0.2 × (15 + 0.4 × 60)
                = 5 + 0.2 × (15 + 24)
                = 5 + 0.2 × 39
                = 5 + 7.8
                = 12.8 ns

  AMAT_total = 1 + 0.05 × 12.8
             = 1 + 0.64
             = 1.64 ns
```

## 4. 预取 (Prefetching)

### 预取分类

| 类型 | 触发机制 | 示例 |
|------|----------|------|
| 硬件预取 | 基于地址模式检测 | Intel 流预取器、步长预取器 |
| 软件预取 | 显式指令 | `prefetchnta` (x86), `prfm` (ARM) |
| 混合预取 | 两者结合 | 编译器插入 + 硬件辅助 |

### 常见硬件预取器

| 预取器 | 模式 | 适用场景 |
|--------|------|----------|
| 下一行 (Next-Line) | A → A+64B | 顺序访问 |
| 流 (Stream) | A, A+64, A+128 → A+192, A+256 | 长序列 |
| 步长 (Stride) | A, A+k, A+2k → A+3k, A+4k | 多维数组 |
| 基于指针 (Pointer-Based) | 基于历史跳转 | 链表/树 |
| GHB (Global History Buffer) | 全局历史关联 | 复杂模式 |

### 预取的时机和质量

```
预取距离 (Prefetch Distance):
  d = latency / bandwidth_per_line

  例如: 延迟 = 100 cycles, 带宽 = 8B/cycle, 行 = 64B
       d = 100 / (64/8) = 100 / 8 ≈ 12.5 → 13 lines ahead

过早预取:  数据到来时还未使用 → 浪费缓存空间
过晚预取:  数据到来时处理器已等待 → 预取未能隐藏延迟

预取准确率 (Accuracy):
  accuracy = useful_prefetches / total_prefetches

  accuracy < 1 → 产生了缓存污染 (Cache Pollution)
```

## 5. 内存级并行 (Memory-Level Parallelism)

### 并行原理

MLP 通过同时处理多个未完成的内存请求来隐藏延迟：

```
无 MLP:
  Req1:  [───────── 50ns ──────────][───]
  Req2:                                [───────── 50ns ──────────][───]
  Req3:                                                               [────...]
  总时间: 150ns + 3×服务时间

有 MLP (3 请求同时):
  Req1:  [───────── 50ns ──────────][───]
  Req2:  [───────── 50ns ──────────][───]
  Req3:  [───────── 50ns ──────────][───]
  总时间: 50ns + 3×服务时间
  改进: 3× 吞吐量
```

### 影响 MLP 的因素

| 因素 | 效果 |
|------|------|
| 乱序执行窗口 | 窗口 = 192 (Skylake) → 可容纳 192 条指令 |
| MSHR 数量 | 10 (L1 MSHRs) → 最多 10 个未完成 L1 请求 |
| Bank 并行 | 8 Banks → 8 倍 Bank 级并行 |
| 依赖链长度 | 长链 → 限制 MLP |
| 预取 | 提前发出请求 → 增加实际 MLP |

### MSHR (Miss Status Holding Register)

MSHR 是缓存未命中时记录未完成请求的硬件结构：

```
┌─────────────────────────────────────────────────┐
│ MSHR Entry:                                     │
│   - Target Address (缓存地址)                   │
│   - Status: PENDING / FULFILLED                │
│   - Destination Register (数据写回哪个寄存器)   │
│   - 合并信息 (多个请求请求同一行)               │
└─────────────────────────────────────────────────┘
```

## 6. 内存控制器与 DRAM

### DRAM 层次

```
DRAM 组织 = Channel × Rank × Bank × Row × Column

DDR4 典型配置:
  2 Channels, 2 Ranks/Channel, 16 Banks/Rank
  8192 Rows/Bank, 1024 Columns/Row (64B 每列)
  
  总容量 = 2 × 2 × 16 × 8192 × 1024 × 64B
         = 34,359,738,368 bytes
         = 32 GB
```

### 内存交换粒度

| 操作 | 行命中 (Page Hit) | 行未命中 (Page Miss) |
|------|-------------------|---------------------|
| 读 | CAS (tCL = 13ns) | PRE(tRP=13ns) + ACT(tRCD=13ns) + CAS(13ns) = ~39ns |
| 写 | CAS (tCWL = 10ns) | PRE(13ns) + ACT(13ns) + CAS(10ns) = ~36ns |

## 7. 地址交织 (Address Interleaving)

```
物理地址 → (Row, Bank, Rank, Column, Byte)
          │     │     │     │       └─ 字节偏移
          │     │     │     └─────────── 列 (Column)
          │     │     └───────────────── 行 (Row)
          │     └─────────────────────── 块 (Rank)
          └───────────────────────────── 组 (Bank)

常见的三种映射:
  [0 ... Row | Bank | Rank | Column | Byte]  Row 优先 (Row-First)
  [0 ... Bank | Row | Rank | Column | Byte]  Bank 优先 (Bank-First)
  [0 ... Rank | Bank | Row | Column | Byte]  Rank 优先 (Rank-First)
```

Row 优先 → 连续地址在同一行 → 高行命中率
Bank 优先 → 连续地址跨 Bank → 高 Bank 并行度

## 8. 性能优化清单

### 软件层次

| 优化项 | 目标 | 受影响的结构 |
|--------|------|-------------|
| 结构体填充 | 消除伪共享 | L1/L2 一致性 |
| 数组分布 (SOA) | 提高向量化 | L1 利用率 |
| 分块 (Tiling/Blocking) | 提高缓存利用率 | L1/L2/L3 |
| 循环交换 | 改善访存模式 | 所有缓存 |
| 预取插入 | 隐藏延迟 | L1/L2 |
| 避免指针追赶 | 减少依赖 | MLP |
| 对齐分配 | 避免跨行边界 | L1 效率 |

### 硬件层次

| 优化项 | 目标 |
|--------|------|
| 增加缓存层级/大小 | 减少未命中率 |
| 增大相联度 | 减少冲突未命中 |
| 硬件预取 | 自动隐藏延迟 |
| 更大的 MSHR | 更大的 MLP |
| 增加内存带宽 | 减少带宽瓶颈 |

## 参考资料

- Jacob, Ng, Wang, "Memory Systems: Cache, DRAM, Disk"
- Drepper, "What Every Programmer Should Know About Memory"
- Hennessy & Patterson, Chapters 2 and Appendix B
- McCalpin, "STREAM: Sustainable Memory Bandwidth in High Performance Computers"
- MIT 6.823, MIT 6.5900, CMU 18-447 lecture notes
