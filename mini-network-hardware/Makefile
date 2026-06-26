CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include
LDFLAGS  = -lm
SRCDIR   = src
EXDIR    = examples
OUTDIR   = bin
DEMODIR  = demos

SRCS = $(SRCDIR)/nic_arch.c \
       $(SRCDIR)/mac.c \
       $(SRCDIR)/rdma.c \
       $(SRCDIR)/offload.c \
       $(SRCDIR)/switch_fabric.c \
       $(SRCDIR)/pcie.c \
       $(SRCDIR)/serdes.c \
       $(SRCDIR)/flow.c \
       $(SRCDIR)/timestamp.c \
       $(SRCDIR)/qos.c

OBJS = $(SRCS:.c=.o)
DEPS = include/nic_arch.h include/mac.h include/rdma.h \
       include/offload.h include/switch_fabric.h include/pcie.h \
       include/serdes.h include/flow.h include/timestamp.h include/qos.h

.PHONY: all clean test dirs \
        mac_demo rdma_demo switch_sim_demo checksum_offload_demo \
        serdes_demo flow_demo network_test

all: dirs mac_demo rdma_demo switch_sim_demo checksum_offload_demo \
     serdes_demo flow_demo network_test

dirs:
	@if [ ! -d "$(OUTDIR)" ]; then mkdir -p "$(OUTDIR)"; fi

$(SRCDIR)/nic_arch.o: $(SRCDIR)/nic_arch.c include/nic_arch.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRCDIR)/mac.o: $(SRCDIR)/mac.c include/mac.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRCDIR)/rdma.o: $(SRCDIR)/rdma.c include/rdma.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRCDIR)/offload.o: $(SRCDIR)/offload.c include/offload.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRCDIR)/switch_fabric.o: $(SRCDIR)/switch_fabric.c include/switch_fabric.h include/mac.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRCDIR)/pcie.o: $(SRCDIR)/pcie.c include/pcie.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRCDIR)/serdes.o: $(SRCDIR)/serdes.c include/serdes.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRCDIR)/flow.o: $(SRCDIR)/flow.c include/flow.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRCDIR)/timestamp.o: $(SRCDIR)/timestamp.c include/timestamp.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(SRCDIR)/qos.o: $(SRCDIR)/qos.c include/qos.h
	$(CC) $(CFLAGS) -c -o $@ $<

mac_demo: $(SRCDIR)/mac.o $(SRCDIR)/offload.o
	$(CC) $(CFLAGS) -o $(OUTDIR)/mac_demo $(EXDIR)/mac_demo.c $(SRCDIR)/mac.o $(LDFLAGS)

rdma_demo: $(SRCDIR)/rdma.o
	$(CC) $(CFLAGS) -o $(OUTDIR)/rdma_demo $(EXDIR)/rdma_demo.c $(SRCDIR)/rdma.o $(LDFLAGS)

switch_sim_demo: $(SRCDIR)/switch_fabric.o $(SRCDIR)/mac.o
	$(CC) $(CFLAGS) -o $(OUTDIR)/switch_sim_demo $(EXDIR)/switch_sim_demo.c \
	$(SRCDIR)/switch_fabric.o $(SRCDIR)/mac.o $(LDFLAGS)

checksum_offload_demo: $(SRCDIR)/offload.o
	$(CC) $(CFLAGS) -o $(OUTDIR)/checksum_offload_demo $(EXDIR)/checksum_offload_demo.c \
	$(SRCDIR)/offload.o $(LDFLAGS)

serdes_demo: $(SRCDIR)/serdes.o
	$(CC) $(CFLAGS) -o $(OUTDIR)/serdes_demo $(EXDIR)/serdes_demo.c \
	$(SRCDIR)/serdes.o $(LDFLAGS)

flow_demo: $(SRCDIR)/flow.o $(SRCDIR)/mac.o
	$(CC) $(CFLAGS) -o $(OUTDIR)/flow_demo $(EXDIR)/flow_demo.c \
	$(SRCDIR)/flow.o $(SRCDIR)/mac.o $(LDFLAGS)

network_test: $(SRCDIR)/nic_arch.o $(SRCDIR)/mac.o $(SRCDIR)/rdma.o \
              $(SRCDIR)/offload.o $(SRCDIR)/switch_fabric.o $(SRCDIR)/pcie.o \
              $(SRCDIR)/serdes.o $(SRCDIR)/flow.o $(SRCDIR)/timestamp.o \
              $(SRCDIR)/qos.o
	$(CC) $(CFLAGS) -o $(OUTDIR)/network_test tests/test_runner.c \
	$(SRCDIR)/nic_arch.o $(SRCDIR)/mac.o $(SRCDIR)/rdma.o \
	$(SRCDIR)/offload.o $(SRCDIR)/switch_fabric.o $(SRCDIR)/pcie.o \
	$(SRCDIR)/serdes.o $(SRCDIR)/flow.o $(SRCDIR)/timestamp.o \
	$(SRCDIR)/qos.o $(LDFLAGS)

test: all
	@echo "=== Running MAC Demo ==="
	@"./$(OUTDIR)/mac_demo"
	@echo ""
	@echo "=== Running RDMA Demo ==="
	@"./$(OUTDIR)/rdma_demo"
	@echo ""
	@echo "=== Running Switch Sim Demo ==="
	@"./$(OUTDIR)/switch_sim_demo"
	@echo ""
	@echo "=== Running Checksum Offload Demo ==="
	@"./$(OUTDIR)/checksum_offload_demo"
	@echo ""
	@echo "=== Running SerDes Demo ==="
	@"./$(OUTDIR)/serdes_demo"
	@echo ""
	@echo "=== Running Flow Control Demo ==="
	@"./$(OUTDIR)/flow_demo"
	@echo ""
	@echo "=== Running Network Test Suite ==="
	@"./$(OUTDIR)/network_test"
	@echo ""
	@echo "=== All tests complete ==="

clean:
	@rm -f "$(SRCDIR)"/*.o
	@rm -rf "$(OUTDIR)"
