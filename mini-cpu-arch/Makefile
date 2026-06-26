CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
SRCDIR  = src
EXDIR   = examples
OUTDIR  = bin

SRCS    = src/isa.c src/pipeline.c src/superscalar.c \
          src/branch_pred.c src/ooo_exec.c src/register_file.c \
          src/cache.c src/perf_model.c src/memory_system.c

all: isa_demo pipeline_sim_demo branch_pred_demo ooo_demo tomasulo_demo mem_hierarchy_demo

bin:
	@mkdir -p bin

isa_demo: $(SRCS) examples/isa_demo.c | bin
	$(CC) $(CFLAGS) -o bin/isa_demo examples/isa_demo.c $(SRCS) $(LDFLAGS)

pipeline_sim_demo: $(SRCS) examples/pipeline_sim_demo.c | bin
	$(CC) $(CFLAGS) -o bin/pipeline_sim_demo examples/pipeline_sim_demo.c $(SRCS) $(LDFLAGS)

branch_pred_demo: $(SRCS) examples/branch_pred_demo.c | bin
	$(CC) $(CFLAGS) -o bin/branch_pred_demo examples/branch_pred_demo.c $(SRCS) $(LDFLAGS)

ooo_demo: $(SRCS) examples/ooo_demo.c | bin
	$(CC) $(CFLAGS) -o bin/ooo_demo examples/ooo_demo.c $(SRCS) $(LDFLAGS)

tomasulo_demo: $(SRCS) examples/tomasulo_demo.c | bin
	$(CC) $(CFLAGS) -o bin/tomasulo_demo examples/tomasulo_demo.c $(SRCS) $(LDFLAGS)

mem_hierarchy_demo: $(SRCS) examples/mem_hierarchy_demo.c | bin
	$(CC) $(CFLAGS) -o bin/mem_hierarchy_demo examples/mem_hierarchy_demo.c $(SRCS) $(LDFLAGS)

test: all
	@echo ========================================
	@echo   Running ISA Demo
	@echo ========================================
	./bin/isa_demo
	@echo
	@echo ========================================
	@echo   Running Pipeline Sim Demo
	@echo ========================================
	./bin/pipeline_sim_demo
	@echo
	@echo ========================================
	@echo   Running Branch Pred Demo
	@echo ========================================
	./bin/branch_pred_demo
	@echo
	@echo ========================================
	@echo   Running OOO Demo
	@echo ========================================
	./bin/ooo_demo
	@echo
	@echo ========================================
	@echo   Running Tomasulo Demo
	@echo ========================================
	./bin/tomasulo_demo
	@echo
	@echo ========================================
	@echo   Running Memory Hierarchy Demo
	@echo ========================================
	./bin/mem_hierarchy_demo
	@echo
	@echo ========================================
	@echo   All tests completed
	@echo ========================================

clean:
	rm -rf bin

.PHONY: all test clean isa_demo pipeline_sim_demo branch_pred_demo ooo_demo tomasulo_demo mem_hierarchy_demo
