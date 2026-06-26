#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interconnect.h"

int main(void)
{
    Interconnect bus_icn, crossbar_icn, mesh_icn, ring_icn;

    printf("========================================\n");
    printf("  Interconnection Network Demo\n");
    printf("========================================\n\n");

    intcn_init(&bus_icn, BUS, 0);
    intcn_init(&crossbar_icn, CROSSBAR, 0);
    intcn_init(&mesh_icn, MESH, 4);
    intcn_init(&ring_icn, RING, 0);

    for (uint32_t i = 0; i < 4; i++) {
        intcn_add_node(&bus_icn, NODE_PE, (double)i, 0.0);
        intcn_add_node(&crossbar_icn, NODE_PE, (double)i, 0.0);
        intcn_add_node(&ring_icn, NODE_PE, (double)i, 0.0);
    }

    for (uint32_t y = 0; y < 4; y++) {
        for (uint32_t x = 0; x < 4; x++) {
            intcn_add_node(&mesh_icn, NODE_PE, (double)x, (double)y);
        }
    }

    printf("=== Bus Topology ===\n");
    intcn_print_topology(&bus_icn);
    printf("\n");

    printf("Bus routing between nodes:\n");
    for (uint32_t i = 0; i < 4; i++) {
        for (uint32_t j = i + 1; j < 4; j++) {
            intcn_print_route(&bus_icn, i, j);
        }
    }
    printf("\n");

    printf("=== Crossbar Topology ===\n");
    intcn_print_topology(&crossbar_icn);
    printf("\n");

    printf("Crossbar routing between nodes:\n");
    for (uint32_t i = 0; i < 4; i++) {
        for (uint32_t j = i + 1; j < 4; j++) {
            intcn_print_route(&crossbar_icn, i, j);
        }
    }
    printf("\n");

    printf("=== Mesh (4x4) Topology ===\n");
    intcn_print_topology(&mesh_icn);
    printf("\n");

    printf("Mesh routing examples:\n");
    intcn_print_route(&mesh_icn, 0, 15);
    intcn_print_route(&mesh_icn, 3, 12);
    intcn_print_route(&mesh_icn, 5, 10);
    intcn_print_route(&mesh_icn, 0, 5);
    printf("\n");

    printf("=== Ring Topology ===\n");
    intcn_print_topology(&ring_icn);
    printf("\n");

    printf("Ring routing between nodes:\n");
    for (uint32_t i = 0; i < 4; i++) {
        for (uint32_t j = i + 1; j < 4; j++) {
            intcn_print_route(&ring_icn, i, j);
        }
    }
    printf("\n");

    printf("========================================\n");
    printf("  Topology Comparison\n");
    printf("========================================\n");
    printf("%-12s %-12s %-16s\n", "Topology", "Bandwidth", "Avg Latency");
    printf("--------------------------------------------\n");

    Interconnect *all[] = { &bus_icn, &crossbar_icn, &mesh_icn, &ring_icn };
    const char *names[] = { "Bus", "Crossbar", "Mesh (4x4)", "Ring" };

    for (int t = 0; t < 4; t++) {
        double total_lat = 0.0;
        uint32_t pairs = 0;
        for (uint32_t i = 0; i < all[t]->node_count; i++) {
            for (uint32_t j = i + 1; j < all[t]->node_count; j++) {
                total_lat += intcn_latency(all[t], i, j);
                pairs++;
            }
        }
        double avg_lat = pairs > 0 ? total_lat / (double)pairs : 0.0;
        printf("%-12s %-12.1f %-16.2f\n", names[t],
               intcn_bandwidth(all[t]), avg_lat);
    }
    printf("========================================\n");

    return 0;
}
