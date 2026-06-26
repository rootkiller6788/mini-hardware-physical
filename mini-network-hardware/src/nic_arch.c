#include "nic_arch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void nic_init(NIC *nic, const uint8_t *mac, uint32_t ip)
{
    memset(nic, 0, sizeof(*nic));
    memcpy(nic->mac_addr, mac, NIC_MAC_ADDR_LEN);
    nic->ip_addr = ip;
    nic->tx_ring.head = 0;
    nic->tx_ring.tail = 0;
    nic->tx_ring.len = NIC_MAX_DESCRIPTORS;
    nic->tx_ring.base_addr = 0x10000000;
    nic->rx_ring.head = 0;
    nic->rx_ring.tail = 0;
    nic->dma_engine_active = false;
    nic->dma_total_bytes = 0;
    nic->num_registers = 0;
    for (int i = 0; i < NIC_MAX_REGISTERS; i++) {
        nic->registers[i].offset = 0;
        nic->registers[i].width = 0;
        nic->registers[i].value = 0;
        nic->registers[i].name = NULL;
    }
}

int nic_tx_enqueue(NIC *nic, const void *data, int len)
{
    if (len <= 0 || data == NULL) return -1;

    uint32_t next_tail = (nic->tx_ring.tail + 1) % nic->tx_ring.len;
    if (next_tail == nic->tx_ring.head) {
        return -1;
    }

    NICDescriptor *desc = &nic->tx_ring.descriptors[nic->tx_ring.tail];
    desc->buffer_addr = (uint64_t)(uintptr_t)data;
    desc->length = (uint32_t)len;
    desc->flags = NIC_FLAG_SOP | NIC_FLAG_EOP;
    desc->status = 0;

    nic->tx_ring.tail = next_tail;
    return 0;
}

int nic_rx_enqueue(NIC *nic, const void *data, int len)
{
    if (len <= 0 || data == NULL) return -1;

    uint32_t next_tail = (nic->rx_ring.tail + 1) % NIC_MAX_DESCRIPTORS;
    if (next_tail == nic->rx_ring.head) {
        return -1;
    }

    NICDescriptor *desc = &nic->rx_ring.descriptors[nic->rx_ring.tail];
    desc->buffer_addr = (uint64_t)(uintptr_t)data;
    desc->length = (uint32_t)len;
    desc->flags = NIC_FLAG_SOP | NIC_FLAG_EOP;
    desc->status = 0;

    nic->rx_ring.tail = next_tail;
    return 0;
}

void nic_process(NIC *nic)
{
    if (!nic->dma_engine_active) return;

    while (nic->tx_ring.head != nic->tx_ring.tail) {
        NICDescriptor *desc = &nic->tx_ring.descriptors[nic->tx_ring.head];
        nic->dma_total_bytes += desc->length;
        desc->status = 0x01;
        nic->tx_ring.head = (nic->tx_ring.head + 1) % nic->tx_ring.len;
    }

    while (nic->rx_ring.head != nic->rx_ring.tail) {
        NICDescriptor *desc = &nic->rx_ring.descriptors[nic->rx_ring.head];
        nic->dma_total_bytes += desc->length;
        desc->status = 0x01;
        nic->rx_ring.head = (nic->rx_ring.head + 1) % NIC_MAX_DESCRIPTORS;
    }

    if (nic->tx_ring.head == nic->tx_ring.tail) {
        nic->interrupts[NIC_IRQ_TX_COMPLETE] = true;
    }
    if (nic->rx_ring.head != nic->rx_ring.tail) {
        nic->interrupts[NIC_IRQ_RX_READY] = true;
    }
}

void nic_interrupt_handler(NIC *nic, int irq)
{
    if (irq < 0 || irq >= NIC_IRQ_COUNT) return;

    switch ((NICInterrupt)irq) {
    case NIC_IRQ_TX_COMPLETE:
        nic->interrupts[NIC_IRQ_TX_COMPLETE] = false;
        nic->dma_engine_active = false;
        break;
    case NIC_IRQ_RX_READY:
        nic->interrupts[NIC_IRQ_RX_READY] = false;
        break;
    case NIC_IRQ_LINK_CHANGE:
        nic->interrupts[NIC_IRQ_LINK_CHANGE] = false;
        break;
    case NIC_IRQ_ERROR:
        nic->interrupts[NIC_IRQ_ERROR] = false;
        break;
    default:
        break;
    }
}

void nic_dump_state(const NIC *nic)
{
    printf("=== NIC State ===\n");
    printf("MAC addr: %02x:%02x:%02x:%02x:%02x:%02x\n",
           nic->mac_addr[0], nic->mac_addr[1], nic->mac_addr[2],
           nic->mac_addr[3], nic->mac_addr[4], nic->mac_addr[5]);
    printf("IP addr:  %u.%u.%u.%u\n",
           (nic->ip_addr >> 24) & 0xFF, (nic->ip_addr >> 16) & 0xFF,
           (nic->ip_addr >> 8) & 0xFF, nic->ip_addr & 0xFF);
    printf("TX Ring:  head=%u tail=%u len=%u\n",
           nic->tx_ring.head, nic->tx_ring.tail, nic->tx_ring.len);
    printf("RX Ring:  head=%u tail=%u\n",
           nic->rx_ring.head, nic->rx_ring.tail);
    printf("DMA:      active=%d total_bytes=%llu\n",
           nic->dma_engine_active,
           (unsigned long long)nic->dma_total_bytes);
    printf("IRQs:     tx_complete=%d rx_ready=%d link_change=%d error=%d\n",
           nic->interrupts[NIC_IRQ_TX_COMPLETE],
           nic->interrupts[NIC_IRQ_RX_READY],
           nic->interrupts[NIC_IRQ_LINK_CHANGE],
           nic->interrupts[NIC_IRQ_ERROR]);
}
