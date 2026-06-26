# mini-interconnect — 互连网络

## 概述 (Overview)

互连网络是多核处理器和大型并行系统的关键组件。它将处理器、内存和 I/O 设备连接在一起，决定了系统的通信延迟、带宽和可扩展性。本模块分析五种基本拓扑和路由算法，通过 C 实现比较其性能特性。

Interconnection networks form the communication backbone of multi-core processors and large-scale parallel systems. The choice of topology and routing algorithm directly impacts system performance, scalability, and cost.

## 理论基础 (Theory)

### 互连网络的基本参数

| 参数 | 符号 | 定义 |
|------|------|------|
| 节点度 (Node Degree) | d | 连接到节点的链路数量 |
| 直径 (Diameter) | D | 网络中任意两节点间的最大最短距离 |
| 对分带宽 (Bisection Bandwidth) | B | 将网络分为两半的最小切分带宽 |
| 平均距离 | — | 所有节点对之间的平均跳数 |
| 链路数 | L | 网络中边的总数 |

### 拓扑结构

#### 1. 总线 (Bus)

```
    ┌────────────────────────────────────────────┐
    │                Shared Bus                  │
    └──┬─────────┬─────────┬─────────┬──────────┘
       │         │         │         │
    ┌──┴──┐   ┌──┴──┐   ┌──┴──┐   ┌──┴──┐
    │ PE0 │   │ PE1 │   │ PE2 │   │ PE3 │
    └─────┘   └─────┘   └─────┘   └─────┘
```

- **节点度**: 每个节点 d = 1
- **直径**: D = 1（总线是广播介质）
- **对分带宽**: B = 1（所有通信共享一条总线）
- **链路数**: L = N（每个节点一个连接 + 1 条总线）

**特点**:
- 最简单、最低成本
- 总线带宽是所有通信的瓶颈
- 不支持同时多对通信
- 适用于 2–8 个节点的小规模系统

#### 2. 交叉开关 (Crossbar)

```
    ┌──────┐   ┌──────┐   ┌──────┐   ┌──────┐
    │ PE0  │   │ PE1  │   │ PE2  │   │ PE3  │
    └──┬───┘   └──┬───┘   └──┬───┘   └──┬───┘
       │          │          │          │
    ───┼──────────┼──────────┼──────────┼───
       │    ┌─────┴──────────┼──────────┼───
       │    │     ┌──────────┴──────────┼───
       │    │     │     ┌───────────────┴───
    ───┼────┼─────┼─────┼───────────────────
       │    │     │     │
    ┌──┴────┴─────┴─────┴───┐
    │    Crossbar Switch    │
    └───────────────────────┘
```

- **节点度**: d = 1
- **直径**: D = 2（input → switch → output）
- **对分带宽**: B = N（可以同时支持 N 对通信）
- **链路数**: L = 2N（输入端 N + 输出端 N）
- **交叉点**: N² 个

**特点**:
- 非阻塞！任何排列可以同时进行
- 成本随 N² 增长
- 适用于 8–32 端口的中规模系统

#### 3. Mesh (网格)

```
    4×4 2D Mesh:
    ┌────┐    ┌────┐    ┌────┐    ┌────┐
    │ 00 │───│ 01 │───│ 02 │───│ 03 │
    └──┬─┘    └──┬─┘    └──┬─┘    └──┬─┘
       │         │         │         │
    ┌──┴─┐    ┌──┴─┐    ┌──┴─┐    ┌──┴─┐
    │ 10 │───│ 11 │───│ 12 │───│ 13 │
    └──┬─┘    └──┬─┘    └──┬─┘    └──┬─┘
       │         │         │         │
    ┌──┴─┐    ┌──┴─┐    ┌──┴─┐    ┌──┴─┐
    │ 20 │───│ 21 │───│ 22 │───│ 23 │
    └──┬─┘    └──┬─┘    └──┬─┘    └──┬─┘
       │         │         │         │
    ┌──┴─┐    ┌──┴─┐    ┌──┴─┐    ┌──┴─┐
    │ 30 │───│ 31 │───│ 32 │───│ 33 │
    └────┘    └────┘    └────┘    └────┘
```

