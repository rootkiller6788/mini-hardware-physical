# mini-hardware-physical — 文件创建计划

参照 `0. mini-math-theory` 的格式与风格，为当前目录下 8 个空模块创建完整文件。

---

## 一、顶层文件

| # | 文件 | 内容 |
|---|------|------|
| 1 | `README.md` | 英文顶层说明：intro、模块表、设计理念、构建说明、目录树、MIT License |
| 2 | `README-CN.md` | 中文顶层说明，结构与英文版对应 |

---

## 二、每个模块的标准目录结构

参照 math-theory 的 7 目录 + Makefile 模式：

```
mini-<module>/
├── README.md
├── Makefile
├── include/          # 头文件 (*.h)
├── src/              # 源文件 (*.c)，与 include 一一对应
├── examples/         # *_demo.c 可运行示例 (3-6 个)
├── demos/            # 子 demo 目录 (3-6 个，每个仅 README.md)
├── docs/             # 文档 (course-alignment.md + 2-4 篇专业笔记)
├── tests/            # 测试目录（初始可为空）
└── benches/          # 性能测试目录（初始可为空）
```

---

## 三、8 个模块规划详情

### 3.1 mini-digital-circuit — 数字电路

| 维度 | 内容 |
|------|------|
| **对标** | MIT 6.004 Computation Structures, MIT 6.111/6.205 FPGA/SoC |
| **include/** | `logic_gate.h`, `combinational.h`, `sequential.h`, `fsm.h`, `rtl_basic.h` |
| **src/** | 对应 5 个 .c |
| **examples/** | `gate_sim_demo.c`, `alu_demo.c`, `fsm_demo.c`, `counter_demo.c` |
| **demos/** | `mini-alu/`, `mini-traffic-light-fsm/`, `mini-simple-cpu-rtl/` |
| **docs/** | `course-alignment.md`, `logic-fundamentals.md`, `rtl-design-patterns.md` |

### 3.2 mini-cpu-arch — CPU 架构

| 维度 | 内容 |
|------|------|
| **对标** | MIT 6.004, MIT 6.175 RISC-V, Stanford EE282 |
| **include/** | `isa.h`, `pipeline.h`, `superscalar.h`, `branch_pred.h`, `ooo_exec.h`, `register_file.h` |
| **src/** | 对应 6 个 .c |
| **examples/** | `isa_demo.c`, `pipeline_sim_demo.c`, `branch_pred_demo.c`, `ooo_demo.c`, `tomasulo_demo.c` |
| **demos/** | `mini-riscv-interpreter/`, `mini-tomasulo/`, `mini-branch-predictor/`, `mini-cache-sim/` |
| **docs/** | `course-alignment.md`, `pipeline-hazards.md`, `branch-prediction-survey.md`, `ooo-execution.md` |

### 3.3 mini-computer-arch — 计算机体系结构

| 维度 | 内容 |
|------|------|
| **对标** | MIT 6.823, MIT 6.5900, CMU 18-447 |
| **include/** | `memory_hierarchy.h`, `cache.h`, `virtual_memory.h`, `multicore.h`, `interconnect.h`, `coherence.h` |
| **src/** | 对应 6 个 .c |
| **examples/** | `cache_sim_demo.c`, `tlb_demo.c`, `bus_demo.c`, `coherence_demo.c` |
| **demos/** | `mini-cache-hierarchy/`, `mini-coherence-protocol/`, `mini-interconnect/`, `mini-memory-scheduler/` |
| **docs/** | `course-alignment.md`, `cache-design.md`, `coherence-protocols.md`, `memory-systems.md` |

### 3.4 mini-gpu-arch — GPU 架构

| 维度 | 内容 |
|------|------|
| **对标** | CMU 15-418 Parallel Arch, Stanford CS149, UMich EECS 570 |
| **include/** | `simd.h`, `warp.h`, `shader_core.h`, `tensor_core.h`, `memory_gpu.h`, `thread_sched.h` |
| **src/** | 对应 6 个 .c |
| **examples/** | `simd_demo.c`, `warp_sim_demo.c`, `tensor_op_demo.c`, `coalescing_demo.c`, `occupancy_demo.c` |
| **demos/** | `mini-simd-engine/`, `mini-warp-scheduler/`, `mini-tensor-core/`, `mini-gpu-mem-model/` |
| **docs/** | `course-alignment.md`, `simt-model.md`, `gpu-memory-coalescing.md`, `tensor-core-internals.md` |

### 3.5 mini-ai-accelerator — AI 加速器

| 维度 | 内容 |
|------|------|
| **对标** | Google TPU ISCA 2017, MIT 6.5930, Stanford CS217 |
| **include/** | `systolic_array.h`, `tpu_isa.h`, `quantization.h`, `sparse_accel.h`, `dataflow.h`, `mma.h` |
| **src/** | 对应 6 个 .c |
| **examples/** | `systolic_mm_demo.c`, `quantize_demo.c`, `sparse_dot_demo.c`, `dataflow_demo.c` |
| **demos/** | `mini-systolic-array/`, `mini-tpu-sim/`, `mini-quantized-inference/`, `mini-sparse-accelerator/` |
| **docs/** | `course-alignment.md`, `systolic-arrays.md`, `quantization-techniques.md`, `tpu-architecture.md` |

### 3.6 mini-hardware-security — 硬件安全

| 维度 | 内容 |
|------|------|
| **对标** | MIT 6.5950, UC Berkeley CS261, Stanford CS356 |
| **include/** | `side_channel.h`, `cache_attack.h`, `spec_exec_atk.h`, `secure_enclave.h`, `puf.h`, `fault_injection.h` |
| **src/** | 对应 6 个 .c |
| **examples/** | `cache_timing_demo.c`, `spectre_demo.c`, `meltdown_demo.c`, `puf_demo.c` |
| **demos/** | `mini-cache-attack/`, `mini-spectre/`, `mini-secure-enclave/`, `mini-puf-auth/` |
| **docs/** | `course-alignment.md`, `side-channel-primer.md`, `speculative-execution-attacks.md`, `secure-enclaves.md` |

### 3.7 mini-network-hardware — 网络硬件

| 维度 | 内容 |
|------|------|
| **对标** | Stanford CS144, MIT 6.829, UC Berkeley EE 122 |
| **include/** | `nic_arch.h`, `mac.h`, `rdma.h`, `offload.h`, `switch_fabric.h`, `pcie.h` |
| **src/** | 对应 6 个 .c |
| **examples/** | `mac_demo.c`, `rdma_demo.c`, `switch_sim_demo.c`, `checksum_offload_demo.c` |
| **demos/** | `mini-nic-sim/`, `mini-rdma-transport/`, `mini-switch-fabric/`, `mini-hardware-offload/` |
| **docs/** | `course-alignment.md`, `nic-architecture.md`, `rdma-internals.md`, `hardware-offloading.md` |

### 3.8 mini-storage-hardware — 存储硬件

| 维度 | 内容 |
|------|------|
| **对标** | CMU 18-746 Storage Systems, Stanford CS240, MIT 6.5830 |
| **include/** | `ftl.h`, `ssd_controller.h`, `nvme.h`, `wear_leveling.h`, `gc.h`, `ecc.h` |
| **src/** | 对应 6 个 .c |
| **examples/** | `ftl_demo.c`, `wear_level_demo.c`, `gc_demo.c`, `nvme_cmd_demo.c`, `ecc_demo.c` |
| **demos/** | `mini-ftl-sim/`, `mini-ssd-controller/`, `mini-wear-leveler/`, `mini-nvme-queue/` |
| **docs/** | `course-alignment.md`, `ftl-internals.md`, `nand-flash-basics.md`, `nvme-protocol.md` |

---

## 四、README 风格选择

采用 **风格A（中英双语学术型）**，与 `mini-computation-theory`、`mini-discrete-math` 等保持一致：
- 标题包含中文副标题（`# mini-xxx — 中文名 (C 语言实现)`）
- 开头 blockquote 引用对标课程
- 中文章节标题
- 模块对照表
- 精简目录树 + 构建说明

---

## 五、阶段化执行计划

### 阶段 0：基础骨架搭建

| # | 任务 | 产出 |
|---|------|------|
| 0.1 | 创建顶层 `README.md` | 英文顶层说明（模块表、设计理念、构建、License） |
| 0.2 | 创建顶层 `README-CN.md` | 中文顶层说明 |
| 0.3 | 初始化 `.gitignore` | 排除 `*.obj`, `*.exe`, `build/` 等 |

**里程碑**：目录拥有顶层文档，可作为项目入口。

---

### 阶段 1：核心基础模块（4 个模块的完整实现）

从底层硬件向上层逐步推进，先夯实基础。

| 阶段 | 模块 | 任务 |
|------|------|------|
| **1.1** | `mini-digital-circuit` | ① `README.md` + `Makefile` → ② `include/` + `src/` → ③ `docs/` → ④ `examples/` → ⑤ `demos/` → ⑥ `tests/` + `benches/` |
| **1.2** | `mini-cpu-arch` | 同上顺序 |
| **1.3** | `mini-computer-arch` | 同上顺序 |
| **1.4** | `mini-storage-hardware` | 同上顺序 |

**里程碑**：数字电路 → CPU → 体系结构 → 存储 四条链路连通，形成完整 Von Neumann 体系覆盖。

---

### 阶段 2：专用处理器模块（2 个模块）

| 阶段 | 模块 | 任务 |
|------|------|------|
| **2.1** | `mini-gpu-arch` | ① `README.md` + `Makefile` → ② `include/` + `src/` → ③ `docs/` → ④ `examples/` → ⑤ `demos/` → ⑥ `tests/` + `benches/` |
| **2.2** | `mini-ai-accelerator` | 同上顺序 |

**里程碑**：从通用 CPU 扩展到 GPU 和 AI 专用加速器，覆盖异构计算图景。

---

### 阶段 3：硬件交叉领域（2 个模块）

| 阶段 | 模块 | 任务 |
|------|------|------|
| **3.1** | `mini-network-hardware` | ① `README.md` + `Makefile` → ② `include/` + `src/` → ③ `docs/` → ④ `examples/` → ⑤ `demos/` → ⑥ `tests/` + `benches/` |
| **3.2** | `mini-hardware-security` | 同上顺序 |

**里程碑**：硬件知识域从单机扩展到网络 + 安全，覆盖完整硬件技术栈。

---

### 阶段 4：收尾校验

| # | 任务 | 产出 |
|---|------|------|
| 4.1 | 全量一致性检查 | 确认每个模块 `include/` ↔ `src/` 一一对应 |
| 4.2 | Makefile 可构建验证 | 确保 `make all` 通过（至少编译通过） |
| 4.3 | README 交叉链接 | 顶层 README 中模块表链接到各子模块 README |
| 4.4 | Git 初始化 + 首次提交 | `git init && git add -A && git commit -m "..."` |

**里程碑**：整个 `mini-hardware-physical` 目录完整可用，与 `mini-math-theory` 对等。

---

### 每个模块内部的子任务顺序

```
模块名/
│
├─ [1] README.md         ← 先写文档，明确模块定位
├─ [2] Makefile          ← 构建骨架
├─ [3] include/*.h       ← 头文件（接口定义）
├─ [4] src/*.c           ← 源文件（实现）
├─ [5] docs/*.md         ← 课程对照 + 专业笔记
├─ [6] examples/*_demo.c ← 可运行示例
├─ [7] demos/mini-*/     ← 深度 Demo（README.md）
└─ [8] tests/ + benches/ ← 目录占位
```

---

### 进度总览表

| 阶段 | 模块数 | 预计核心文件数 | 状态 |
|------|--------|----------------|------|
| 阶段 0 | 顶层 | 3 文件 | ✅ 已完成 |
| 阶段 1 | 4 模块 | ~102 文件 | ✅ 已完成 |
| 阶段 2 | 2 模块 | ~53 文件 | ✅ 已完成 |
| 阶段 3 | 2 模块 | ~52 文件 | ✅ 已完成 |
| 阶段 4 | 收尾 | - | ✅ 已完成 |
| **合计** | **8 模块** | **~210 文件** | ✅ 已完成 |

---

## 六、代码规范

- **语言**：纯 C99，仅依赖 libc + libm
- **命名**：snake_case 文件名与函数名，PascalCase 结构体名
- **Include Guard**：`#ifndef MODULE_H` / `#define MODULE_H`
- **构建**：每个模块独立 Makefile，目标 `all` / `clean` / `test`
