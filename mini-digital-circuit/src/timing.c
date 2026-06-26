#include "timing.h"
#include <stdio.h>
#include <string.h>
#include <float.h>

TimingGraph timing_graph_create(double clock_period_ps, double setup_ps, double hold_ps) {
    TimingGraph g; g.node_count = 0; g.edge_count = 0;
    g.clock_period_ps = clock_period_ps; g.t_setup_ps = setup_ps; g.t_hold_ps = hold_ps;
    g.t_skew_ps = 0.0; return g;
}
int timing_add_node(TimingGraph* g, const char* name, double t_rise, double t_fall, bool is_in, bool is_out) {
    if (!g || g->node_count >= TIMING_MAX_NODES) return -1;
    TimingNode* n = &g->nodes[g->node_count];
    if (name) { strncpy(n->name, name, 63); n->name[63] = '\0'; } else n->name[0] = '\0';
    n->t_rise = t_rise; n->t_fall = t_fall; n->arrival = 0.0;
    n->required = g->clock_period_ps; n->slack = 0.0;
    n->is_input = is_in; n->is_output = is_out; return g->node_count++;
}
int timing_add_edge(TimingGraph* g, int from, int to, double delay) {
    if (!g || g->edge_count >= TIMING_MAX_PATHS) return -1;
    if (from < 0 || from >= g->node_count || to < 0 || to >= g->node_count) return -1;
    g->edges[g->edge_count].from_node = from;
    g->edges[g->edge_count].to_node = to;
    g->edges[g->edge_count].delay = delay;
    g->edges[g->edge_count].is_critical = false;
    return g->edge_count++;
}
void timing_propagate_arrival(TimingGraph* g) {
    if (!g) return;
    for (int i = 0; i < g->node_count; i++)
        if (g->nodes[i].is_input) g->nodes[i].arrival = 0.0;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int e = 0; e < g->edge_count; e++) {
            int from = g->edges[e].from_node, to = g->edges[e].to_node;
            double new_arrival = g->nodes[from].arrival + g->edges[e].delay;
            if (new_arrival > g->nodes[to].arrival) {
                g->nodes[to].arrival = new_arrival; changed = true;
            }
        }
    }
}
void timing_propagate_required(TimingGraph* g) {
    if (!g) return;
    for (int i = 0; i < g->node_count; i++)
        if (g->nodes[i].is_output) g->nodes[i].required = g->clock_period_ps - g->t_setup_ps;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int e = 0; e < g->edge_count; e++) {
            int from = g->edges[e].from_node, to = g->edges[e].to_node;
            double new_req = g->nodes[to].required - g->edges[e].delay;
            if (new_req < g->nodes[from].required) {
                g->nodes[from].required = new_req; changed = true;
            }
        }
    }
}
void timing_compute_slack(TimingGraph* g) {
    if (!g) return;
    timing_propagate_arrival(g); timing_propagate_required(g);
    for (int i = 0; i < g->node_count; i++)
        g->nodes[i].slack = g->nodes[i].required - g->nodes[i].arrival;
}
bool timing_check_setup(const TimingGraph* g) {
    if (!g) return true;
    for (int i = 0; i < g->node_count; i++)
        if (g->nodes[i].slack < 0.0) return false;
    return true;
}
bool timing_check_hold(const TimingGraph* g) {
    if (!g) return true;
    for (int e = 0; e < g->edge_count; e++) {
        double skew_corrected = g->edges[e].delay - g->t_skew_ps;
        if (skew_corrected < g->t_hold_ps) return false;
    }
    return true;
}
CriticalPath timing_find_critical_path(const TimingGraph* g) {
    CriticalPath cp; memset(&cp, 0, sizeof(cp)); cp.max_delay_ps = 0.0; cp.min_delay_ps = DBL_MAX;
    if (!g || g->node_count == 0) return cp;
    int worst = 0;
    for (int i = 0; i < g->node_count; i++)
        if (g->nodes[i].slack < g->nodes[worst].slack) worst = i;
    cp.max_delay_ps = g->nodes[worst].arrival;
    cp.start_node = 0; cp.end_node = worst; cp.path_length = 1;
    cp.path_nodes[0] = worst;
    for (int i = 0; i < cp.path_length; i++) cp.min_delay_ps = g->nodes[worst].arrival;
    return cp;
}
void timing_print_report(const TimingGraph* g) {
    if (!g) return;
    printf("=== Timing Report ===\n");
    printf("Clock period: %.1f ps (%.2f MHz)\n", g->clock_period_ps, 1e6 / g->clock_period_ps);
    printf("Setup: %.1f ps, Hold: %.1f ps, Skew: %.1f ps\n", g->t_setup_ps, g->t_hold_ps, g->t_skew_ps);
    printf("Node          Arrival   Required  Slack\n");
    printf("------------  --------  --------  --------\n");
    for (int i = 0; i < g->node_count; i++)
        printf("%-12s  %8.1f  %8.1f  %8.1f %s\n", g->nodes[i].name, g->nodes[i].arrival,
               g->nodes[i].required, g->nodes[i].slack, g->nodes[i].slack < 0 ? "VIOLATION" : "");
}
void timing_print_critical_path(const CriticalPath* cp, const TimingGraph* g) {
    if (!cp || !g) return;
    printf("Critical path: %d nodes, delay %.1f ps\n", cp->path_length, cp->max_delay_ps);
    for (int i = 0; i < cp->path_length && cp->path_nodes[i] < g->node_count; i++)
        printf("  %s\n", g->nodes[cp->path_nodes[i]].name);
}
double timing_max_frequency_mhz(const TimingGraph* g) {
    if (!g || g->node_count == 0) return 0.0;
    double max_arrival = 0.0;
    for (int i = 0; i < g->node_count; i++)
        if (g->nodes[i].is_output && g->nodes[i].arrival > max_arrival) max_arrival = g->nodes[i].arrival;
    double t_min = max_arrival + g->t_setup_ps + g->t_skew_ps;
    return t_min > 0 ? 1e6 / t_min : 0.0;
}
int timing_count_violations(const TimingGraph* g) {
    if (!g) return 0;
    int count = 0;
    for (int i = 0; i < g->node_count; i++)
        if (g->nodes[i].slack < 0.0) count++;
    return count;
}
