CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm
SRCDIR   = src
EXDIR    = examples
OUTDIR   = bin

SRCS     = $(SRCDIR)/ftl.c $(SRCDIR)/ssd_controller.c $(SRCDIR)/nvme.c \
           $(SRCDIR)/wear_leveling.c $(SRCDIR)/gc.c $(SRCDIR)/ecc.c

OBJS     = $(SRCS:.c=.o)

DEMOS    = ftl_demo wear_level_demo gc_demo nvme_cmd_demo ecc_demo

.PHONY: all test clean

all: $(DEMOS)

$(OUTDIR):
	if not exist "$(OUTDIR)" mkdir "$(OUTDIR)"

ftl_demo: $(SRCDIR)/ftl.o $(EXDIR)/ftl_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/ftl.o $(EXDIR)/ftl_demo.c -o $(OUTDIR)/ftl_demo $(LDFLAGS)

wear_level_demo: $(SRCDIR)/ftl.o $(SRCDIR)/wear_leveling.o $(EXDIR)/wear_level_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/ftl.o $(SRCDIR)/wear_leveling.o $(EXDIR)/wear_level_demo.c -o $(OUTDIR)/wear_level_demo $(LDFLAGS)

gc_demo: $(SRCDIR)/ftl.o $(SRCDIR)/gc.o $(SRCDIR)/wear_leveling.o $(EXDIR)/gc_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/ftl.o $(SRCDIR)/gc.o $(SRCDIR)/wear_leveling.o $(EXDIR)/gc_demo.c -o $(OUTDIR)/gc_demo $(LDFLAGS)

nvme_cmd_demo: $(SRCDIR)/nvme.o $(EXDIR)/nvme_cmd_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/nvme.o $(EXDIR)/nvme_cmd_demo.c -o $(OUTDIR)/nvme_cmd_demo $(LDFLAGS)

ecc_demo: $(SRCDIR)/ecc.o $(EXDIR)/ecc_demo.c | $(OUTDIR)
	$(CC) $(CFLAGS) $(SRCDIR)/ecc.o $(EXDIR)/ecc_demo.c -o $(OUTDIR)/ecc_demo $(LDFLAGS)

$(SRCDIR)/ftl.o: $(SRCDIR)/ftl.c include/ftl.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/ftl.c -o $(SRCDIR)/ftl.o

$(SRCDIR)/ssd_controller.o: $(SRCDIR)/ssd_controller.c include/ssd_controller.h include/ftl.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/ssd_controller.c -o $(SRCDIR)/ssd_controller.o

$(SRCDIR)/nvme.o: $(SRCDIR)/nvme.c include/nvme.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/nvme.c -o $(SRCDIR)/nvme.o

$(SRCDIR)/wear_leveling.o: $(SRCDIR)/wear_leveling.c include/wear_leveling.h include/ftl.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/wear_leveling.c -o $(SRCDIR)/wear_leveling.o

$(SRCDIR)/gc.o: $(SRCDIR)/gc.c include/gc.h include/ftl.h include/wear_leveling.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/gc.c -o $(SRCDIR)/gc.o

$(SRCDIR)/ecc.o: $(SRCDIR)/ecc.c include/ecc.h
	$(CC) $(CFLAGS) -c $(SRCDIR)/ecc.c -o $(SRCDIR)/ecc.o

test: all
	@echo Running FTL Demo...
	$(OUTDIR)/ftl_demo
	@echo.
	@echo Running Wear Leveling Demo...
	$(OUTDIR)/wear_level_demo
	@echo.
	@echo Running GC Demo...
	$(OUTDIR)/gc_demo
	@echo.
	@echo Running NVMe Command Demo...
	$(OUTDIR)/nvme_cmd_demo
	@echo.
	@echo Running ECC Demo...
	$(OUTDIR)/ecc_demo
	@echo.
	@echo All demos completed.

clean:
	if exist "$(OUTDIR)" rmdir /s /q "$(OUTDIR)"
	if exist "$(SRCDIR)\*.o" del /q "$(SRCDIR)\*.o"
