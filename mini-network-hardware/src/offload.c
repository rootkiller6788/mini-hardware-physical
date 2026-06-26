#include "offload.h"
#include "mac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

OffloadEngine *offload_engine_create(OffloadType type)
{
    OffloadEngine *eng = (OffloadEngine *)malloc(sizeof(OffloadEngine));
    if (!eng) return NULL;
    eng->type = type;
    eng->enabled = true;
    eng->packets_offloaded = 0;
    eng->bytes_saved = 0;
    return eng;
}

void offload_engine_destroy(OffloadEngine *eng)
{
    free(eng);
}

uint16_t offload_csum_compute(const uint8_t *data, int len)
{
    uint32_t sum = 0;
    int i = 0;

    while (i < len - 1) {
        sum += (uint16_t)((data[i] << 8) | data[i + 1]);
        i += 2;
    }
    if (i < len) {
        sum += (uint16_t)(data[i] << 8);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

bool offload_checksum_verify(OffloadEngine *eng, const uint8_t *data,
                              int len, uint16_t expected_csum)
{
    (void)eng;
    uint16_t computed = offload_csum_compute(data, len);
    return computed == expected_csum;
}

int offload_tso_segment(OffloadEngine *eng, const uint8_t *packet,
                        int len, int mss, uint8_t **segments, int *n_segments)
{
    if (!eng || !packet || len <= 0 || mss <= 0 || !segments || !n_segments)
        return -1;

    *n_segments = (len + mss - 1) / mss;
    *segments = (uint8_t *)malloc((size_t)(*n_segments * mss));
    if (!*segments) return -1;

    for (int i = 0; i < *n_segments; i++) {
        int offset = i * mss;
        int remain = len - offset;
        int seg_len = remain < mss ? remain : mss;
        memcpy(*segments + offset, packet + offset, (size_t)seg_len);
        if (seg_len < mss) {
            memset(*segments + offset + seg_len, 0, (size_t)(mss - seg_len));
        }
    }

    eng->packets_offloaded++;
    eng->bytes_saved += (uint64_t)len;
    return 0;
}

void offload_lro_merge(OffloadEngine *eng, uint8_t **packets,
                       int n_packets, uint8_t *merged, int *merged_len)
{
    (void)eng;
    *merged_len = 0;
    for (int i = 0; i < n_packets && packets[i]; i++) {
        memcpy(merged + *merged_len, packets[i], ETHERNET_MTU);
        *merged_len += ETHERNET_MTU;
    }
    eng->packets_offloaded++;
    eng->bytes_saved += (uint64_t)(*merged_len);
}

void offload_print_stats(const OffloadEngine *eng)
{
    if (!eng) return;
    printf("=== Offload Engine: %s ===\n", offload_type_name(eng->type));
    printf("Enabled:           %s\n", eng->enabled ? "yes" : "no");
    printf("Packets offloaded: %llu\n", (unsigned long long)eng->packets_offloaded);
    printf("Bytes saved:       %llu\n", (unsigned long long)eng->bytes_saved);
}

const char *offload_type_name(OffloadType type)
{
    switch (type) {
        case OFFLOAD_TSO:   return "TSO (TCP Segmentation Offload)";
        case OFFLOAD_LRO:   return "LRO (Large Receive Offload)";
        case OFFLOAD_CSUM:  return "Checksum Offload";
        case OFFLOAD_IPSEC: return "IPsec Offload";
        case OFFLOAD_TLS:   return "TLS Offload";
        case OFFLOAD_GRO:   return "GRO (Generic Receive Offload)";
        default:            return "Unknown";
    }
}
