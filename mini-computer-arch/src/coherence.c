#include "coherence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *coherence_state_name(CoherenceState state)
{
    switch (state) {
    case MESI_M: return "M (Modified)";
    case MESI_E: return "E (Exclusive)";
    case MESI_S: return "S (Shared)";
    case MESI_I: return "I (Invalid)";
    default:     return "UNKNOWN";
    }
}

const char *coherence_protocol_name(CoherenceProtocol protocol)
{
    switch (protocol) {
    case PROTO_MSI:   return "MSI";
    case PROTO_MESI:  return "MESI";
    case PROTO_MOESI: return "MOESI";
    default:          return "UNKNOWN";
    }
}

void coherence_init(CoherenceController *ctrl, CoherenceProtocol protocol,
                    uint32_t num_caches)
{
    if (num_caches > COHERENCE_MAX_CACHES) num_caches = COHERENCE_MAX_CACHES;

    ctrl->protocol = protocol;
    ctrl->num_caches = num_caches;
    ctrl->bus_transactions = 0;
    ctrl->coherence_misses = 0;
    ctrl->invalidations = 0;
    ctrl->writebacks = 0;

    memset(ctrl->caches, 0, sizeof(ctrl->caches));
    memset(ctrl->directory, 0, sizeof(ctrl->directory));

    for (uint32_t i = 0; i < num_caches; i++) {
        ctrl->caches[i].id = i;
        ctrl->caches[i].num_lines = COHERENCE_CACHE_LINES;
        ctrl->caches[i].access_count = 0;
        ctrl->caches[i].pending_messages = 0;
        memset(ctrl->caches[i].lines, 0, sizeof(ctrl->caches[i].lines));
    }
}

static uint32_t cache_line_index(uint32_t address, uint32_t num_lines)
{
    return (address / 64) % num_lines;
}

static bool has_exclusive_mode(CoherenceProtocol protocol)
{
    return protocol == PROTO_MESI || protocol == PROTO_MOESI;
}

bool coherence_read(CoherenceController *ctrl, uint32_t cache_id, uint32_t address,
                    uint8_t *data_out)
{
    if (cache_id >= ctrl->num_caches) return false;

    CoherenceCache *cache = &ctrl->caches[cache_id];
    uint32_t idx = cache_line_index(address, cache->num_lines);
    CoherenceCacheLine *line = &cache->lines[idx];
    uint32_t tag = address / 64;

    cache->access_count++;

    if (line->valid && line->tag == tag) {
        if (data_out) {
            memcpy(data_out, line->data, 64);
        }
        return true;
    }

    ctrl->coherence_misses++;
    ctrl->bus_transactions++;

    int32_t dir_idx = dir_lookup(ctrl, address);
    bool shared_exists = false;

    for (uint32_t i = 0; i < ctrl->num_caches; i++) {
        if (i == cache_id) continue;
        CoherenceCache *other = &ctrl->caches[i];
        uint32_t oidx = cache_line_index(address, other->num_lines);
        if (other->lines[oidx].valid && other->lines[oidx].tag == tag) {
            CoherenceState ost = other->lines[oidx].state;
            if (ost == MESI_M) {
                memcpy(line->data, other->lines[oidx].data, 64);
                other->lines[oidx].state = MESI_S;
                line->state = MESI_S;
                ctrl->writebacks++;

                if (dir_idx >= 0 && dir_idx < COHERENCE_DIR_ENTRIES) {
                    ctrl->directory[dir_idx].state = MESI_S;
                }

                line->valid = true;
                line->tag = tag;

                if (data_out) memcpy(data_out, line->data, 64);
                return true;
            }
            if (ost == MESI_E || ost == MESI_S) {
                shared_exists = true;
                if (ost == MESI_E) {
                    other->lines[oidx].state = MESI_S;
                }
            }
        }
    }

    if (shared_exists && has_exclusive_mode(ctrl->protocol)) {
        line->state = MESI_S;
    } else if (has_exclusive_mode(ctrl->protocol)) {
        line->state = MESI_E;
    } else {
        line->state = MESI_S;
    }

    line->valid = true;
    line->tag = tag;
    memset(line->data, 0, 64);

    if (data_out) memcpy(data_out, line->data, 64);

    dir_update(ctrl, address, line->state, cache_id);

    return false;
}

