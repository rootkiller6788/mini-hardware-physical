#include "fsm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FSM fsm_create(FSMType type) {
    FSM fsm; fsm.type = type; fsm.state_count = 0; fsm.trans_count = 0;
    fsm.current_state = 0; fsm.last_output = 0; return fsm;
}
int fsm_add_state(FSM* fsm, const char* name, int moore_output, bool is_initial, bool is_accept) {
    if (fsm->state_count >= FSM_MAX_STATES) return -1;
    FSMState* s = &fsm->states[fsm->state_count];
    if (name) { strncpy(s->name, name, FSM_MAX_NAME_LEN - 1); s->name[FSM_MAX_NAME_LEN - 1] = '\0'; }
    else snprintf(s->name, FSM_MAX_NAME_LEN, "S%d", fsm->state_count);
    s->moore_output = moore_output; s->is_initial = is_initial; s->is_accept = is_accept;
    s->encoding = fsm->state_count;
    if (is_initial) fsm->current_state = fsm->state_count;
    return fsm->state_count++;
}
void fsm_add_transition(FSM* fsm, int from, int input, int to, int mealy_output) {
    if (fsm->trans_count >= FSM_MAX_TRANSITIONS) return;
    FSMTransition* t = &fsm->transitions[fsm->trans_count];
    t->from_state = from; t->input = input; t->to_state = to;
    t->mealy_output = mealy_output; fsm->trans_count++;
}
void fsm_reset(FSM* fsm) {
    if (!fsm) return;
    for (int i = 0; i < fsm->state_count; i++)
        if (fsm->states[i].is_initial) { fsm->current_state = i; return; }
    fsm->current_state = 0;
}
int fsm_step(FSM* fsm, int input) {
    if (!fsm) return 0;
    for (int i = 0; i < fsm->trans_count; i++) {
        FSMTransition* t = &fsm->transitions[i];
        if (t->from_state == fsm->current_state && t->input == input) {
            int old = fsm->current_state;
            fsm->current_state = t->to_state;
            fsm->last_output = (fsm->type == FSM_MEALY) ? t->mealy_output : fsm->states[t->to_state].moore_output;
            printf("  [%s] --(%d)--> [%s]", fsm->states[old].name, input, fsm->states[t->to_state].name);
            printf("  out=%d\n", fsm->last_output);
            return t->to_state;
        }
    }
    return fsm->current_state;
}
int fsm_get_output(const FSM* fsm) { return fsm ? fsm->last_output : 0; }
void fsm_simulate(FSM* fsm, const int* inputs, int length) {
    if (!fsm || !inputs) return;
    printf("FSM simulation (type=%s, start=%s):\n",
           fsm->type == FSM_MOORE ? "Moore" : "Mealy", fsm->states[fsm->current_state].name);
    for (int i = 0; i < length; i++) fsm_step(fsm, inputs[i]);
    printf("Final: %s\n\n", fsm->states[fsm->current_state].name);
}
bool fsm_is_accept(const FSM* fsm) {
    if (!fsm || fsm->current_state >= fsm->state_count) return false;
    return fsm->states[fsm->current_state].is_accept;
}
int fsm_state_count(const FSM* fsm) { return fsm ? fsm->state_count : 0; }

int fsm_minimize(FSM* fsm) {
    if (!fsm || fsm->state_count <= 1) return fsm ? fsm->state_count : 0;
    int equiv[FSM_MAX_STATES];
    for (int i = 0; i < fsm->state_count; i++) equiv[i] = i;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < fsm->state_count; i++) {
            for (int j = i + 1; j < fsm->state_count; j++) {
                if (equiv[i] == equiv[j]) continue;
                if (fsm->states[i].moore_output != fsm->states[j].moore_output) continue;
                if (fsm->states[i].is_accept != fsm->states[j].is_accept) continue;
                int old = equiv[j];
                for (int k = 0; k < fsm->state_count; k++)
                    if (equiv[k] == old) equiv[k] = equiv[i];
                changed = true;
            }
        }
    }
    int classes = 0;
    for (int i = 0; i < fsm->state_count; i++)
        if (equiv[i] == i) classes++;
    return classes;
}
int fsm_merge_equivalent(FSM* fsm) {
    if (!fsm || fsm->state_count <= 1) return 0;
    int merged = 0;
    for (int i = 0; i < fsm->state_count; i++) {
        for (int j = i + 1; j < fsm->state_count; j++) {
            if (fsm->states[i].moore_output == fsm->states[j].moore_output &&
                fsm->states[i].is_accept == fsm->states[j].is_accept) {
                merged++;
            }
        }
    }
    return merged / 2;
}
bool fsm_convert_type(FSM* fsm, FSMType new_type) {
    if (!fsm) return false;
    if (fsm->type == new_type) return true;
    if (fsm->type == FSM_MOORE && new_type == FSM_MEALY) {
        for (int i = 0; i < fsm->trans_count; i++)
            fsm->transitions[i].mealy_output = fsm->states[fsm->transitions[i].to_state].moore_output;
    } else if (fsm->type == FSM_MEALY && new_type == FSM_MOORE) {
        for (int i = 0; i < fsm->state_count; i++)
            fsm->states[i].moore_output = fsm->states[i].is_accept ? 1 : 0;
    }
    fsm->type = new_type; return true;
}

