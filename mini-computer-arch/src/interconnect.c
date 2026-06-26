#include "interconnect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void intcn_init(Interconnect *icn, Topology topology, uint32_t mesh_width)
{
    icn->topology = topology;
    icn->node_count = 0;
    icn->mesh_width = mesh_width > 0 ? mesh_width : 4;
    memset(icn->nodes, 0, sizeof(icn->nodes));
    memset(icn->routing_table, 0, sizeof(icn->routing_table));
}

void intcn_add_node(Interconnect *icn, NodeType type, double x, double y)
{
    if (icn->node_count >= INTERCONNECT_MAX_NODES) return;

    Node *node = &icn->nodes[icn->node_count];
    node->id = icn->node_count;
    node->type = type;
    node->x = x;
    node->y = y;
    icn->node_count++;
}

static uint32_t bus_route(const Interconnect *icn, uint32_t src, uint32_t dst,
                          uint32_t *path, uint32_t *path_len)
{
    path[0] = src;
    *path_len = 1;
    if (src != dst) {
        path[1] = dst;
        *path_len = 2;
    }
    return 1;
}

static uint32_t crossbar_route(const Interconnect *icn, uint32_t src, uint32_t dst,
                               uint32_t *path, uint32_t *path_len)
{
    path[0] = src;
    *path_len = 1;
    if (src != dst) {
        path[1] = dst;
        *path_len = 2;
    }
    return 1;
}

static uint32_t mesh_route(const Interconnect *icn, uint32_t src, uint32_t dst,
                           uint32_t *path, uint32_t *path_len)
{
    uint32_t w = icn->mesh_width;

    if (src >= w * w || dst >= w * w) {
        path[0] = src;
        *path_len = 1;
        return 1;
    }

    uint32_t sx = src % w;
    uint32_t sy = src / w;
    uint32_t dx = dst % w;
    uint32_t dy = dst / w;
    uint32_t hops = 0;

    *path_len = 0;

    uint32_t cx = sx;
    uint32_t cy = sy;
    path[(*path_len)++] = cy * w + cx;

    while (cx != dx) {
        cx += (dx > cx) ? 1 : -1;
        path[(*path_len)++] = cy * w + cx;
        hops++;
    }

    while (cy != dy) {
        cy += (dy > cy) ? 1 : -1;
        path[(*path_len)++] = cy * w + cx;
        hops++;
    }

    return hops;
}

static uint32_t ring_route(const Interconnect *icn, uint32_t src, uint32_t dst,
                           uint32_t *path, uint32_t *path_len)
{
    uint32_t n = icn->node_count;
    if (n == 0) {
        *path_len = 0;
        return 0;
    }

    uint32_t forward_dist = (dst >= src) ? (dst - src) : (n - src + dst);
    uint32_t backward_dist = (src >= dst) ? (src - dst) : (src + n - dst);

    *path_len = 0;
    path[(*path_len)++] = src;
    uint32_t hops = 0;

    if (forward_dist <= backward_dist) {
        for (uint32_t i = 1; i <= forward_dist; i++) {
            path[(*path_len)++] = (src + i) % n;
            hops++;
        }
    } else {
        for (uint32_t i = 1; i <= backward_dist; i++) {
            path[(*path_len)++] = (src + n - i) % n;
            hops++;
        }
    }

    return hops;
}

static uint32_t tree_route(const Interconnect *icn, uint32_t src, uint32_t dst,
                           uint32_t *path, uint32_t *path_len)
{
    path[0] = src;
    *path_len = 1;
    if (src != dst) {
        path[1] = dst;
        *path_len = 2;
    }
    return 1;
}

uint32_t intcn_route(const Interconnect *icn, uint32_t src, uint32_t dst,
                     uint32_t *path, uint32_t *path_len)
{
    if (src >= INTERCONNECT_MAX_NODES || dst >= INTERCONNECT_MAX_NODES) {
        *path_len = 0;
        return 0;
    }

    switch (icn->topology) {
    case BUS:
        return bus_route(icn, src, dst, path, path_len);
    case CROSSBAR:
        return crossbar_route(icn, src, dst, path, path_len);
    case MESH:
        return mesh_route(icn, src, dst, path, path_len);
    case RING:
        return ring_route(icn, src, dst, path, path_len);
    case TREE:
        return tree_route(icn, src, dst, path, path_len);
    default:
        *path_len = 0;
        return 0;
    }
}

double intcn_latency(const Interconnect *icn, uint32_t src, uint32_t dst)
{
    uint32_t path[INTERCONNECT_MAX_HOPS];
    uint32_t path_len;

    uint32_t hops = intcn_route(icn, src, dst, path, &path_len);

    switch (icn->topology) {
    case BUS:
        return (src == dst) ? 1.0 : 2.0;
    case CROSSBAR:
        return 2.0;
    case MESH:
        return (double)hops * 2.0 + 1.0;
    case RING:
        return (double)hops * 1.5 + 1.0;
    case TREE:
        return log2((double)icn->node_count) * 3.0 + 1.0;
    default:
        return (double)hops * 2.0;
    }
}

double intcn_bandwidth(const Interconnect *icn)
{
    switch (icn->topology) {
    case BUS:
        return 1.0;
    case CROSSBAR:
        return (double)icn->node_count;
    case MESH: {
        double w = (double)icn->mesh_width;
        return w * 2.0;
    }
    case RING:
        return 2.0;
    case TREE:
        return log2((double)icn->node_count);
    default:
        return 1.0;
    }
}

static const char *topology_name(Topology t)
{
    switch (t) {
    case BUS:      return "Bus";
    case CROSSBAR: return "Crossbar";
    case MESH:     return "Mesh";
    case RING:     return "Ring";
    case TREE:     return "Tree";
    default:       return "Unknown";
    }
}

static const char *node_type_name(NodeType t)
{
    switch (t) {
    case NODE_PE:     return "PE";
    case NODE_MEM:    return "MEM";
    case NODE_SWITCH: return "SW";
    default:          return "??";
    }
}

void intcn_print_topology(const Interconnect *icn)
{
    printf("========================================\n");
    printf("  Interconnection Network\n");
    printf("========================================\n");
    printf("  Topology:    %s\n", topology_name(icn->topology));
    printf("  Nodes:       %u\n", icn->node_count);
    printf("  Bandwidth:   %.1f (relative)\n", intcn_bandwidth(icn));
    printf("  Mesh Width:  %u\n", icn->mesh_width);
    printf("----------------------------------------\n");
    printf("  Node List:\n");

    for (uint32_t i = 0; i < icn->node_count; i++) {
        const Node *n = &icn->nodes[i];
        printf("    [%u] %-3s @ (%.0f, %.0f)\n",
               n->id, node_type_name(n->type), n->x, n->y);
    }
    printf("========================================\n");
}

void intcn_print_route(const Interconnect *icn, uint32_t src, uint32_t dst)
{
    uint32_t path[INTERCONNECT_MAX_HOPS];
    uint32_t path_len;
    uint32_t hops = intcn_route(icn, src, dst, path, &path_len);
    double latency = intcn_latency(icn, src, dst);

    printf("Route [%u] -> [%u]: ", src, dst);
    for (uint32_t i = 0; i < path_len; i++) {
        printf("%u", path[i]);
        if (i < path_len - 1) printf(" -> ");
    }
    printf("\n  Hops: %u, Latency: %.1f ns\n", hops, latency);
}
