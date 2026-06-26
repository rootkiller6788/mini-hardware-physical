#ifndef SWITCH_FABRIC_H
#define SWITCH_FABRIC_H

#include <stdbool.h>
#include <stdint.h>
#include "mac.h"

#define SWITCH_MAX_PORTS 32
#define SWITCH_MAC_TABLE_SIZE 4096
#define SWITCH_BUFFER_SIZE (64 * 1024)

typedef enum {
    PORT_DOWN = 0,
    PORT_UP   = 1
} SwitchPortState;

typedef enum {
    SCHED_FIFO      = 0,
    SCHED_PRIORITY  = 1,
    SCHED_WRR       = 2
} SchedulingPolicy;

typedef struct {
    uint8_t  mac[MAC_ADDR_LEN];
    int      port_id;
    bool     valid;
} SwitchMacEntry;

typedef struct {
    int              port_id;
    int              speed_gbps;
    SwitchPortState  state;
    SwitchMacEntry   mac_table[SWITCH_MAC_TABLE_SIZE];
    int              mac_table_size;
    uint16_t         vlan_id;
    uint64_t         frames_rx;
    uint64_t         frames_tx;
    uint64_t         bytes_rx;
    uint64_t         bytes_tx;
} SwitchPort;

typedef struct {
    int               num_ports;
    SwitchPort        ports[SWITCH_MAX_PORTS];
    bool              crossbar_matrix[SWITCH_MAX_PORTS][SWITCH_MAX_PORTS];
    uint8_t           buffer[SWITCH_BUFFER_SIZE];
    uint32_t          buffer_used;
    SchedulingPolicy  scheduling_policy;
} SwitchFabric;

SwitchFabric *switch_init(int num_ports);
void          switch_destroy(SwitchFabric *sw);
int           switch_add_port(SwitchFabric *sw, int port_id, int speed);
void          switch_learn_mac(SwitchFabric *sw, const uint8_t *mac, int port);
int           switch_forward(SwitchFabric *sw, const uint8_t *dst_mac,
                             const uint8_t *frame, int frame_len);
void          switch_crossbar_route(SwitchFabric *sw, int src_port, int dst_port);
void          switch_crossbar_clear(SwitchFabric *sw, int src_port);
void          switch_flood(SwitchFabric *sw, int src_port, const uint8_t *frame,
                           int frame_len);
void          switch_print_mac_table(const SwitchFabric *sw);
void          switch_print_port_stats(const SwitchFabric *sw, int port_id);

#endif
