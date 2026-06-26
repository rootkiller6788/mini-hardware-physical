#ifndef NIC_ARCH_H
#define NIC_ARCH_H

#include <stdbool.h>
#include <stdint.h>

#define NIC_MAX_DESCRIPTORS 256
#define NIC_MAX_REGISTERS 32
#define NIC_MAC_ADDR_LEN 6

#define NIC_FLAG_EOP         0x01
#define NIC_FLAG_SOP         0x02
#define NIC_FLAG_CSUM_OFFLOAD 0x04
#define NIC_FLAG_TSO          0x08

typedef struct {
    uint32_t offset;
    uint32_t width;
    uint32_t value;
    const char *name;
} NICRegister;

typedef struct {
    uint64_t buffer_addr;
    uint32_t length;
    uint16_t flags;
    uint16_t status;
} NICDescriptor;

typedef struct {
    NICDescriptor descriptors[NIC_MAX_DESCRIPTORS];
    uint32_t head;
    uint32_t tail;
    uint64_t base_addr;
    uint32_t len;
} NICTxRing;

typedef struct {
    NICDescriptor descriptors[NIC_MAX_DESCRIPTORS];
    uint32_t head;
    uint32_t tail;
} NICRxRing;

typedef enum {
    NIC_IRQ_TX_COMPLETE = 0,
    NIC_IRQ_RX_READY    = 1,
    NIC_IRQ_LINK_CHANGE = 2,
    NIC_IRQ_ERROR       = 3,
    NIC_IRQ_COUNT       = 4
} NICInterrupt;

typedef struct {
    uint8_t  mac_addr[NIC_MAC_ADDR_LEN];
    uint32_t ip_addr;
    NICTxRing tx_ring;
    NICRxRing rx_ring;
    NICRegister registers[NIC_MAX_REGISTERS];
    uint32_t num_registers;
    bool     interrupts[NIC_IRQ_COUNT];
    bool     dma_engine_active;
    uint64_t dma_total_bytes;
} NIC;

void nic_init(NIC *nic, const uint8_t *mac, uint32_t ip);
int  nic_tx_enqueue(NIC *nic, const void *data, int len);
int  nic_rx_enqueue(NIC *nic, const void *data, int len);
void nic_process(NIC *nic);
void nic_interrupt_handler(NIC *nic, int irq);
void nic_dump_state(const NIC *nic);

#endif
