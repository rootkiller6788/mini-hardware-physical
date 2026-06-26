#ifndef BUS_ARBITER_H
#define BUS_ARBITER_H
#include <stdbool.h>
#include <stdint.h>

#define BUS_MAX_MASTERS 16
#define BUS_MAX_NAME 32

typedef enum { ARB_FIXED_PRIORITY, ARB_ROUND_ROBIN, ARB_TDMA } ArbiterType;

typedef struct {
    char name[BUS_MAX_NAME];
    int priority;
    bool request;
    bool grant;
    uint32_t data;
    int last_grant_cycle;
} BusMaster;

typedef struct {
    ArbiterType type;
    BusMaster masters[BUS_MAX_MASTERS];
    int num_masters;
    int current_master;
    int tdma_slot;
    int tdma_num_slots;
    int cycle;
    int starvation_count[BUS_MAX_MASTERS];
} BusArbiter;

BusArbiter bus_arbiter_create(ArbiterType type, int num_masters);
void bus_arbiter_request(BusArbiter* arb, int master_id);
void bus_arbiter_release(BusArbiter* arb, int master_id);
int bus_arbiter_arbitrate(BusArbiter* arb);
int bus_arbiter_get_grant(const BusArbiter* arb);
void bus_arbiter_set_priority(BusArbiter* arb, int master_id, int prio);
void bus_arbiter_tdma_configure(BusArbiter* arb, int num_slots);
void bus_arbiter_print_state(const BusArbiter* arb);
int bus_arbiter_get_starvation(const BusArbiter* arb, int master_id);
bool bus_arbiter_has_contention(const BusArbiter* arb);

typedef struct {
    BusArbiter address_arbiter;
    BusArbiter data_arbiter;
    uint32_t address_bus;
    uint32_t data_bus;
    bool read_write;
    bool busy;
} BusSystem;

BusSystem bus_system_create(int num_masters);
void bus_system_tick(BusSystem* bus);

#endif
