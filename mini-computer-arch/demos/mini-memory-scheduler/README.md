# mini-memory-scheduler — DRAM 内存控制器调度

## 概述 (Overview)

DRAM 内存控制器是连接处理器和主存的关键桥梁。它负责将处理器的内存请求转换为 DRAM 命令序列，同时优化吞吐量和延迟。本模块深入分析 DRAM 架构、行缓冲管理和请求调度策略。

The DRAM memory controller is a critical component that translates processor memory requests into DRAM commands. Its scheduling decisions directly impact system performance through row buffer hit rates and bank-level parallelism.

## 理论基础 (Theory)

### DRAM 内部结构

DRAM 芯片按层次组织为多个存储单元：

```
┌──────────────────────────────────────────────────────┐
│                    DRAM Channel                      │
│  ┌────────────────────────────────────────────────┐  │
│  │                   Rank 0                       │  │
│  │  ┌───────────┐ ┌───────────┐ ┌───────────┐    │  │
│  │  │  Bank 0   │ │  Bank 1   │ │  Bank 7   │    │  │
│  │  │ ┌───────┐ │ │ ┌───────┐ │ │ ┌───────┐ │    │  │
│  │  │ │Row Buf│ │ │ │Row Buf│ │ │ │Row Buf│ │    │  │
│  │  │ ├───────┤ │ │ ├───────┤ │ │ ├───────┤ │    │  │
│  │  │ │  Row 0│ │ │ │  Row 0│ │ │ │  Row 0│ │    │  │
│  │  │ │  ...  │ │ │ │  ...  │ │ │ │  ...  │ │    │  │
│  │  │ │ Row N │ │ │ │ Row N │ │ │ │ Row N │ │    │  │
│  │  │ └───────┘ │ │ └───────┘ │ │ └───────┘ │    │  │
│  │  └───────────┘ └───────────┘ └───────────┘    │  │
│  └────────────────────────────────────────────────┘  │
│                         ...                           │
│  ┌────────────────────────────────────────────────┐  │
│  │                   Rank 1                       │  │
│  │                      ...                        │  │
│  └────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
```

### DRAM 层次术语

| 层次 | 定义 | 典型值 (DDR4) |
|------|------|---------------|
| Channel | 独立的内存总线 | 1–4 个通道 |
| Rank | 共享同一总线的一组芯片 | 1–4 个 Rank/通道 |
| Bank | 芯片内的独立存储阵列 | 4–16 个 Bank/Rank |
| Row | Bank 内的一行数据 | 1–8 KB |
| Column | 行内的数据位置 | 8–64 位 |

### 地址映射方案

内存地址到 DRAM 坐标的映射影响行缓冲命中率：

```
物理地址: [ Row | Bank | Column | Byte ]

常见的地址交织方案：

方案 1 (行优先):   [ Row ........ | Bank | Col | Byte ]
方案 2 (Bank优先): [ Row | Bank ........ | Col | Byte ]
方案 3 (Cache行):  [ Row | Bank | Col | Byte .. ]  
```

| 方案 | 连续访问 | 步长访问 | 说明 |
|------|----------|----------|------|
| 行优先 (Row-First) | 同一行 | 行缓冲命中高 | 空间局部性好 |
| Bank 优先 (Bank-First) | 跨 Bank | Bank 级并行 | 增加了 MLP |
| 混合 (Hybrid) | 平衡 | 中等 | 基于工作负载特性 |

### 行缓冲管理策略

#### 开页策略 (Open-Page Policy)

- **行为**: 访问后保持行处于激活（打开）状态
- **优点**: 行命中时延迟最低（只需 CAS 命令）
- **缺点**: 行未命中延迟最高（需要 PRE + ACT + CAS）
- **适用**: 具有高空间局部性的工作负载

```
时间轴（开页）:
  命中:     [CAS] [CAS] [CAS] ...
  未命中:   [PRE] [ACT] [CAS]
```

#### 闭页策略 (Closed-Page Policy)

- **行为**: 访问后自动关闭行（预充电）
- **优点**: 访问模式随机时降低延迟
- **缺点**: 即使连续访问也需要 ACT + CAS
- **适用**: 随机访问的工作负载（如服务器）

