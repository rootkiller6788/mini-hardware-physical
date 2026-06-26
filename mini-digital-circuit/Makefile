CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -I include
LDFLAGS = -lm

SRCDIR  = src
EXDIR   = examples
OUTDIR  = bin

SRCS    = $(SRCDIR)/logic_gate.c $(SRCDIR)/combinational.c $(SRCDIR)/sequential.c $(SRCDIR)/fsm.c $(SRCDIR)/rtl_basic.c

.PHONY: all clean test

all: gate_sim_demo alu_demo fsm_demo counter_demo

$(OUTDIR):
	if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

gate_sim_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/gate_sim_demo $(EXDIR)/gate_sim_demo.c $(SRCDIR)/logic_gate.c $(SRCDIR)/combinational.c $(LDFLAGS)

alu_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/alu_demo $(EXDIR)/alu_demo.c $(SRCS) $(LDFLAGS)

fsm_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/fsm_demo $(EXDIR)/fsm_demo.c $(SRCDIR)/fsm.c $(LDFLAGS)

counter_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/counter_demo $(EXDIR)/counter_demo.c $(SRCDIR)/sequential.c $(LDFLAGS)

test: all
	@echo ===== Running gate_sim_demo =====
	$(OUTDIR)/gate_sim_demo
	@echo.
	@echo ===== Running alu_demo =====
	$(OUTDIR)/alu_demo
	@echo.
	@echo ===== Running fsm_demo =====
	$(OUTDIR)/fsm_demo
	@echo.
	@echo ===== Running counter_demo =====
	$(OUTDIR)/counter_demo
	@echo.
	@echo All tests passed.

clean:
	if exist "$(OUTDIR)" rmdir /s /q "$(OUTDIR)"
