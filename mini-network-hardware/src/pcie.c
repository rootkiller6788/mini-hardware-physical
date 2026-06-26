#include "pcie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

PCIELink *pcie_link_init(int gen, int lanes)
{
    if (gen < 1 || gen > 5 || lanes < 1 || lanes > PCIE_MAX_LANES)
        return NULL;

    PCIELink *link = (PCIELink *)malloc(sizeof(PCIELink));
    if (!link) return NULL;

    memset(link, 0, sizeof(*link));
    link->gen = (PCIEGen)gen;
    link->num_lanes = lanes;
    link->link_width = lanes;
    link->max_payload_size = 256;

    switch (gen) {
        case 1: link->speed = 2.5;  break;
        case 2: link->speed = 5.0;  break;
        case 3: link->speed = 8.0;  break;
        case 4: link->speed = 16.0; break;
        case 5: link->speed = 32.0; break;
        default: link->speed = 2.5; break;
    }

    for (int i = 0; i < lanes; i++) {
        link->lanes[i].gen = gen;
        link->lanes[i].speed_gbps = link->speed;
        link->lanes[i].direction = PCIE_DIR_TX;
    }
    for (int i = 0; i < lanes; i++) {
        link->lanes[lanes + i].gen = gen;
        link->lanes[lanes + i].speed_gbps = link->speed;
        link->lanes[lanes + i].direction = PCIE_DIR_RX;
    }

    return link;
}

void pcie_link_destroy(PCIELink *link)
{
    free(link);
}

double pcie_link_speed_gbps(const PCIELink *link)
{
    if (!link) return 0.0;
    return link->speed;
}

double pcie_link_bandwidth_gbps(const PCIELink *link)
{
    if (!link) return 0.0;
    double encoding_overhead;
    if (link->gen >= 3) {
        encoding_overhead = 128.0 / 130.0;
    } else {
        encoding_overhead = 8.0 / 10.0;
    }
    return link->speed * link->num_lanes * encoding_overhead;
}

TLPPacket pcie_tlp_create_read(uint64_t addr, int len)
{
    TLPPacket tlp;
    memset(&tlp, 0, sizeof(tlp));
    tlp.type = TLP_MEM_READ;
    tlp.requester_id = 0x0001;
    tlp.tag = 0x01;
    tlp.address = addr;
    tlp.length = (uint32_t)(len > PCIE_TLP_MAX_DATA ? PCIE_TLP_MAX_DATA : len);
    tlp.has_data = false;
    return tlp;
}

TLPPacket pcie_tlp_create_write(uint64_t addr, const void *data, int len)
{
    TLPPacket tlp;
    memset(&tlp, 0, sizeof(tlp));
    tlp.type = TLP_MEM_WRITE;
    tlp.requester_id = 0x0001;
    tlp.tag = 0x02;
    tlp.address = addr;

    if (data && len > 0) {
        uint32_t copy_len = (uint32_t)(len > PCIE_TLP_MAX_DATA ? PCIE_TLP_MAX_DATA : len);
        memcpy(tlp.data, data, copy_len);
        tlp.length = copy_len;
        tlp.has_data = true;
    } else {
        tlp.length = 0;
        tlp.has_data = false;
    }
    return tlp;
}

uint32_t pcie_config_space_read(const PCIEConfig *cfg, int offset, int len)
{
    if (!cfg || offset < 0 || offset >= 256) return 0;

    const uint8_t *cfg_bytes = (const uint8_t *)cfg;

    if (len == 1) {
        return (uint32_t)cfg_bytes[offset];
    } else if (len == 2) {
        return (uint32_t)(cfg_bytes[offset] | (cfg_bytes[offset + 1] << 8));
    } else {
        return (uint32_t)(cfg_bytes[offset] |
                         (cfg_bytes[offset + 1] << 8) |
                         (cfg_bytes[offset + 2] << 16) |
                         (cfg_bytes[offset + 3] << 24));
    }
}

void pcie_config_space_write(PCIEConfig *cfg, int offset, uint32_t value)
{
    if (!cfg || offset < 0 || offset >= 256) return;

    uint8_t *cfg_bytes = (uint8_t *)cfg;
    cfg_bytes[offset]     = (uint8_t)(value & 0xFF);
    cfg_bytes[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    cfg_bytes[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    cfg_bytes[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

void pcie_init_config(PCIEConfig *cfg, uint16_t vid, uint16_t did)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->vendor_id = vid;
    cfg->device_id = did;
    cfg->msi_enabled = false;
    cfg->msi_vector = 0;
    for (int i = 0; i < PCIE_BAR_COUNT; i++) {
        cfg->bars[i] = (uint64_t)(0x10000000 + i * 0x100000);
    }
}

void pcie_print_link_info(const PCIELink *link)
{
    if (!link) return;
    printf("=== PCIe Link Info ===\n");
    printf("Generation:       PCIe Gen%d\n", link->gen);
    printf("Lanes:            %d\n", link->num_lanes);
    printf("Link width:       x%d\n", link->link_width);
    printf("Per-lane speed:   %.1f GT/s\n", link->speed);
    printf("Max payload:      %d bytes\n", link->max_payload_size);
    printf("Total bandwidth:  %.2f Gbps\n", pcie_link_bandwidth_gbps(link));
}

void pcie_print_tlp(const TLPPacket *tlp)
{
    if (!tlp) return;
    const char *type_str;
    switch (tlp->type) {
        case TLP_MEM_READ:   type_str = "Memory Read";   break;
        case TLP_MEM_WRITE:  type_str = "Memory Write";  break;
        case TLP_CFG_READ:   type_str = "Config Read";   break;
        case TLP_CFG_WRITE:  type_str = "Config Write";  break;
        case TLP_MSG:        type_str = "Message";       break;
        case TLP_COMPLETION: type_str = "Completion";    break;
        default:             type_str = "Unknown";       break;
    }

    printf("=== TLP Packet ===\n");
    printf("Type:          %s\n", type_str);
    printf("Requester ID:  0x%04X\n", tlp->requester_id);
    printf("Tag:           %u\n", tlp->tag);
    printf("Address:       0x%08llX\n", (unsigned long long)tlp->address);
    printf("Length:        %u bytes\n", tlp->length);
    printf("Has data:      %s\n", tlp->has_data ? "yes" : "no");
    if (tlp->has_data && tlp->length > 0) {
        printf("Data (first 32 bytes hex):\n  ");
        for (uint32_t i = 0; i < 32 && i < tlp->length; i++) {
            printf("%02x ", tlp->data[i]);
            if ((i + 1) % 16 == 0) printf("\n  ");
        }
        printf("\n");
    }
}
