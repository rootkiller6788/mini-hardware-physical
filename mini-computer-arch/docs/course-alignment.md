# 课程对齐文档 (Course Alignment)

## 模块与标准计算机体系结构课程的对齐

本模块 `mini-computer-arch` 覆盖了三大高校计算机体系结构课程的核心内容，重点关注存储器层次结构、缓存一致性、互连网络和虚拟内存。

---

## MIT 6.823 — Computer System Architecture

| 章节 | 主题 | 本模块对应文件和内容 |
|------|------|----------------------|
| Ch 9 | Interconnection Networks | `interconnect.h` / `interconnect.c` — Bus, Crossbar, Mesh, Ring, Tree 拓扑；路由算法与延迟估算 |
| Ch 11 | Memory Hierarchy | `memory_hierarchy.h` / `memory_hierarchy.c` — SRAM/DRAM/SSD 多级层次，平均访问时间 |
| Ch 12 | Cache Design | `cache.h` / `cache.c` — 组相联映射、LRU/FIFO/LFU/Random 替换、Write-Through/Write-Back |
| Ch 14 | Cache Coherence | `coherence.h` / `coherence.c` — MESI/MSI/MOESI 协议，监听总线与目录实现 |

---

## MIT 6.5900 — Computer System Architecture (Graduate)

| 主题 | 对应内容 |
|------|----------|
| Memory Consistency Models | `coherence.h` — 一致性模型基础（MESI 状态机） |
| Multiprocessor Design | `multicore.h` / `multicore.c` — 4 核模型，私有 L1 + 共享 L2 |
| Advanced Caching | `cache.h` — 多级缓存、替换策略比较 |
| Interconnect Scalability | `interconnect.h` — 五种拓扑的对分带宽和直径分析 |

---

## CMU 18-447 — Introduction to Computer Architecture

| 章节 | 主题 | 对应内容 |
|------|------|----------|
| Lecture 10–13 | Cache Design | `cache.h` — 缓存组织、地址分解、AMAT 计算 |
| Lecture 14–16 | Virtual Memory | `virtual_memory.h` / `virtual_memory.c` — 页表、TLB、需求分页、FIFO 页面置换 |
| Lecture 18–21 | Cache Coherence | `coherence.h` — MESI 协议、snooping、directory-based |
| Lecture 22–24 | Interconnects | `interconnect.h` — Mesh、Crossbar、路由算法 |
| Lecture 25–27 | DRAM & Memory Controllers | `demos/mini-memory-scheduler/README.md` — 行缓冲、FR-FCFS、地址映射 |

---

## 能力矩阵 (Competency Matrix)

| 能力 | 级别 | 评估方式 |
|------|------|----------|
| 理解存储器层次结构 | 入门 | 模拟并分析多级存储器 |
| 设计缓存 | 中级 | 实现并调整缓存参数 |
| 分析缓存一致性协议 | 中级 | 跟踪 MESI 状态转换 |
| 理解地址转换（虚拟内存） | 入门 | 实现 TLB 和页表 |
| 比较互连拓扑 | 中级 | 计算延迟、对分带宽 |
| 模拟多核系统 | 高级基础 | 多核 + 一致性 + 互连集成 |
| 理解 DRAM 结构 | 入门 | 解释行缓冲和调度 |

---

## 推荐学习路径 (Recommended Learning Path)

1. **Week 1–2**: Memory Hierarchy + Cache Design
   - `memory_hierarchy.h` → `cache.h` → `examples/cache_sim_demo.c`
   - 阅读 `demos/mini-cache-hierarchy/README.md`

2. **Week 3**: Virtual Memory
   - `virtual_memory.h` → `examples/tlb_demo.c`
   - 结合 MIT 6.823 讲义第 8 章

3. **Week 4–5**: Cache Coherence
   - `coherence.h` → `examples/coherence_demo.c`
   - 阅读 `demos/mini-coherence-protocol/README.md`
   - 结合 MIT 6.823 讲座 14–16

4. **Week 6**: Interconnection Networks
   - `interconnect.h` → `examples/bus_demo.c`
   - 阅读 `demos/mini-interconnect/README.md`
   - 结合 MIT 6.823 讲座 9

5. **Week 7**: DRAM and Memory Controllers
   - 阅读 `demos/mini-memory-scheduler/README.md`
   - 结合 CMU 18-447 讲座 25–27

6. **Week 8**: Multicore Integration
   - `multicore.h` → 整合缓存 + 一致性 + 互连
   - 完成所有四个运行示例