bool coherence_write(CoherenceController *ctrl, uint32_t cache_id, uint32_t address,
                     const uint8_t *data_in)
{
    if (cache_id >= ctrl->num_caches) return false;

    CoherenceCache *cache = &ctrl->caches[cache_id];
    uint32_t idx = cache_line_index(address, cache->num_lines);
    CoherenceCacheLine *line = &cache->lines[idx];
    uint32_t tag = address / 64;

    cache->access_count++;

    if (line->valid && line->tag == tag &&
        (line->state == MESI_M || line->state == MESI_E)) {
        line->state = MESI_M;
        if (data_in) memcpy(line->data, data_in, 64);
        return true;
    }

    ctrl->coherence_misses++;
    ctrl->bus_transactions++;

    for (uint32_t i = 0; i < ctrl->num_caches; i++) {
        if (i == cache_id) continue;
        CoherenceCache *other = &ctrl->caches[i];
        uint32_t oidx = cache_line_index(address, other->num_lines);
        if (other->lines[oidx].valid && other->lines[oidx].tag == tag) {
            if (other->lines[oidx].state == MESI_M) {
                memcpy(line->data, other->lines[oidx].data, 64);
                ctrl->writebacks++;
            }
            other->lines[oidx].state = MESI_I;
            other->lines[oidx].valid = false;
            ctrl->invalidations++;
        }
    }

    line->valid = true;
    line->tag = tag;
    line->state = MESI_M;

    if (data_in) {
        if (line->valid) {
            memcpy(line->data, data_in, 64);
        } else {
            memset(line->data, 0, 64);
            if (data_in) memcpy(line->data, data_in, 64);
        }
    }

    dir_update(ctrl, address, MESI_M, cache_id);

    return false;
}

void coherence_snoop(CoherenceController *ctrl, uint32_t cache_id,
                     uint32_t address, bool is_write)
{
    if (cache_id >= ctrl->num_caches) return;

    CoherenceCache *cache = &ctrl->caches[cache_id];
    uint32_t idx = cache_line_index(address, cache->num_lines);
    CoherenceCacheLine *line = &cache->lines[idx];
    uint32_t tag = address / 64;

    if (!line->valid || line->tag != tag) return;

    if (is_write) {
        if (line->state == MESI_M || line->state == MESI_E) {
            line->state = MESI_I;
            line->valid = false;
            ctrl->invalidations++;
        }
        if (line->state == MESI_S) {
            line->state = MESI_I;
            line->valid = false;
        }
    }
}

int32_t dir_lookup(const CoherenceController *ctrl, uint32_t address)
{
    uint32_t dir_idx = (address / 64) % COHERENCE_DIR_ENTRIES;

    if (ctrl->directory[dir_idx].tag == (address / 64)) {
        return (int32_t)dir_idx;
    }

    return -1;
}

void dir_update(CoherenceController *ctrl, uint32_t address, CoherenceState state,
                uint32_t cache_id)
{
    uint32_t dir_idx = (address / 64) % COHERENCE_DIR_ENTRIES;

    ctrl->directory[dir_idx].tag = address / 64;
    ctrl->directory[dir_idx].state = state;

    if (cache_id < COHERENCE_MAX_CACHES) {
        ctrl->directory[dir_idx].owner_id = cache_id;

        uint32_t byte_idx = cache_id / 8;
        uint32_t bit_idx = cache_id % 8;

        if (state == MESI_S) {
            ctrl->directory[dir_idx].sharer_bits[byte_idx] |= (1u << bit_idx);
        } else {
            memset(ctrl->directory[dir_idx].sharer_bits, 0,
                   sizeof(ctrl->directory[dir_idx].sharer_bits));
        }
    }
}

void coherence_print_states(const CoherenceController *ctrl)
{
    printf("========================================\n");
    printf("  Cache Coherence States\n");
    printf("========================================\n");
    printf("  Protocol: %s\n", coherence_protocol_name(ctrl->protocol));
    printf("  Caches:   %u\n", ctrl->num_caches);
    printf("----------------------------------------\n");

    for (uint32_t i = 0; i < ctrl->num_caches; i++) {
        const CoherenceCache *cache = &ctrl->caches[i];
        printf("  Cache [%u]:\n", cache->id);

        uint32_t valid_count = 0;
        for (uint32_t j = 0; j < cache->num_lines; j++) {
            if (cache->lines[j].valid) {
                valid_count++;
                printf("    Line %u: Tag=0x%X, State=%s\n",
                       j, cache->lines[j].tag,
                       coherence_state_name(cache->lines[j].state));
            }
        }
        if (valid_count == 0) {
            printf("    (empty)\n");
        }
    }
    printf("========================================\n");
}

void coherence_print_stats(const CoherenceController *ctrl)
{
    printf("========================================\n");
    printf("  Coherence Statistics\n");
    printf("========================================\n");
    printf("  Protocol:         %s\n", coherence_protocol_name(ctrl->protocol));
    printf("  Bus Transactions: %llu\n",
           (unsigned long long)ctrl->bus_transactions);
    printf("  Coherence Misses: %llu\n",
           (unsigned long long)ctrl->coherence_misses);
    printf("  Invalidations:    %llu\n",
           (unsigned long long)ctrl->invalidations);
    printf("  Writebacks:       %llu\n",
           (unsigned long long)ctrl->writebacks);
    printf("========================================\n");
}
