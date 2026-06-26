CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I include
LDFLAGS = -lm
SRCDIR  = src
EXDIR   = examples
OUTDIR  = bin

SRCS    = src\isa.c src\pipeline.c src\superscalar.c \
          src\branch_pred.c src\ooo_exec.c src\register_file.c

all: isa_demo pipeline_sim_demo branch_pred_demo ooo_demo tomasulo_demo

bin:
	@if not exist bin mkdir bin

isa_demo: $(SRCS) examples\isa_demo.c | bin
	$(CC) $(CFLAGS) -o bin\isa_demo.exe examples\isa_demo.c $(SRCS) $(LDFLAGS)

pipeline_sim_demo: $(SRCS) examples\pipeline_sim_demo.c | bin
	$(CC) $(CFLAGS) -o bin\pipeline_sim_demo.exe examples\pipeline_sim_demo.c $(SRCS) $(LDFLAGS)

branch_pred_demo: $(SRCS) examples\branch_pred_demo.c | bin
	$(CC) $(CFLAGS) -o bin\branch_pred_demo.exe examples\branch_pred_demo.c $(SRCS) $(LDFLAGS)

ooo_demo: $(SRCS) examples\ooo_demo.c | bin
	$(CC) $(CFLAGS) -o bin\ooo_demo.exe examples\ooo_demo.c $(SRCS) $(LDFLAGS)

tomasulo_demo: $(SRCS) examples\tomasulo_demo.c | bin
	$(CC) $(CFLAGS) -o bin\tomasulo_demo.exe examples\tomasulo_demo.c $(SRCS) $(LDFLAGS)

test: all
	@echo ========================================
	@echo   Running ISA Demo
	@echo ========================================
	bin\isa_demo.exe
	@echo.
	@echo ========================================
	@echo   Running Pipeline Sim Demo
	@echo ========================================
	bin\pipeline_sim_demo.exe
	@echo.
	@echo ========================================
	@echo   Running Branch Pred Demo
	@echo ========================================
	bin\branch_pred_demo.exe
	@echo.
	@echo ========================================
	@echo   Running OOO Demo
	@echo ========================================
	bin\ooo_demo.exe
	@echo.
	@echo ========================================
	@echo   Running Tomasulo Demo
	@echo ========================================
	bin\tomasulo_demo.exe
	@echo.
	@echo ========================================
	@echo   All tests completed
	@echo ========================================

clean:
	@if exist bin rmdir /s /q bin

.PHONY: all test clean isa_demo pipeline_sim_demo branch_pred_demo ooo_demo tomasulo_demo
