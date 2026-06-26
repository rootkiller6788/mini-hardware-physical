#ifndef OFFLOAD_H
#define OFFLOAD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OFFLOAD_TSO   = 0,
    OFFLOAD_LRO   = 1,
    OFFLOAD_CSUM  = 2,
    OFFLOAD_IPSEC = 3,
    OFFLOAD_TLS   = 4,
    OFFLOAD_GRO   = 5,
    OFFLOAD_COUNT = 6
} OffloadType;

typedef struct {
    OffloadType type;
    bool        enabled;
    uint64_t    packets_offloaded;
    uint64_t    bytes_saved;
} OffloadEngine;

OffloadEngine *offload_engine_create(OffloadType type);
void           offload_engine_destroy(OffloadEngine *eng);
int            offload_tso_segment(OffloadEngine *eng, const uint8_t *packet,
                                   int len, int mss, uint8_t **segments,
                                   int *n_segments);
bool           offload_checksum_verify(OffloadEngine *eng,
                                       const uint8_t *data, int len,
                                       uint16_t expected_csum);
uint16_t       offload_csum_compute(const uint8_t *data, int len);
void           offload_lro_merge(OffloadEngine *eng, uint8_t **packets,
                                 int n_packets, uint8_t *merged, int *merged_len);
void           offload_print_stats(const OffloadEngine *eng);
const char    *offload_type_name(OffloadType type);

#endif
