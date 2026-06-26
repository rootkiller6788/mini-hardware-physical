CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm

SRCDIR = src
EXDIR = examples
TSTDIR = tests
OUTDIR = bin

SRCS = $(SRCDIR)/simd.c $(SRCDIR)/warp.c $(SRCDIR)/shader_core.c \
       $(SRCDIR)/tensor_core.c $(SRCDIR)/memory_gpu.c $(SRCDIR)/thread_sched.c

all: $(OUTDIR) test_suite simd_demo warp_demo sm_occupancy_demo gemm_demo mem_demo sched_demo

$(OUTDIR):
	mkdir -p $(OUTDIR)

# --- Test suite ---
test_suite: $(TSTDIR)/test_suite.c $(SRCS)
	$(CC) $(CFLAGS) $(TSTDIR)/test_suite.c $(SRCS) -o $(OUTDIR)/test_suite $(LDFLAGS)

# --- Examples ---
simd_demo: $(EXDIR)/simd_demo.c $(SRCDIR)/simd.c
	$(CC) $(CFLAGS) $(EXDIR)/simd_demo.c $(SRCDIR)/simd.c -o $(OUTDIR)/simd_demo $(LDFLAGS)

warp_demo: $(EXDIR)/warp_demo.c $(SRCDIR)/warp.c
	$(CC) $(CFLAGS) $(EXDIR)/warp_demo.c $(SRCDIR)/warp.c -o $(OUTDIR)/warp_demo $(LDFLAGS)

sm_occupancy_demo: $(EXDIR)/sm_occupancy_demo.c $(SRCDIR)/shader_core.c $(SRCDIR)/warp.c $(SRCDIR)/memory_gpu.c
	$(CC) $(CFLAGS) $(EXDIR)/sm_occupancy_demo.c $(SRCDIR)/shader_core.c $(SRCDIR)/warp.c $(SRCDIR)/memory_gpu.c -o $(OUTDIR)/sm_occupancy_demo $(LDFLAGS)

gemm_demo: $(EXDIR)/gemm_demo.c $(SRCDIR)/tensor_core.c
	$(CC) $(CFLAGS) $(EXDIR)/gemm_demo.c $(SRCDIR)/tensor_core.c -o $(OUTDIR)/gemm_demo $(LDFLAGS)

mem_demo: $(EXDIR)/mem_demo.c $(SRCDIR)/memory_gpu.c
	$(CC) $(CFLAGS) $(EXDIR)/mem_demo.c $(SRCDIR)/memory_gpu.c -o $(OUTDIR)/mem_demo $(LDFLAGS)

sched_demo: $(EXDIR)/sched_demo.c $(SRCDIR)/thread_sched.c
	$(CC) $(CFLAGS) $(EXDIR)/sched_demo.c $(SRCDIR)/thread_sched.c -o $(OUTDIR)/sched_demo $(LDFLAGS)

# --- Test target ---
test: all
	@echo ========================================
	@echo   Running GPU-Arch Test Suite
	@echo ========================================
	./$(OUTDIR)/test_suite
	@echo ========================================
	@echo   Running simd_demo
	@echo ========================================
	./$(OUTDIR)/simd_demo
	@echo ========================================
	@echo   Running warp_demo
	@echo ========================================
	./$(OUTDIR)/warp_demo
	@echo ========================================
	@echo   Running sm_occupancy_demo
	@echo ========================================
	./$(OUTDIR)/sm_occupancy_demo
	@echo ========================================
	@echo   Running gemm_demo
	@echo ========================================
	./$(OUTDIR)/gemm_demo
	@echo ========================================
	@echo   Running mem_demo
	@echo ========================================
	./$(OUTDIR)/mem_demo
	@echo ========================================
	@echo   Running sched_demo
	@echo ========================================
	./$(OUTDIR)/sched_demo
	@echo ========================================
	@echo   All tests and demos PASSED
	@echo ========================================

clean:
	rm -rf $(OUTDIR)

bench: all
	@echo ========================================
	@echo   Running GPU-Arch Benchmarks
	@echo ========================================
	./$(OUTDIR)/test_suite bench

.PHONY: all test clean bench
