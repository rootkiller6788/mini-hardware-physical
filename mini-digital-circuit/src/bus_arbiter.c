#include "bus_arbiter.h"
#include <stdio.h>
#include <string.h>

BusArbiter bus_arbiter_create(ArbiterType type, int num_masters) {
    BusArbiter arb; arb.type = type;
    arb.num_masters = (num_masters > BUS_MAX_MASTERS) ? BUS_MAX_MASTERS : num_masters;
    if (arb.num_masters < 1) arb.num_masters = 1;
    arb.current_master = -1; arb.tdma_slot = 0; arb.tdma_num_slots = arb.num_masters;
    arb.cycle = 0;
    for (int i = 0; i < arb.num_masters; i++) {
        snprintf(arb.masters[i].name, BUS_MAX_NAME, "M%d", i);
        arb.masters[i].priority = i; arb.masters[i].request = false;
        arb.masters[i].grant = false; arb.masters[i].data = 0;
        arb.masters[i].last_grant_cycle = -1; arb.starvation_count[i] = 0;
    }
    return arb;
}
void bus_arbiter_request(BusArbiter* arb, int master_id) {
    if (arb && master_id >= 0 && master_id < arb->num_masters) arb->masters[master_id].request = true;
}
void bus_arbiter_release(BusArbiter* arb, int master_id) {
    if (arb && master_id >= 0 && master_id < arb->num_masters) {
        arb->masters[master_id].request = false; arb->masters[master_id].grant = false;
    }
}
int bus_arbiter_arbitrate(BusArbiter* arb) {
    if (!arb) return -1;
    for (int i = 0; i < arb->num_masters; i++) if (arb->masters[i].grant) arb->masters[i].grant = false;
    switch (arb->type) {
    case ARB_FIXED_PRIORITY:
        for (int i = 0; i < arb->num_masters; i++) {
            if (arb->masters[i].request) {
                arb->masters[i].grant = true; arb->masters[i].last_grant_cycle = arb->cycle;
                arb->current_master = i; arb->cycle++; return i;
            }
        }
        break;
    case ARB_ROUND_ROBIN: {
        int start = (arb->current_master + 1) % arb->num_masters;
        for (int i = 0; i < arb->num_masters; i++) {
            int m = (start + i) % arb->num_masters;
            if (arb->masters[m].request) {
                arb->masters[m].grant = true; arb->masters[m].last_grant_cycle = arb->cycle;
                arb->current_master = m; arb->cycle++;
                for (int j = 0; j < arb->num_masters; j++)
                    if (j != m && arb->masters[j].request) arb->starvation_count[j]++;
                return m;
            }
        }
        break;
    }
    case ARB_TDMA: {
        int slot = arb->tdma_slot % arb->tdma_num_slots;
        if (slot < arb->num_masters && arb->masters[slot].request) {
            arb->masters[slot].grant = true; arb->masters[slot].last_grant_cycle = arb->cycle;
            arb->current_master = slot; arb->tdma_slot++; arb->cycle++; return slot;
        }
        arb->tdma_slot++; arb->cycle++;
        break;
    }
    }
    arb->cycle++;
    return -1;
}
int bus_arbiter_get_grant(const BusArbiter* arb) { return arb ? arb->current_master : -1; }
void bus_arbiter_set_priority(BusArbiter* arb, int master_id, int prio) {
    if (arb && master_id >= 0 && master_id < arb->num_masters) arb->masters[master_id].priority = prio;
}
void bus_arbiter_tdma_configure(BusArbiter* arb, int num_slots) { if (arb) arb->tdma_num_slots = num_slots; }
void bus_arbiter_print_state(const BusArbiter* arb) {
    if (!arb) return;
    printf("Bus Arbiter (type=%d, masters=%d, cycle=%d)\n", arb->type, arb->num_masters, arb->cycle);
    for (int i = 0; i < arb->num_masters; i++)
        printf("  %s: req=%d gnt=%d prio=%d starve=%d\n", arb->masters[i].name,
               arb->masters[i].request, arb->masters[i].grant, arb->masters[i].priority, arb->starvation_count[i]);
}
int bus_arbiter_get_starvation(const BusArbiter* arb, int master_id) {
    return (arb && master_id >= 0 && master_id < arb->num_masters) ? arb->starvation_count[master_id] : 0;
}
bool bus_arbiter_has_contention(const BusArbiter* arb) {
    if (!arb) return false;
    int grants = 0;
    for (int i = 0; i < arb->num_masters; i++) if (arb->masters[i].grant) grants++;
    return grants > 1;
}

BusSystem bus_system_create(int num_masters) {
    BusSystem bus; memset(&bus, 0, sizeof(bus));
    bus.address_arbiter = bus_arbiter_create(ARB_ROUND_ROBIN, num_masters);
    bus.data_arbiter = bus_arbiter_create(ARB_ROUND_ROBIN, num_masters);
    bus.busy = false; bus.address_bus = 0; bus.data_bus = 0; bus.read_write = true;
    return bus;
}
void bus_system_tick(BusSystem* bus) {
    if (!bus) return;
    if (bus->busy) return;
    bus_arbiter_arbitrate(&bus->address_arbiter);
    bus_arbiter_arbitrate(&bus->data_arbiter);
}