```
时间轴（闭页）:
  每次:     [ACT] [CAS] [PRE]
                  ↓
            [ACT] [CAS] [PRE]
```

### DRAM 时序参数

| 参数 | 全称 | 含义 | 典型值 (ns) |
|------|------|------|-------------|
| tRCD | RAS to CAS Delay | ACT → CAS 延迟 | 13–18 ns |
| tCL | CAS Latency | CAS → 数据可用 | 13–18 ns |
| tRP | Row Precharge | PRE → ACT 延迟 | 13–18 ns |
| tRAS | Row Active Time | ACT → PRE 最小间隔 | 32–50 ns |
| tRFC | Refresh Cycle | 刷新周期 | 260–350 ns |
| tFAW | Four Activate Window | 4 次激活窗口 | 20–40 ns |
| tWR | Write Recovery | 写恢复时间 | 10–15 ns |
| tRTP | Read to Precharge | 读 → PRE 最小间隔 | 5–7.5 ns |

### 内存请求调度算法

#### FR-FCFS (First-Ready, First-Come-First-Served)

FR-FCFS 是现代 DRAM 控制器中最常用的调度算法：
1. **行命中优先 (First-Ready)**: 优先调度行缓冲命中的请求
2. **FCFS**: 行未命中请求按到达顺序处理

```
FR-FCFS 优先级：
┌─────────────────────────────────────┐
│ Priority 1: Row Hit (CAS 即可)      │
│ Priority 2: 较老的 Row Miss         │
│ Priority 3: 较新的 Row Miss         │
└─────────────────────────────────────┘
```

**优点**:
- 最大化行缓冲命中率
- 提高总吞吐量

**缺点**:
- 可能导致饥饿 (Starvation)
- 非行命中的请求可能无限等待

#### 其他调度算法

| 算法 | 描述 | 优势 | 劣势 |
|------|------|------|------|
| FCFS | 先到先服务 | 公平 | 低行命中率 |
| FR-FCFS | 行命中优先 + FCFS | 高吞吐量 | 可能饥饿 |
| 最短延迟优先 (SJF) | 最小估计服务时间 | 低平均延迟 | 不公平 |
| 批处理 (Batching) | 定期形成批次并排序 | 平衡 | 批次间延迟 |
| PAR-BS | 并行感知批处理 | 银行并行 | 复杂度高 |
| ATLAS | 自适应阈值 | 动态平衡 | 参数调优 |

### 内存级并行 (MLP)

**定义**: 同时处理多个未完成的内存请求，利用多个 Bank 的并行性。

```
Bank0:  [ACT] [CAS] [CAS] [CAS] [PRE]
Bank1:       [ACT] [CAS] [CAS] [PRE]
Bank2:            [ACT] [CAS] [CAS] [CAS] [PRE]
Bank3:                 [ACT] [CAS] [CAS] [PRE]
          ↑ 重叠执行，提高总吞吐量
```

### 刷新 (Refresh)

DRAM 需要周期性刷新以保持数据：
- **周期**: 64ms（需要刷新所有行）
- **方式**: 分布式刷新（tRFC 分散在 64ms 内）
- **影响**: 每 7.8 μs 暂停约 260–350 ns

## 实现细节 (Implementation)

### 内存控制器架构

```
    ┌─────────────┐
    │   Request   │  来自处理器/缓存
    │   Queue     │
    └──────┬──────┘
           │
    ┌──────┴──────┐
    │  Scheduler  │  调度算法：FR-FCFS
    └──────┬──────┘
           │
    ┌──────┴──────┐
    │  Address    │  地址映射：物理地址 → (Rank, Bank, Row, Col)
    │  Mapper     │
    └──────┬──────┘
           │
    ┌──────┴──────┐
    │  Row Buffer │  每个 Bank 的行缓冲状态
    │  Tracker    │
    └──────┬──────┘
           │
    ┌──────┴──────┐
    │  Arbiter    │  为选中的请求发出 DRAM 命令
    │  (DRAM Cmd) │  ACT / CAS / PRE / REF
    └─────────────┘
```

### FR-FCFS 实现伪代码

