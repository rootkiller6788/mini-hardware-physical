CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -I include
LDFLAGS = -lm

SRCDIR  = src
EXDIR   = examples
OUTDIR  = bin

SRCS    = $(SRCDIR)/logic_gate.c $(SRCDIR)/combinational.c $(SRCDIR)/sequential.c           $(SRCDIR)/fsm.c $(SRCDIR)/rtl_basic.c $(SRCDIR)/boolean_algebra.c           $(SRCDIR)/timing.c $(SRCDIR)/bus_arbiter.c $(SRCDIR)/memory.c $(SRCDIR)/alu.c

.PHONY: all clean test

all: gate_sim_demo alu_demo fsm_demo counter_demo boolean_demo bus_demo pipeline_demo

$(OUTDIR):
	mkdir -p $(OUTDIR)

gate_sim_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/gate_sim_demo $(EXDIR)/gate_sim_demo.c $(SRCDIR)/logic_gate.c $(SRCDIR)/combinational.c $(LDFLAGS)

alu_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/alu_demo $(EXDIR)/alu_demo.c $(SRCS) $(LDFLAGS)

fsm_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/fsm_demo $(EXDIR)/fsm_demo.c $(SRCDIR)/fsm.c $(SRCDIR)/logic_gate.c $(LDFLAGS)

counter_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/counter_demo $(EXDIR)/counter_demo.c $(SRCDIR)/sequential.c $(LDFLAGS)

boolean_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/boolean_demo $(EXDIR)/boolean_demo.c $(SRCDIR)/boolean_algebra.c $(SRCDIR)/logic_gate.c $(LDFLAGS)

bus_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/bus_demo $(EXDIR)/bus_demo.c $(SRCDIR)/bus_arbiter.c $(LDFLAGS)

pipeline_demo: $(OUTDIR)
	$(CC) $(CFLAGS) -o $(OUTDIR)/pipeline_demo $(EXDIR)/pipeline_demo.c $(SRCDIR)/rtl_basic.c $(LDFLAGS)

test: all
	@echo ===== Running gate_sim_demo =====
	$(OUTDIR)/gate_sim_demo
	@echo ===== Running alu_demo =====
	$(OUTDIR)/alu_demo
	@echo ===== Running fsm_demo =====
	$(OUTDIR)/fsm_demo
	@echo ===== Running counter_demo =====
	$(OUTDIR)/counter_demo
	@echo ===== Running boolean_demo =====
	$(OUTDIR)/boolean_demo
	@echo ===== Running bus_demo =====
	$(OUTDIR)/bus_demo
	@echo ===== Running pipeline_demo =====
	$(OUTDIR)/pipeline_demo
	@echo All tests passed.

clean:
	rm -rf $(OUTDIR)
