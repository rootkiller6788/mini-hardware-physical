CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm
OUTDIR   = bin
TESTDIR  = bin/tests

SRCS     = $(wildcard src/*.c)
OBJS     = $(SRCS:src/%.c=$(OUTDIR)/%.o)
TESTS    = $(wildcard tests/*.c)
TESTEXE  = $(TESTS:tests/%.c=$(TESTDIR)/%)
EXAMPLES = $(wildcard examples/*.c)
EXE      = $(EXAMPLES:examples/%.c=$(OUTDIR)/%)

.PHONY: all test clean examples

all: $(OUTDIR) $(OBJS)

$(OUTDIR):
	mkdir -p $(OUTDIR) $(TESTDIR)

$(OUTDIR)/%.o: src/%.c include/*.h | $(OUTDIR)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(OUTDIR) $(OBJS) $(TESTEXE)
	@echo "=== Running lake_test_mini tests ==="
	@for t in $(TESTEXE); do \
		if [ -f "$$t" ]; then \
			if ./"$$t"; then echo "PASS: $$t"; \
			else echo "FAIL: $$t"; exit 1; fi; \
		fi; \
	done; \
	echo "=== All tests passed ==="

$(TESTDIR)/%: tests/%.c $(OBJS) | $(OUTDIR)
	$(CC) $(CFLAGS) $< $(OBJS) -o $@ $(LDFLAGS)

examples: $(OUTDIR) $(OBJS) $(EXE)

$(OUTDIR)/%: examples/%.c $(OBJS) | $(OUTDIR)
	$(CC) $(CFLAGS) $< $(OBJS) -o $@ $(LDFLAGS)

clean:
	rm -rf $(OUTDIR)
