# Mini Hardware Physical

A collection of **from-scratch, zero-dependency C implementations** of computer hardware and physical-layer concepts. Each module simulates or models real hardware behavior at an educational level — from logic gates to CPU pipelines, GPU SIMD cores, AI accelerators, network hardware, and storage controllers. Modules map to MIT, Stanford, and CMU courses, bridging hardware theory to runnable C code.

## Modules

| Module | Topics | Key Courses |
|--------|--------|-------------|
| [mini-digital-circuit](mini-digital-circuit/) | Logic gates, combinational/sequential circuits, FSM, RTL basics | MIT 6.004, MIT 6.111 |
| [mini-cpu-arch](mini-cpu-arch/) | ISA design, pipelining, superscalar, branch prediction, out-of-order execution | MIT 6.004, MIT 6.175, Stanford EE282 |
| [mini-computer-arch](mini-computer-arch/) | Memory hierarchy, cache design, virtual memory, multicore, coherence protocols | MIT 6.823, MIT 6.5900, CMU 18-447 |
| [mini-gpu-arch](mini-gpu-arch/) | SIMD/SIMT, warp scheduling, shader cores, tensor cores, GPU memory model | CMU 15-418, Stanford CS149 |
| [mini-ai-accelerator](mini-ai-accelerator/) | Systolic arrays, TPU ISA, quantization, sparse acceleration, dataflow | Google TPU ISCA 2017, MIT 6.5930 |
| [mini-hardware-security](mini-hardware-security/) | Side-channel attacks, cache timing, Meltdown/Spectre, secure enclaves, PUFs | MIT 6.5950, UC Berkeley CS261 |
| [mini-network-hardware](mini-network-hardware/) | NIC architecture, MAC/PHY, RDMA, hardware offloading, switch fabrics | Stanford CS144, MIT 6.829 |
| [mini-storage-hardware](mini-storage-hardware/) | SSD controller, FTL, wear leveling, garbage collection, NVMe protocol | CMU 18-746, Stanford CS240 |

## Design Philosophy

- **Zero external dependencies** — pure C (C99/C11), only `libc` and `libm`
- **Self-contained modules** — each directory has its own `Makefile`, `include/`, `src/`, `examples/`, `demos/`, `tests/`
- **Hardware simulation in software** — cycle-accurate or behavioral models of real hardware components
- **Theory-to-code mapping** — every module includes `docs/` with course-alignment notes
- **Practical demos** — logic gate simulators, pipeline simulators, cache simulators, TPU systolic arrays, and more

## Building

Each module is standalone. Navigate to a module directory and run:

```bash
cd mini-cpu-arch
make all    # build everything
make test   # run tests
```

Requires **GCC** and **GNU Make**.

## Project Structure

```
mini-hardware-physical/
├── mini-digital-circuit/       # Digital Logic & Circuit Design
├── mini-cpu-arch/              # CPU Microarchitecture
├── mini-computer-arch/         # Computer Architecture & Memory Systems
├── mini-gpu-arch/              # GPU Architecture & Parallel Processing
├── mini-ai-accelerator/        # AI Accelerators (TPU, NPU)
├── mini-hardware-security/     # Hardware Security & Side-Channel Attacks
├── mini-network-hardware/      # Network Hardware & Offloading
└── mini-storage-hardware/      # Storage Hardware & SSD Controllers
```

## License

MIT
