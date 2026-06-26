CC = gcc
CFLAGS = -Wall -Wextra -O2 -I include
LDFLAGS = -lm

MINGW_BIN = D:/msys64/mingw64/bin
MINGW_USR = D:/msys64/usr/bin
EXPORT_PATH = export PATH=$(MINGW_BIN):$(MINGW_USR):$$PATH

SRCDIR = src
EXDIR = examples
OUTDIR = bin

SRCS = $(SRCDIR)/side_channel.c $(SRCDIR)/cache_attack.c $(SRCDIR)/spec_exec_atk.c \
       $(SRCDIR)/secure_enclave.c $(SRCDIR)/puf.c $(SRCDIR)/fault_injection.c

OBJS = $(SRCS:$(SRCDIR)/%.c=$(OUTDIR)/%.o)

DEMOS = cache_timing_demo spectre_demo meltdown_demo puf_demo

all: $(OBJS)
	@echo "All library objects compiled."

$(OUTDIR):
	@mkdir -p "$(OUTDIR)"

$(OUTDIR)/%.o: $(SRCDIR)/%.c | $(OUTDIR)
	$(EXPORT_PATH) && $(CC) $(CFLAGS) -c $< -o $@

cache_timing_demo: $(EXDIR)/cache_timing_demo.c $(OUTDIR)/cache_attack.o | $(OUTDIR)
	$(EXPORT_PATH) && $(CC) $(CFLAGS) $< $(OUTDIR)/cache_attack.o -o $(OUTDIR)/cache_timing_demo $(LDFLAGS)
	@echo "  Running cache_timing_demo..."
	$(OUTDIR)/cache_timing_demo

spectre_demo: $(EXDIR)/spectre_demo.c $(OUTDIR)/spec_exec_atk.o | $(OUTDIR)
	$(EXPORT_PATH) && $(CC) $(CFLAGS) $< $(OUTDIR)/spec_exec_atk.o -o $(OUTDIR)/spectre_demo $(LDFLAGS)

meltdown_demo: $(EXDIR)/meltdown_demo.c $(OUTDIR)/spec_exec_atk.o | $(OUTDIR)
	$(EXPORT_PATH) && $(CC) $(CFLAGS) $< $(OUTDIR)/spec_exec_atk.o -o $(OUTDIR)/meltdown_demo $(LDFLAGS)

puf_demo: $(EXDIR)/puf_demo.c $(OUTDIR)/puf.o | $(OUTDIR)
	$(EXPORT_PATH) && $(CC) $(CFLAGS) $< $(OUTDIR)/puf.o -o $(OUTDIR)/puf_demo $(LDFLAGS)
	@echo "  Running puf_demo..."
	$(OUTDIR)/puf_demo

test: cache_timing_demo spectre_demo meltdown_demo puf_demo
	@echo ""
	@echo "========================================"
	@echo "  ALL TESTS COMPLETE"
	@echo "========================================"

clean:
	@rm -rf "$(OUTDIR)"
	@echo "Cleaned build artifacts."

.PHONY: all test clean cache_timing_demo spectre_demo meltdown_demo puf_demo
