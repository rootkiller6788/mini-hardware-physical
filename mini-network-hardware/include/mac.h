#ifndef MAC_H
#define MAC_H

#include <stdbool.h>
#include <stdint.h>

#define ETHERNET_MTU  1500
#define MAC_ADDR_LEN  6
#define MAC_CRC32_POLY 0xEDB88320

typedef struct {
    uint8_t  dst_mac[MAC_ADDR_LEN];
    uint8_t  src_mac[MAC_ADDR_LEN];
    uint16_t ethertype;
    uint8_t  payload[ETHERNET_MTU];
    uint16_t payload_len;
    uint32_t fcs;
} MACFrame;

typedef struct {
    uint64_t frames_tx;
    uint64_t frames_rx;
    uint64_t collisions;
    uint64_t errors;
} MACStats;

int      mac_addr_parse(const char *str, uint8_t *out);
void     mac_addr_to_str(const uint8_t *mac, char *out);
void     mac_frame_build(MACFrame *f, const uint8_t *dst, const uint8_t *src,
                         uint16_t ethertype, const uint8_t *payload, int len);
uint32_t mac_crc32(const uint8_t *data, int len);
bool     mac_frame_check(const MACFrame *f);
void     mac_print_frame(const MACFrame *f);
void     mac_stats_init(MACStats *stats);
void     mac_stats_record_tx(MACStats *stats);
void     mac_stats_record_rx(MACStats *stats);
void     mac_stats_record_collision(MACStats *stats);
void     mac_stats_record_error(MACStats *stats);
void     mac_stats_print(const MACStats *stats);

#endif
