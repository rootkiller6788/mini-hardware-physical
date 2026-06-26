#include <stdio.h>
#include "fsm.h"

int main(void) {
    printf("===== Traffic Light FSM (Moore) =====\n\n");
    FSM traffic = fsm_create(FSM_MOORE);
    fsm_add_state(&traffic, "GREEN", 0, true, false);
    fsm_add_state(&traffic, "YELLOW", 0, false, false);
    fsm_add_state(&traffic, "RED", 1, false, true);
    fsm_add_transition(&traffic, 0, 0, 1, 0);
    fsm_add_transition(&traffic, 1, 0, 2, 0);
    fsm_add_transition(&traffic, 2, 0, 0, 0);
    int t_in[] = {0, 0, 0, 0, 0, 0};
    fsm_simulate(&traffic, t_in, 6);

    printf("===== Sequence Detector (101) =====\n\n");
    FSM seq = fsm_create(FSM_MOORE);
    fsm_add_state(&seq, "S0", 0, true, false);
    fsm_add_state(&seq, "S1", 0, false, false);
    fsm_add_state(&seq, "S10", 0, false, false);
    fsm_add_state(&seq, "S101", 1, false, true);
    fsm_add_transition(&seq, 0, 0, 0, 0); fsm_add_transition(&seq, 0, 1, 1, 0);
    fsm_add_transition(&seq, 1, 0, 2, 0); fsm_add_transition(&seq, 1, 1, 1, 0);
    fsm_add_transition(&seq, 2, 0, 0, 0); fsm_add_transition(&seq, 2, 1, 3, 0);
    fsm_add_transition(&seq, 3, 0, 2, 0); fsm_add_transition(&seq, 3, 1, 1, 0);
    int s_in[] = {1, 0, 1, 0, 1, 1, 0, 1};
    fsm_simulate(&seq, s_in, 8);

    printf("===== Mealy Edge Detector =====\n\n");
    FSM edge = fsm_create(FSM_MEALY);
    fsm_add_state(&edge, "LOW", 0, true, false);
    fsm_add_state(&edge, "HIGH", 0, false, false);
    fsm_add_transition(&edge, 0, 0, 0, 0); fsm_add_transition(&edge, 0, 1, 1, 1);
    fsm_add_transition(&edge, 1, 0, 0, 0); fsm_add_transition(&edge, 1, 1, 1, 0);
    int e_in[] = {0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};
    fsm_simulate(&edge, e_in, 11);

    printf("FSM demo complete.\n");
    return 0;
}