- **节点度**: d = 4（内部），2（边），3（角）
- **直径**: D = (k-1)×2 = 2k-2（对于 k×k mesh）
- **对分带宽**: B = k（对于 k×k mesh）= √N
- **链路数**: L ≈ 2k(k-1) ≈ 2N

**特点**:
- 可扩展性好，布线方便
- 端到端延迟随 √N 增长
- 适用于 16–256 节点的中大规模系统
- 变体：Torus（通过环绕连接降低直径）

#### 4. Ring (环)

```
    ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐
    │  0  │─────►│  1  │─────►│  2  │─────►│  3  │
    └─────┘      └─────┘      └─────┘      └─────┘
       ▲                                        │
       │                                        │
       └────────────────────────────────────────┘
```

- **节点度**: d = 2
- **直径**: D = ⌊N/2⌋
- **对分带宽**: B = 2（环总是被分切成两半）
- **链路数**: L = N

**特点**:
- 简单、规则
- 直径随 N 线性增长
- 对分带宽恒定为 2（不随 N 增长）
- 适用于 4–16 节点的系统

#### 5. Tree (树)

```
                     ┌──────┐
                     │ Root │
                     └──┬───┘
              ┌────────┼────────┐
          ┌───┴───┐        ┌───┴───┐
          │ Node  │        │ Node  │
          └──┬───┘        └──┬───┘
        ┌────┼────┐     ┌────┼────┐
    ┌───┴─┐  │  ┌─┴───┐ │  ┌─┴───┐ │
    │Leaf │  │  │Leaf │ │  │Leaf │ │
    └─────┘  │  └─────┘ │  └─────┘ │
          ┌──┴──┐   ┌───┴──┐  ┌───┴──┐
          │ PE  │   │  PE  │  │  PE  │
          └─────┘   └──────┘  └──────┘
```

- **节点度**: d ≤ 3（二叉树）
- **直径**: D = 2×log₂N（叶到叶）
- **对分带宽**: B = 1（二叉树根处瓶颈）
- **链路数**: L = N-1

**特点**:
- 父节点是瓶颈
- **胖树 (Fat Tree)**: 增加高层链路带宽来解决瓶颈
- 广泛应用于 InfiniBand

### 拓扑比较表

| 拓扑 | 节点度 | 直径 | 对分带宽 | 链路数 | 扩展性 |
|------|--------|------|----------|--------|--------|
| Bus | 1 | 1 | 1 | N | 差 |
| Crossbar | 1 | 2 | N | 2N | 中（N²成本） |
| Mesh (k×k) | 2–4 | 2k-2 | k | ≈2N | 好 |
| Ring | 2 | ⌊N/2⌋ | 2 | N | 中 |
| Tree | ≤3 | 2log₂N | 1 | N-1 | 根瓶颈 |

### 路由算法

#### 确定性路由 (Deterministic Routing)

消息路径仅由源和目的地决定，不依赖网络状态。

**XY 路由 (Mesh)**:
```
1. 先在 X 方向路由（东西）
2. 然后在 Y 方向路由（南北）
```
- 无死锁
- 简单实现
- 不利用备选路径

**E-Cube 路由 (Hypercube)**:
逐维路由，每次在一个维度上移动。

#### 自适应路由 (Adaptive Routing)

根据网络拥塞情况选择路径。

**最小自适应路由**: 总是走最短路径，但在多个最短路径中选择
- 西优先 (West-First)
- 北后 (North-Last)
- 负优先 (Negative-First)

**完全自适应路由**: 允许非最短路径（绕远路避免拥塞）

### 死锁、活锁与饥饿

