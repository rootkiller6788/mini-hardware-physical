#include "switch_fabric.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

SwitchFabric *switch_init(int num_ports)
{
    if (num_ports < 1 || num_ports > SWITCH_MAX_PORTS) return NULL;

    SwitchFabric *sw = (SwitchFabric *)malloc(sizeof(SwitchFabric));
    if (!sw) return NULL;

    memset(sw, 0, sizeof(*sw));
    sw->num_ports = num_ports;
    sw->buffer_used = 0;
    sw->scheduling_policy = SCHED_FIFO;

    for (int i = 0; i < SWITCH_MAX_PORTS; i++) {
        sw->ports[i].port_id = -1;
        sw->ports[i].state = PORT_DOWN;
        sw->ports[i].mac_table_size = 0;
        for (int j = 0; j < SWITCH_MAC_TABLE_SIZE; j++) {
            sw->ports[i].mac_table[j].valid = false;
        }
        for (int j = 0; j < SWITCH_MAX_PORTS; j++) {
            sw->crossbar_matrix[i][j] = false;
        }
    }

    return sw;
}

void switch_destroy(SwitchFabric *sw)
{
    free(sw);
}

int switch_add_port(SwitchFabric *sw, int port_id, int speed)
{
    if (port_id < 0 || port_id >= SWITCH_MAX_PORTS) return -1;
    if (sw->ports[port_id].state == PORT_UP) return -1;

    sw->ports[port_id].port_id = port_id;
    sw->ports[port_id].speed_gbps = speed;
    sw->ports[port_id].state = PORT_UP;
    sw->ports[port_id].vlan_id = 0;
    sw->ports[port_id].frames_rx = 0;
    sw->ports[port_id].frames_tx = 0;
    sw->ports[port_id].bytes_rx = 0;
    sw->ports[port_id].bytes_tx = 0;
    sw->ports[port_id].mac_table_size = 0;
    return 0;
}

void switch_learn_mac(SwitchFabric *sw, const uint8_t *mac, int port)
{
    if (!sw || !mac || port < 0 || port >= sw->num_ports) return;
    if (sw->ports[port].state != PORT_UP) return;

    SwitchPort *p = &sw->ports[port];
    for (int i = 0; i < p->mac_table_size; i++) {
        if (p->mac_table[i].valid &&
            memcmp(p->mac_table[i].mac, mac, MAC_ADDR_LEN) == 0) {
            p->mac_table[i].port_id = port;
            return;
        }
    }

    if (p->mac_table_size < SWITCH_MAC_TABLE_SIZE) {
        int idx = p->mac_table_size++;
        memcpy(p->mac_table[idx].mac, mac, MAC_ADDR_LEN);
        p->mac_table[idx].port_id = port;
        p->mac_table[idx].valid = true;
    }
}

int switch_forward(SwitchFabric *sw, const uint8_t *dst_mac,
                   const uint8_t *frame, int frame_len)
{
    if (!sw || !dst_mac || !frame) return -1;

    for (int p = 0; p < sw->num_ports; p++) {
        if (sw->ports[p].state != PORT_UP) continue;
        for (int e = 0; e < sw->ports[p].mac_table_size; e++) {
            if (sw->ports[p].mac_table[e].valid &&
                memcmp(sw->ports[p].mac_table[e].mac, dst_mac, MAC_ADDR_LEN) == 0) {
                int dst_port = sw->ports[p].mac_table[e].port_id;
                sw->ports[dst_port].frames_tx++;
                sw->ports[dst_port].bytes_tx += (uint64_t)frame_len;
                return dst_port;
            }
        }
    }

    return -1;
}

void switch_crossbar_route(SwitchFabric *sw, int src_port, int dst_port)
{
    if (src_port < 0 || src_port >= SWITCH_MAX_PORTS) return;
    if (dst_port < 0 || dst_port >= SWITCH_MAX_PORTS) return;
    sw->crossbar_matrix[src_port][dst_port] = true;
}

void switch_crossbar_clear(SwitchFabric *sw, int src_port)
{
    if (src_port < 0 || src_port >= SWITCH_MAX_PORTS) return;
    for (int i = 0; i < SWITCH_MAX_PORTS; i++) {
        sw->crossbar_matrix[src_port][i] = false;
    }
}

void switch_flood(SwitchFabric *sw, int src_port,
                  const uint8_t *frame, int frame_len)
{
    if (!sw || src_port < 0 || src_port >= sw->num_ports) return;
    (void)frame; /* Frame content not inspected during flooding */

    for (int p = 0; p < sw->num_ports; p++) {
        if (p == src_port) continue;
        if (sw->ports[p].state == PORT_UP) {
            sw->ports[p].frames_tx++;
            sw->ports[p].bytes_tx += (uint64_t)frame_len;
        }
    }
}

void switch_print_mac_table(const SwitchFabric *sw)
{
    if (!sw) return;
    printf("=== Switch MAC Address Table ===\n");
    printf("%-25s %-10s\n", "MAC Address", "Port");
    printf("------------------------------------\n");
    for (int p = 0; p < sw->num_ports; p++) {
        if (sw->ports[p].state != PORT_UP) continue;
        for (int e = 0; e < sw->ports[p].mac_table_size; e++) {
            if (sw->ports[p].mac_table[e].valid) {
                printf("%02x:%02x:%02x:%02x:%02x:%02x    %d\n",
                       sw->ports[p].mac_table[e].mac[0],
                       sw->ports[p].mac_table[e].mac[1],
                       sw->ports[p].mac_table[e].mac[2],
                       sw->ports[p].mac_table[e].mac[3],
                       sw->ports[p].mac_table[e].mac[4],
                       sw->ports[p].mac_table[e].mac[5],
                       sw->ports[p].mac_table[e].port_id);
            }
        }
    }
    printf("Total entries: ");
    int total = 0;
    for (int p = 0; p < sw->num_ports; p++) {
        total += sw->ports[p].mac_table_size;
    }
    printf("%d\n", total);
}

void switch_print_port_stats(const SwitchFabric *sw, int port_id)
{
    if (!sw || port_id < 0 || port_id >= sw->num_ports) return;
    const SwitchPort *p = &sw->ports[port_id];
    printf("=== Port %d Statistics ===\n", port_id);
    printf("State:      %s\n", p->state == PORT_UP ? "UP" : "DOWN");
    printf("Speed:      %d Gbps\n", p->speed_gbps);
    printf("VLAN ID:    %u\n", p->vlan_id);
    printf("Frames RX:  %llu\n", (unsigned long long)p->frames_rx);
    printf("Frames TX:  %llu\n", (unsigned long long)p->frames_tx);
    printf("Bytes RX:   %llu\n", (unsigned long long)p->bytes_rx);
    printf("Bytes TX:   %llu\n", (unsigned long long)p->bytes_tx);
    printf("MAC entries: %d\n", p->mac_table_size);
}
