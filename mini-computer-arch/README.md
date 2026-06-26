# mini-computer-arch — 计算机体系结构 (C 语言实现)

> 参考 MIT 6.823, MIT 6.5900, CMU 18-447

## 模块与课程映射 (Module-Course Mapping)

| 本模块组件 | MIT 6.823 | MIT 6.5900 | CMU 18-447 |
|------------|-----------|------------|------------|
| `memory_hierarchy.h/c` | Ch 11 | ✓ | L10–13 |
| `cache.h/c` | Ch 12 | ✓ | L10–13 |
| `virtual_memory.h/c` | — | ✓ | L14–16 |
| `multicore.h/c` | — | ✓ | — |
| `interconnect.h/c` | Ch 9 | ✓ | L22–24 |
| `coherence.h/c` | Ch 14 | ✓ | L18–21 |

## 目录结构 (Directory Tree)

```
mini-computer-arch/
├── include/
│   ├── memory_hierarchy.h     # 存储器层次结构
│   ├── cache.h                # 缓存仿真器
│   ├── virtual_memory.h       # 虚拟内存 + TLB
│   ├── multicore.h            # 多核处理器模型
│   ├── interconnect.h         # 互连网络
│   └── coherence.h            # 缓存一致性协议
├── src/
│   ├── memory_hierarchy.c
│   ├── cache.c
│   ├── virtual_memory.c
│   ├── multicore.c
│   ├── interconnect.c
│   └── coherence.c
├── examples/
│   ├── cache_sim_demo.c       # 缓存仿真器示例
│   ├── tlb_demo.c             # 虚拟内存 + TLB 示例
│   ├── bus_demo.c             # 互连网络示例
│   └── coherence_demo.c       # MESI 一致性示例
├── demos/
│   ├── mini-cache-hierarchy/  # 缓存层次设计详解
│   ├── mini-coherence-protocol/ # 一致性协议详解
│   ├── mini-interconnect/     # 互连网络详解
│   └── mini-memory-scheduler/ # DRAM 调度器详解
├── docs/
│   ├── course-alignment.md    # 课程对齐文档
│   ├── cache-design.md        # 缓存设计原理
│   ├── coherence-protocols.md # 一致性协议详解
│   └── memory-systems.md      # 完整内存系统
├── tests/                     # (预留)
├── benches/                   # (预留)
├── README.md
└── Makefile
```

## 构建与运行 (Build & Run)

### 编译所有示例

```bash
make all
```

### 运行单个示例

```bash
make cache_sim_demo
make tlb_demo
make bus_demo
make coherence_demo
```

### 运行所有测试

```bash
make test
```

### 清理

```bash
make clean
```

## 快速开始 (Quick Start)

```bash
# 编译并运行缓存仿真器
make all
bin/cache_sim_demo.exe

# 编译并运行 TLB 示例
make tlb_demo
bin/tlb_demo.exe

# 编译并运行互连网络示例
make bus_demo
bin/bus_demo.exe

# 编译并运行一致性示例
make coherence_demo
bin/coherence_demo.exe
```

## 组件说明 (Component Overview)

### 1. 存储器层次结构 (Memory Hierarchy)

`memory_hierarchy.h` / `memory_hierarchy.c`

定义 SRAM → DRAM → SSD 的多级层次，计算平均访问时间 (AMAT)。

### 2. 缓存仿真器 (Cache Simulator)

`cache.h` / `cache.c`

组相联缓存仿真，支持 LRU/FIFO/LFU/Random 替换策略和 Write-Through/Write-Back 写策略。

### 3. 虚拟内存 (Virtual Memory)

`virtual_memory.h` / `virtual_memory.c`

页表管理、TLB 仿真、FIFO 页面置换、需求分页。

### 4. 多核模型 (Multicore Model)

`multicore.h` / `multicore.c`

4 核处理器模型，私有 L1 缓存 + 共享 L2 缓存。

### 5. 互连网络 (Interconnection Networks)

`interconnect.h` / `interconnect.c`

Bus, Crossbar, Mesh, Ring, Tree 拓扑的路由和延迟估算。

### 6. 缓存一致性 (Cache Coherence)

`coherence.h` / `coherence.c`

MSI/MESI/MOESI 协议实现，支持监听总线和目录两种实现方式。

## 设计原则 (Design Principles)

- C99 标准，仅依赖 libc 和 libm
- 所有函数使用 snake_case, 类型使用 PascalCase, 常量使用 UPPER_SNAKE_CASE
- 自包含头文件，使用 `#ifndef` 包含保护
- 清晰的输出格式，便于理解体系结构概念
