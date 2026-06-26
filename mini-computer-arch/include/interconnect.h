#ifndef INTERCONNECT_H
#define INTERCONNECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define INTERCONNECT_MAX_NODES 64
#define INTERCONNECT_MAX_HOPS 16

typedef enum {
    BUS,
    CROSSBAR,
    MESH,
    RING,
    TREE
} Topology;

typedef enum {
    NODE_PE,
    NODE_MEM,
    NODE_SWITCH
} NodeType;

typedef struct {
    uint32_t id;
    NodeType type;
    double x;
    double y;
} Node;

typedef struct {
    Topology topology;
    Node nodes[INTERCONNECT_MAX_NODES];
    uint32_t node_count;
    double routing_table[INTERCONNECT_MAX_NODES][INTERCONNECT_MAX_NODES];
    uint32_t mesh_width;
} Interconnect;

void intcn_init(Interconnect *icn, Topology topology, uint32_t mesh_width);
void intcn_add_node(Interconnect *icn, NodeType type, double x, double y);
uint32_t intcn_route(const Interconnect *icn, uint32_t src, uint32_t dst,
                     uint32_t *path, uint32_t *path_len);
double intcn_latency(const Interconnect *icn, uint32_t src, uint32_t dst);
double intcn_bandwidth(const Interconnect *icn);
void intcn_print_topology(const Interconnect *icn);
void intcn_print_route(const Interconnect *icn, uint32_t src, uint32_t dst);

#endif /* INTERCONNECT_H */
