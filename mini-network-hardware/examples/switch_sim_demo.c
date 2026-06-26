#include "switch_fabric.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    printf("=== mini-network-hardware: Switch Fabric Simulation ===\n\n");

    printf("[1] Creating 4-port switch\n");
    SwitchFabric *sw = switch_init(4);
    if (!sw) {
        printf("    ERROR: Failed to create switch\n");
        return 1;
    }
    printf("    Switch created with %d ports\n", sw->num_ports);

    printf("\n[2] Adding ports\n");
    switch_add_port(sw, 0, 10);
    switch_add_port(sw, 1, 10);
    switch_add_port(sw, 2, 10);
    switch_add_port(sw, 3, 10);
    printf("    Port 0: 10 Gbps (UP)\n");
    printf("    Port 1: 10 Gbps (UP)\n");
    printf("    Port 2: 10 Gbps (UP)\n");
    printf("    Port 3: 10 Gbps (UP)\n");

    uint8_t mac_a[6] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t mac_b[6] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
    uint8_t mac_c[6] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25};
    uint8_t mac_d[6] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35};

    printf("\n[3] MAC Address Learning\n");
    printf("    Learning: port 0 -> 00:01:02:03:04:05\n");
    switch_learn_mac(sw, mac_a, 0);
    printf("    Learning: port 0 -> 10:11:12:13:14:15\n");
    switch_learn_mac(sw, mac_b, 0);
    printf("    Learning: port 0 -> 20:21:22:23:24:25\n");
    switch_learn_mac(sw, mac_c, 0);

    printf("    Learning: port 1 -> 30:31:32:33:34:35\n");
    switch_learn_mac(sw, mac_d, 1);

    printf("\n[4] MAC Table After Learning\n");
    switch_print_mac_table(sw);

    printf("\n[5] Forwarding: Known Destination\n");
    uint8_t test_frame[64];
    memset(test_frame, 0xAA, sizeof(test_frame));

    int dst_port = switch_forward(sw, mac_a, test_frame, sizeof(test_frame));
    if (dst_port >= 0) {
        printf("    Frame to 00:01:02:03:04:05 forwarded to port %d\n", dst_port);
    } else {
        printf("    Unknown destination\n");
    }

    dst_port = switch_forward(sw, mac_d, test_frame, sizeof(test_frame));
    if (dst_port >= 0) {
        printf("    Frame to 30:31:32:33:34:35 forwarded to port %d\n", dst_port);
    } else {
        printf("    Unknown destination\n");
    }

    printf("\n[6] Flooding: Unknown Destination\n");
    printf("    Attempting to forward to FF:FF:FF:FF:FF:FF (broadcast/unknown)\n");
    switch_flood(sw, 0, test_frame, sizeof(test_frame));
    printf("    Flooded to all ports except source port 0\n");

    printf("\n[7] Crossbar Routing\n");
    switch_crossbar_route(sw, 0, 1);
    switch_crossbar_route(sw, 1, 2);
    switch_crossbar_route(sw, 2, 3);
    switch_crossbar_route(sw, 3, 0);

    printf("    Crossbar matrix:\n");
    for (int i = 0; i < sw->num_ports; i++) {
        printf("    Port %d -> ", i);
        for (int j = 0; j < sw->num_ports; j++) {
            if (sw->crossbar_matrix[i][j]) {
                printf("[%d] ", j);
            }
        }
        printf("\n");
    }

    printf("\n[8] Port Statistics\n");
    for (int p = 0; p < sw->num_ports; p++) {
        switch_print_port_stats(sw, p);
        printf("\n");
    }

    printf("\n[9] Re-learning (MAC moves)\n");
    switch_learn_mac(sw, mac_a, 2);
    printf("    MAC 00:01:02:03:04:05 moved to port 2\n");
    switch_print_mac_table(sw);

    switch_destroy(sw);
    printf("\n=== Demo Complete ===\n");
    return 0;
}