| 问题 | 描述 | 解决方案 |
|------|------|----------|
| 死锁 (Deadlock) | 消息循环等待，所有消息被阻塞 | 虚通道 (Virtual Channels)、转弯模型 |
| 活锁 (Livelock) | 消息不断移动但无法到达目的地 | 最小自适应路由 |
| 饥饿 (Starvation) | 某些消息永远得不到服务 | 公平调度 |

### 流控制 (Flow Control)

| 粒度 | 描述 | 缓冲需求 |
|------|------|----------|
| 消息级 (Message) | 整个消息 | 大缓冲 |
| 包级 (Packet) | 网络包 | 中等缓冲 |
| Flit 级 | 流控单元 | 小缓冲（1 flit） |
| 虫孔 (Wormhole) | 流水线式 flit 传输 | 极小缓冲 |

## 实现细节 (Implementation)

### 数据结构

```c
typedef enum { BUS, CROSSBAR, MESH, RING, TREE } Topology;

typedef struct {
    Node nodes[64];
    Topology topology;
    double routing_table[64][64];
} Interconnect;
```

### 路由实现

```c
uint32_t intcn_route(Interconnect *icn, uint32_t src, uint32_t dst,
                     uint32_t *path, uint32_t *path_len) {
    switch (icn->topology) {
    case BUS:      return bus_route(src, dst, path, path_len);
    case CROSSBAR: return crossbar_route(src, dst, path, path_len);
    case MESH:     return mesh_route(src, dst, path, path_len);
    case RING:     return ring_route(src, dst, path, path_len);
    case TREE:     return tree_route(src, dst, path, path_len);
    }
}
```

### Mesh XY 路由算法

```c
// source: (sx, sy), dest: (dx, dy)
uint32_t cx = sx, cy = sy;
// 先路由 X 方向
while (cx != dx) {
    cx += (dx > cx) ? 1 : -1;
    path[len++] = cy * width + cx;
}
// 再路由 Y 方向
while (cy != dy) {
    cy += (dy > cy) ? 1 : -1;
    path[len++] = cy * width + cx;
}
```

### 延迟模型

| 拓扑 | 延迟估计 (无拥塞) |
|------|--------------------|
| Bus | 1–2 周期（仲裁延迟） |
| Crossbar | 2 周期（输入/输出缓冲区） |
| Mesh | (Δx + Δy)×T_hop |
| Ring | min(Δ_forward, Δ_backward)×T_hop |
| Tree | log₂(N)×2×T_hop |

## 代码示例 (Code)

```c
Interconnect bus, bar, mesh, ring;

intcn_init(&mesh, MESH, 4);  // 4x4 mesh
for (int i = 0; i < 16; i++) intcn_add_node(&mesh, PE, i%4, i/4);

intcn_print_topology(&mesh);
intcn_print_route(&mesh, 0, 15);  // corner to opposite corner
// Route [0] -> [15]: 0 -> 1 -> 2 -> 3 -> 7 -> 11 -> 15
// Hops: 6, Latency: 13.0 ns
```

## 实验与练习 (Exercises)

1. **拓扑比较**: 对 N=4, 8, 16, 64 绘制直径和对分带宽图
2. **路由算法分析**: 实现西优先和北后路由并比较平均延迟
3. **拥塞模拟**: 模拟随机流量下不同拓扑的吞吐量
4. **胖树分析**: 验证胖树消除根瓶颈的效果
5. **虚通道**: 为 Mesh 添加 2 个虚通道并测量死锁避免
6. **Torus 变体**: 将 Mesh 改为 Torus 并计算直径改进

## 参考资料 (References)

- Dally & Towles, "Principles and Practices of Interconnection Networks"
- MIT 6.823, Lecture 9: Interconnection Networks
- CMU 18-447, Lecture 22–24: Networks and Routers
- Duato, Yalamanchili, Ni, "Interconnection Networks: An Engineering Approach"
