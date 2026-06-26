CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm

SRCDIR = src
EXDIR = examples
OUTDIR = bin

SRCS = $(SRCDIR)/memory_hierarchy.c $(SRCDIR)/cache.c $(SRCDIR)/virtual_memory.c \
       $(SRCDIR)/multicore.c $(SRCDIR)/interconnect.c $(SRCDIR)/coherence.c

OBJS = $(SRCS:.c=.o)

DEMOS = cache_sim_demo tlb_demo bus_demo coherence_demo

.PHONY: all test clean

all: $(OUTDIR) $(addprefix $(OUTDIR)/, $(DEMOS:=.exe))

$(OUTDIR):
	-mkdir $(OUTDIR)

$(OUTDIR)/cache_sim_demo.exe: $(OBJS) $(EXDIR)/cache_sim_demo.c
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EXDIR)/cache_sim_demo.c $(LDFLAGS)

$(OUTDIR)/tlb_demo.exe: $(OBJS) $(EXDIR)/tlb_demo.c
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EXDIR)/tlb_demo.c $(LDFLAGS)

$(OUTDIR)/bus_demo.exe: $(OBJS) $(EXDIR)/bus_demo.c
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EXDIR)/bus_demo.c $(LDFLAGS)

$(OUTDIR)/coherence_demo.exe: $(OBJS) $(EXDIR)/coherence_demo.c
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(EXDIR)/coherence_demo.c $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

cache_sim_demo: $(OUTDIR)/cache_sim_demo.exe
	$(OUTDIR)/cache_sim_demo.exe

tlb_demo: $(OUTDIR)/tlb_demo.exe
	$(OUTDIR)/tlb_demo.exe

bus_demo: $(OUTDIR)/bus_demo.exe
	$(OUTDIR)/bus_demo.exe

coherence_demo: $(OUTDIR)/coherence_demo.exe
	$(OUTDIR)/coherence_demo.exe

test: all
	@echo ========================================
	@echo   Running Cache Simulator Demo
	@echo ========================================
	$(OUTDIR)/cache_sim_demo.exe
	@echo.
	@echo ========================================
	@echo   Running TLB Demo
	@echo ========================================
	$(OUTDIR)/tlb_demo.exe
	@echo.
	@echo ========================================
	@echo   Running Bus / Interconnect Demo
	@echo ========================================
	$(OUTDIR)/bus_demo.exe
	@echo.
	@echo ========================================
	@echo   Running Coherence (MESI) Demo
	@echo ========================================
	$(OUTDIR)/coherence_demo.exe

clean:
	-del /Q $(SRCDIR)\*.o 2>nul
	-rmdir /S /Q $(OUTDIR) 2>nul
