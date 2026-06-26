#ifndef TIMING_H
#define TIMING_H
#include <stdbool.h>
#include <stdint.h>

#define TIMING_MAX_NODES 128
#define TIMING_MAX_PATHS 256

typedef struct {
    char name[64];
    double t_rise;
    double t_fall;
    double arrival;
    double required;
    double slack;
    bool is_input;
    bool is_output;
} TimingNode;

typedef struct {
    int from_node;
    int to_node;
    double delay;
    bool is_critical;
} TimingEdge;

typedef struct {
    TimingNode nodes[TIMING_MAX_NODES];
    int node_count;
    TimingEdge edges[TIMING_MAX_PATHS];
    int edge_count;
    double clock_period_ps;
    double t_setup_ps;
    double t_hold_ps;
    double t_skew_ps;
} TimingGraph;

typedef struct {
    double max_delay_ps;
    double min_delay_ps;
    int path_nodes[TIMING_MAX_NODES];
    int path_length;
    int start_node;
    int end_node;
} CriticalPath;

TimingGraph timing_graph_create(double clock_period_ps, double setup_ps, double hold_ps);
int timing_add_node(TimingGraph* g, const char* name, double t_rise, double t_fall, bool is_in, bool is_out);
int timing_add_edge(TimingGraph* g, int from, int to, double delay);
void timing_propagate_arrival(TimingGraph* g);
void timing_propagate_required(TimingGraph* g);
void timing_compute_slack(TimingGraph* g);
bool timing_check_setup(const TimingGraph* g);
bool timing_check_hold(const TimingGraph* g);
CriticalPath timing_find_critical_path(const TimingGraph* g);
void timing_print_report(const TimingGraph* g);
void timing_print_critical_path(const CriticalPath* cp, const TimingGraph* g);
double timing_max_frequency_mhz(const TimingGraph* g);
int timing_count_violations(const TimingGraph* g);

#endif
