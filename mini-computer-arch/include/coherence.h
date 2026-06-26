#ifndef COHERENCE_H
#define COHERENCE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define COHERENCE_MAX_CACHES 16
#define COHERENCE_DIR_ENTRIES 4096
#define COHERENCE_CACHE_LINES 256

typedef enum {
    MESI_M,
    MESI_E,
    MESI_S,
    MESI_I
} CoherenceState;

typedef enum {
    PROTO_MSI,
    PROTO_MESI,
    PROTO_MOESI
} CoherenceProtocol;

typedef struct {
    uint32_t tag;
    CoherenceState state;
    bool valid;
    uint8_t data[64];
} CoherenceCacheLine;

typedef struct {
    uint32_t tag;
    CoherenceState state;
    uint8_t sharer_bits[COHERENCE_MAX_CACHES / 8];
    uint32_t owner_id;
} DirectoryEntry;

typedef struct {
    CoherenceCacheLine lines[COHERENCE_CACHE_LINES];
    uint32_t num_lines;
    uint32_t id;
    uint64_t access_count;
    uint64_t pending_messages;
} CoherenceCache;

typedef struct {
    CoherenceProtocol protocol;
    CoherenceCache caches[COHERENCE_MAX_CACHES];
    uint32_t num_caches;
    DirectoryEntry directory[COHERENCE_DIR_ENTRIES];
    uint64_t bus_transactions;
    uint64_t coherence_misses;
    uint64_t invalidations;
    uint64_t writebacks;
} CoherenceController;

void coherence_init(CoherenceController *ctrl, CoherenceProtocol protocol,
                    uint32_t num_caches);
bool coherence_read(CoherenceController *ctrl, uint32_t cache_id, uint32_t address,
                    uint8_t *data_out);
bool coherence_write(CoherenceController *ctrl, uint32_t cache_id, uint32_t address,
                     const uint8_t *data_in);
void coherence_snoop(CoherenceController *ctrl, uint32_t cache_id,
                     uint32_t address, bool is_write);
int32_t dir_lookup(const CoherenceController *ctrl, uint32_t address);
void dir_update(CoherenceController *ctrl, uint32_t address, CoherenceState state,
                uint32_t cache_id);
void coherence_print_states(const CoherenceController *ctrl);
void coherence_print_stats(const CoherenceController *ctrl);
const char *coherence_state_name(CoherenceState state);
const char *coherence_protocol_name(CoherenceProtocol protocol);

#endif /* COHERENCE_H */
