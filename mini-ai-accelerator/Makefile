CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm

SRCDIR = src
EXDIR = examples
OUTDIR = bin

SRCS = $(SRCDIR)/systolic_array.c $(SRCDIR)/tpu_isa.c $(SRCDIR)/quantization.c \
       $(SRCDIR)/sparse_accel.c $(SRCDIR)/dataflow.c $(SRCDIR)/mma.c

all: $(OUTDIR) systolic_mm_demo quantize_demo sparse_dot_demo dataflow_demo

$(OUTDIR):
	mkdir -p $(OUTDIR)

systolic_mm_demo: $(EXDIR)/systolic_mm_demo.c $(SRCDIR)/systolic_array.c
	$(CC) $(CFLAGS) $(EXDIR)/systolic_mm_demo.c $(SRCDIR)/systolic_array.c -o $(OUTDIR)/systolic_mm_demo $(LDFLAGS)

quantize_demo: $(EXDIR)/quantize_demo.c $(SRCDIR)/quantization.c
	$(CC) $(CFLAGS) $(EXDIR)/quantize_demo.c $(SRCDIR)/quantization.c -o $(OUTDIR)/quantize_demo $(LDFLAGS)

sparse_dot_demo: $(EXDIR)/sparse_dot_demo.c $(SRCDIR)/sparse_accel.c
	$(CC) $(CFLAGS) $(EXDIR)/sparse_dot_demo.c $(SRCDIR)/sparse_accel.c -o $(OUTDIR)/sparse_dot_demo $(LDFLAGS)

dataflow_demo: $(EXDIR)/dataflow_demo.c $(SRCDIR)/dataflow.c
	$(CC) $(CFLAGS) $(EXDIR)/dataflow_demo.c $(SRCDIR)/dataflow.c -o $(OUTDIR)/dataflow_demo $(LDFLAGS)

test: all
	@echo ========================================
	@echo   Running all demos
	@echo ========================================
	@echo --- systolic_mm_demo ---
	./$(OUTDIR)/systolic_mm_demo
	@echo --- quantize_demo ---
	./$(OUTDIR)/quantize_demo
	@echo --- sparse_dot_demo ---
	./$(OUTDIR)/sparse_dot_demo
	@echo --- dataflow_demo ---
	./$(OUTDIR)/dataflow_demo
	@echo ========================================
	@echo   All demos completed
	@echo ========================================

clean:
	rm -rf $(OUTDIR)

.PHONY: all test clean
