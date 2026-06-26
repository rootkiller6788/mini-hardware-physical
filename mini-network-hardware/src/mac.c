#include "mac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int mac_addr_parse(const char *str, uint8_t *out)
{
    if (!str || !out) return -1;

    unsigned int bytes[6];
    int matched = sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
                         &bytes[0], &bytes[1], &bytes[2],
                         &bytes[3], &bytes[4], &bytes[5]);
    if (matched != 6) return -1;

    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)bytes[i];
    }
    return 0;
}

void mac_addr_to_str(const uint8_t *mac, char *out)
{
    sprintf(out, "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void mac_frame_build(MACFrame *f, const uint8_t *dst, const uint8_t *src,
                     uint16_t ethertype, const uint8_t *payload, int len)
{
    memset(f, 0, sizeof(*f));
    memcpy(f->dst_mac, dst, MAC_ADDR_LEN);
    memcpy(f->src_mac, src, MAC_ADDR_LEN);
    f->ethertype = ethertype;
    if (payload && len > 0 && len <= ETHERNET_MTU) {
        memcpy(f->payload, payload, (size_t)len);
        f->payload_len = (uint16_t)len;
    } else if (len > ETHERNET_MTU) {
        memcpy(f->payload, payload, ETHERNET_MTU);
        f->payload_len = ETHERNET_MTU;
    } else {
        f->payload_len = 0;
    }

    int crc_len = MAC_ADDR_LEN * 2 + 2 + f->payload_len;
    uint8_t *crc_buf = (uint8_t *)malloc((size_t)crc_len);
    if (crc_buf) {
        int pos = 0;
        memcpy(crc_buf + pos, f->dst_mac, MAC_ADDR_LEN); pos += MAC_ADDR_LEN;
        memcpy(crc_buf + pos, f->src_mac, MAC_ADDR_LEN); pos += MAC_ADDR_LEN;
        crc_buf[pos++] = (uint8_t)(f->ethertype >> 8);
        crc_buf[pos++] = (uint8_t)(f->ethertype & 0xFF);
        if (f->payload_len > 0) {
            memcpy(crc_buf + pos, f->payload, f->payload_len);
            pos += f->payload_len;
        }
        f->fcs = mac_crc32(crc_buf, pos);
        free(crc_buf);
    }
}

uint32_t mac_crc32(const uint8_t *data, int len)
{
    uint32_t crc = 0xFFFFFFFF;
    static uint32_t table[256];
    static int table_initialized = 0;

    if (!table_initialized) {
        for (int i = 0; i < 256; i++) {
            uint32_t c = (uint32_t)i;
            for (int j = 0; j < 8; j++) {
                if (c & 1) {
                    c = MAC_CRC32_POLY ^ (c >> 1);
                } else {
                    c = c >> 1;
                }
            }
            table[i] = c;
        }
        table_initialized = 1;
    }

    for (int i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

bool mac_frame_check(const MACFrame *f)
{
    int crc_len = MAC_ADDR_LEN * 2 + 2 + f->payload_len;
    uint8_t *crc_buf = (uint8_t *)malloc((size_t)crc_len);
    if (!crc_buf) return false;

    int pos = 0;
    memcpy(crc_buf + pos, f->dst_mac, MAC_ADDR_LEN); pos += MAC_ADDR_LEN;
    memcpy(crc_buf + pos, f->src_mac, MAC_ADDR_LEN); pos += MAC_ADDR_LEN;
    crc_buf[pos++] = (uint8_t)(f->ethertype >> 8);
    crc_buf[pos++] = (uint8_t)(f->ethertype & 0xFF);
    if (f->payload_len > 0) {
        memcpy(crc_buf + pos, f->payload, f->payload_len);
        pos += f->payload_len;
    }

    uint32_t computed = mac_crc32(crc_buf, pos);
    free(crc_buf);
    return computed == f->fcs;
}

void mac_print_frame(const MACFrame *f)
{
    printf("=== MAC Frame ===\n");
    printf("Dst MAC:  %02x:%02x:%02x:%02x:%02x:%02x\n",
           f->dst_mac[0], f->dst_mac[1], f->dst_mac[2],
           f->dst_mac[3], f->dst_mac[4], f->dst_mac[5]);
    printf("Src MAC:  %02x:%02x:%02x:%02x:%02x:%02x\n",
           f->src_mac[0], f->src_mac[1], f->src_mac[2],
           f->src_mac[3], f->src_mac[4], f->src_mac[5]);
    printf("EtherType: 0x%04X\n", f->ethertype);
    printf("Payload len: %u\n", f->payload_len);
    printf("FCS (CRC32): 0x%08X\n", f->fcs);
    if (f->payload_len > 0) {
        printf("Payload hex dump (first 64 bytes):\n  ");
        int show = f->payload_len < 64 ? f->payload_len : 64;
        for (int i = 0; i < show; i++) {
            printf("%02x ", f->payload[i]);
            if ((i + 1) % 16 == 0 && i + 1 < show)
                printf("\n  ");
        }
        printf("\n");
    }
}

void mac_stats_init(MACStats *stats)
{
    memset(stats, 0, sizeof(*stats));
}

void mac_stats_record_tx(MACStats *stats)  { stats->frames_tx++; }
void mac_stats_record_rx(MACStats *stats)  { stats->frames_rx++; }
void mac_stats_record_collision(MACStats *stats) { stats->collisions++; }
void mac_stats_record_error(MACStats *stats)     { stats->errors++; }

void mac_stats_print(const MACStats *stats)
{
    printf("=== MAC Stats ===\n");
    printf("Frames TX:    %llu\n", (unsigned long long)stats->frames_tx);
    printf("Frames RX:    %llu\n", (unsigned long long)stats->frames_rx);
    printf("Collisions:   %llu\n", (unsigned long long)stats->collisions);
    printf("Errors:       %llu\n", (unsigned long long)stats->errors);
}