FSMGraph fsm_to_graph(const FSM* fsm) {
    FSMGraph g; g.num_states = fsm ? fsm->state_count : 0;
    g.num_inputs = 2; g.num_edges = 0;
    if (fsm) {
        for (int i = 0; i < fsm->trans_count && g.num_edges < FSM_GRAPH_MAX_PATHS; i++) {
            g.edges[g.num_edges].from = fsm->transitions[i].from_state;
            g.edges[g.num_edges].input = fsm->transitions[i].input;
            g.edges[g.num_edges].to = fsm->transitions[i].to_state;
            g.edges[g.num_edges].output = fsm->transitions[i].mealy_output;
            g.num_edges++;
        }
    }
    return g;
}
int fsm_reachable_states(const FSM* fsm, int* reachable, int max_states) {
    if (!fsm || !reachable || fsm->state_count == 0) return 0;
    bool visited[FSM_MAX_STATES] = {false};
    int queue[FSM_MAX_STATES], qh = 0, qt = 0, cnt = 0;
    int start = fsm->current_state;
    visited[start] = true; queue[qt++] = start;
    while (qh < qt && cnt < max_states) {
        int s = queue[qh++]; reachable[cnt++] = s;
        for (int i = 0; i < fsm->trans_count; i++) {
            if (fsm->transitions[i].from_state == s) {
                int t = fsm->transitions[i].to_state;
                if (!visited[t] && t < fsm->state_count) { visited[t] = true; queue[qt++] = t; }
            }
        }
    }
    return cnt;
}
bool fsm_is_complete(const FSM* fsm, int num_inputs) {
    if (!fsm) return false;
    for (int s = 0; s < fsm->state_count; s++)
        for (int i = 0; i < num_inputs; i++) {
            bool found = false;
            for (int t = 0; t < fsm->trans_count; t++)
                if (fsm->transitions[t].from_state == s && fsm->transitions[t].input == i)
                { found = true; break; }
            if (!found) return false;
        }
    return true;
}
void fsm_print_table(const FSM* fsm) {
    if (!fsm) return;
    printf("FSM (%s), %d states, %d transitions\n",
           fsm->type == FSM_MOORE ? "Moore" : "Mealy", fsm->state_count, fsm->trans_count);
    printf("State |");
    for (int i = 0; i < fsm->state_count; i++) printf(" %-10s", fsm->states[i].name);
    printf("\nOut   |");
    for (int i = 0; i < fsm->state_count; i++) printf(" %-10d", fsm->states[i].moore_output);
    printf("\n\nTransitions:\n");
    for (int i = 0; i < fsm->trans_count; i++)
        printf("  %s --%d--> %s [out=%d]\n", fsm->states[fsm->transitions[i].from_state].name,
               fsm->transitions[i].input, fsm->states[fsm->transitions[i].to_state].name,
               fsm->transitions[i].mealy_output);
}
void fsm_print_dot(const FSM* fsm) {
    if (!fsm) return;
    printf("digraph FSM {\n");
    for (int i = 0; i < fsm->state_count; i++)
        printf("  %s [shape=%s];\n", fsm->states[i].name, fsm->states[i].is_accept ? "doublecircle" : "circle");
    for (int i = 0; i < fsm->trans_count; i++)
        printf("  %s -> %s [label=\"in=%d\"];\n",
               fsm->states[fsm->transitions[i].from_state].name,
               fsm->states[fsm->transitions[i].to_state].name, fsm->transitions[i].input);
    printf("}\n");
}

FSM fsm_kmp_build(const char* pattern) {
    FSM fsm = fsm_create(FSM_MOORE); if (!pattern) return fsm;
    int m = strlen(pattern);
    for (int i = 0; i <= m; i++) {
        char name[16]; snprintf(name, sizeof(name), "S%d", i);
        fsm_add_state(&fsm, name, 0, i == 0, i == m);
    }
    for (int i = 0; i <= m; i++) {
        for (int c = 0; c < 256; c++) {
            int next = 0;
            if (i < m && c == (unsigned char)pattern[i]) next = i + 1;
            else {
                for (int k = i; k > 0; k--) {
                    if (c == (unsigned char)pattern[k - 1]) {
                        bool match = true;
                        for (int j = 0; j < k - 1; j++)
                            if (pattern[j] != pattern[i - k + 1 + j]) { match = false; break; }
                        if (match) { next = k; break; }
                    }
                }
            }
            fsm_add_transition(&fsm, i, c, next, 0);
        }
    }
    return fsm;
}
FSM fsm_regex_build(const char* pattern) {
    FSM fsm = fsm_create(FSM_MOORE);
    if (!pattern) return fsm;
    fsm_add_state(&fsm, "S0", 0, true, false);
    fsm_add_state(&fsm, "S1", 0, false, true);
    fsm_add_transition(&fsm, 0, 1, 1, 0);
    return fsm;
}
