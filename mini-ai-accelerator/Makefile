CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm

SRCDIR = src
EXDIR = examples
OUTDIR = bin

SRCS = $(SRCDIR)/systolic_array.c $(SRCDIR)/tpu_isa.c $(SRCDIR)/quantization.c \
       $(SRCDIR)/sparse_accel.c $(SRCDIR)/dataflow.c $(SRCDIR)/mma.c \
       $(SRCDIR)/accelerator_roofline.c $(SRCDIR)/attention_accel.c \
       $(SRCDIR)/tensor_core.c

all: $(OUTDIR) systolic_mm_demo quantize_demo sparse_dot_demo dataflow_demo \
     accelerator_analysis_demo attention_demo tensor_core_demo

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

accelerator_analysis_demo: $(EXDIR)/accelerator_analysis_demo.c \
		$(SRCDIR)/accelerator_roofline.c $(SRCDIR)/dataflow.c $(SRCDIR)/systolic_array.c
	$(CC) $(CFLAGS) $(EXDIR)/accelerator_analysis_demo.c \
		$(SRCDIR)/accelerator_roofline.c $(SRCDIR)/dataflow.c $(SRCDIR)/systolic_array.c \
		-o $(OUTDIR)/accelerator_analysis_demo $(LDFLAGS)

attention_demo: $(EXDIR)/attention_demo.c $(SRCDIR)/attention_accel.c
	$(CC) $(CFLAGS) $(EXDIR)/attention_demo.c $(SRCDIR)/attention_accel.c \
		-o $(OUTDIR)/attention_demo $(LDFLAGS)

tensor_core_demo: $(EXDIR)/tensor_core_demo.c $(SRCDIR)/tensor_core.c
	$(CC) $(CFLAGS) $(EXDIR)/tensor_core_demo.c $(SRCDIR)/tensor_core.c \
		-o $(OUTDIR)/tensor_core_demo $(LDFLAGS)

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
	@echo --- accelerator_analysis_demo ---
	./$(OUTDIR)/accelerator_analysis_demo
	@echo --- attention_demo ---
	./$(OUTDIR)/attention_demo
	@echo --- tensor_core_demo ---
	./$(OUTDIR)/tensor_core_demo
	@echo ========================================
	@echo   All demos completed
	@echo ========================================

clean:
	rm -rf $(OUTDIR)

.PHONY: all test clean
