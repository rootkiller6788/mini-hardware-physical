CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
SRCDIR  = src
EXDIR   = examples
OUTDIR  = bin

SRCS    = $(SRCDIR)/simd.c $(SRCDIR)/warp.c $(SRCDIR)/shader_core.c \
          $(SRCDIR)/tensor_core.c $(SRCDIR)/memory_gpu.c $(SRCDIR)/thread_sched.c

.PHONY: all test clean

all: simd_demo warp_sim_demo tensor_op_demo coalescing_demo occupancy_demo

$(OUTDIR):
	@mkdir -p "$(OUTDIR)"

simd_demo: $(OUTDIR) $(SRCS) $(EXDIR)/simd_demo.c
	$(CC) $(CFLAGS) $(EXDIR)/simd_demo.c $(SRCDIR)/simd.c -o $(OUTDIR)/simd_demo $(LDFLAGS)

warp_sim_demo: $(OUTDIR) $(SRCS) $(EXDIR)/warp_sim_demo.c
	$(CC) $(CFLAGS) $(EXDIR)/warp_sim_demo.c $(SRCDIR)/simd.c $(SRCDIR)/warp.c -o $(OUTDIR)/warp_sim_demo $(LDFLAGS)

tensor_op_demo: $(OUTDIR) $(SRCS) $(EXDIR)/tensor_op_demo.c
	$(CC) $(CFLAGS) $(EXDIR)/tensor_op_demo.c $(SRCDIR)/tensor_core.c -o $(OUTDIR)/tensor_op_demo $(LDFLAGS)

coalescing_demo: $(OUTDIR) $(SRCS) $(EXDIR)/coalescing_demo.c
	$(CC) $(CFLAGS) $(EXDIR)/coalescing_demo.c $(SRCDIR)/memory_gpu.c -o $(OUTDIR)/coalescing_demo $(LDFLAGS)

occupancy_demo: $(OUTDIR) $(SRCS) $(EXDIR)/occupancy_demo.c
	$(CC) $(CFLAGS) $(EXDIR)/occupancy_demo.c $(SRCDIR)/thread_sched.c $(SRCDIR)/simd.c $(SRCDIR)/warp.c $(SRCDIR)/shader_core.c -o $(OUTDIR)/occupancy_demo $(LDFLAGS)

test: all
	@echo ========================================
	@echo Running SIMD Demo
	@echo ========================================
	@$(OUTDIR)/simd_demo
	@echo ""
	@echo ========================================
	@echo Running Warp Simulation Demo
	@echo ========================================
	@$(OUTDIR)/warp_sim_demo
	@echo ""
	@echo ========================================
	@echo Running Tensor Operations Demo
	@echo ========================================
	@$(OUTDIR)/tensor_op_demo
	@echo ""
	@echo ========================================
	@echo Running Memory Coalescing Demo
	@echo ========================================
	@$(OUTDIR)/coalescing_demo
	@echo ""
	@echo ========================================
	@echo Running Occupancy Calculator Demo
	@echo ========================================
	@$(OUTDIR)/occupancy_demo
	@echo ""
	@echo ========================================
	@echo All tests completed!
	@echo ========================================

clean:
	@rm -rf "$(OUTDIR)"