```c
Request* schedule_request(RequestQueue *queue) {
    // 第一优先级：行命中
    for each bank:
        for each request to this bank:
            if request.row == bank.open_row:
                return request;  // 行命中！

    // 第二优先级：最老的行未命中
    Request *oldest = NULL;
    for each bank:
        for each request to this bank:
            if oldest == NULL || request.timestamp < oldest->timestamp:
                oldest = request;

    return oldest;
}
```

### 时序约束检查

```c
bool can_issue_command(DRAMCommand cmd, DRAMBbank *bank) {
    uint64_t now = get_current_time();

    switch (cmd) {
    case CMD_ACTIVATE:
        return (now - bank->last_precharge >= tRP) &&
               (now - bank->last_activate >= tRC);

    case CMD_READ:
        return (now - bank->last_activate >= tRCD) &&
               bank->row_is_open;

    case CMD_WRITE:
        return (now - bank->last_activate >= tRCD) &&
               (now - bank->last_write >= tWTR) &&
               bank->row_is_open;

    case CMD_PRECHARGE:
        return (now - bank->last_activate >= tRAS) &&
               (now - bank->last_read >= tRTP);
    }

    return false;
}
```

## 性能分析

### 影响 DRAM 性能的因素

1. **行缓冲命中率**: 
   - 命中：~20 ns (CAS Latency)
   - 未命中：~50 ns (PRE + tRCD + CAS)
   - 命中率 60% → AMAT = 0.6×20 + 0.4×50 = 32 ns

2. **Bank 级并行**:
   - 4 Bank 系统最多 4 倍吞吐量
   - 地址映射影响可用的 Bank 数量

3. **刷新开销**:
   - DDR4: 约 3–5% 的带宽损失
   - 温度影响：温度越高，刷新频率越快

### 优化技术

| 技术 | 描述 | 改进 |
|------|------|------|
| 行缓冲预测 | 预测下一个要访问的行 | 减少延迟 |
| 写分组 (Write Grouping) | 批量处理写请求 | 减少读写切换 |
| Bank 分组 | 限制同时激活的 Bank 数 | 降低功耗 |
| 预取 (Prefetching) | 提前加载即将使用的行 | 增加命中率 |
| 自适应调度 | 根据工作负载切换策略 | 平衡吞吐/延迟 |

## 代码示例 (Code)

完整的 DRAM 模拟器需要实现请求队列、行缓冲跟踪器和调度器。以下为简化示例：

```c
// 行缓冲跟踪
typedef struct {
    int open_row;      // 当前打开的行（-1 表示无）
    bool row_active;   // 行是否激活
    uint64_t last_act; // 上次激活时间
    uint64_t last_pre; // 上次预充电时间
} BankState;

// FR-FCFS 调度
int select_request(RequestQueue *q, BankState *banks) {
    // 第一优先级：行命中
    for (int i = 0; i < q->count; i++) {
        int bid = address_to_bank(q->requests[i].addr);
        int row = address_to_row(q->requests[i].addr);
        if (banks[bid].row_active && banks[bid].open_row == row)
            return i;
    }
    // 第二优先级：最老的请求
    return 0; // (简化)
}
```

## 实验与练习 (Exercises)

1. **地址映射比较**: 比较行优先和 Bank 优先映射在同一工作负载下的行命中率
2. **调度算法**: 实现 FCFS 和 FR-FCFS 并比较吞吐量和平均延迟
3. **Bank 并行**: 改变 Bank 数量（4 → 16）观察吞吐量变化
4. **时序参数**: 改变 tRCD/tCL/tRP 并观察性能敏感度
5. **刷新建模**: 添加刷新开销并测量性能影响
6. **开页 vs 闭页**: 在不同访问模式下比较两种策略

## 参考资料 (References)

- Bruce Jacob et al., "Memory Systems: Cache, DRAM, Disk"
- Rixner et al., "Memory Access Scheduling" (ISCA 2000)
- Mutlu & Moscibroda, "Parallelism-Aware Batch Scheduling" (ISCA 2008)
- Kim et al., "ATLAS: A Scalable and High-Performance Scheduling Algorithm" (HPCA 2010)
- JEDEC DDR4 SDRAM Standard (JESD79-4)
- MIT 6.823, DRAM System Architecture
