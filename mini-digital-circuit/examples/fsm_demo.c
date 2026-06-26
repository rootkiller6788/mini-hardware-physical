#include <stdio.h>
#include "fsm.h"

int main(void) {
    printf("===== Traffic Light FSM (Moore) =====\n\n");

    /* 交通灯 FSM：Green → Yellow → Red → Green */
    FSM traffic = fsm_create(FSM_MOORE);
    fsm_add_state(&traffic, "GREEN",  false);
    fsm_add_state(&traffic, "YELLOW", false);
    fsm_add_state(&traffic, "RED",    true);

    fsm_add_transition(&traffic, 0, 0, 1, 0); /* GREEN  → YELLOW */
    fsm_add_transition(&traffic, 1, 0, 2, 0); /* YELLOW → RED */
    fsm_add_transition(&traffic, 2, 0, 0, 0); /* RED    → GREEN */

    int traffic_inputs[] = {0, 0, 0, 0, 0, 0};
    fsm_simulate(&traffic, traffic_inputs, 6);

    printf("===== Sequence Detector FSM (101) - Moore =====\n\n");

    /* 序列检测器：检测 "101" */
    FSM seq = fsm_create(FSM_MOORE);
    fsm_add_state(&seq, "S0", false);   /* 初始状态，未匹配 */
    fsm_add_state(&seq, "S1", false);   /* 已匹配 1 */
    fsm_add_state(&seq, "S10", false);  /* 已匹配 10 */
    fsm_add_state(&seq, "S101", true);  /* 已匹配 101 */

    fsm_add_transition(&seq, 0, 0, 0, 0); /* S0 + 0 → S0 */
    fsm_add_transition(&seq, 0, 1, 1, 0); /* S0 + 1 → S1 */
    fsm_add_transition(&seq, 1, 0, 2, 0); /* S1 + 0 → S10 */
    fsm_add_transition(&seq, 1, 1, 1, 0); /* S1 + 1 → S1 */
    fsm_add_transition(&seq, 2, 0, 0, 0); /* S10 + 0 → S0 */
    fsm_add_transition(&seq, 2, 1, 3, 0); /* S10 + 1 → S101 */
    fsm_add_transition(&seq, 3, 0, 2, 0); /* S101 + 0 → S10 */
    fsm_add_transition(&seq, 3, 1, 1, 0); /* S101 + 1 → S1 */

    int seq_inputs[] = {1, 0, 1, 0, 1, 1, 0, 1};
    fsm_simulate(&seq, seq_inputs, 8);

    printf("===== Mealy FSM Demo: Edge Detector =====\n\n");

    FSM edge = fsm_create(FSM_MEALY);
    fsm_add_state(&edge, "LOW",  false);
    fsm_add_state(&edge, "HIGH", false);

    fsm_add_transition(&edge, 0, 0, 0, 0); /* LOW,  in=0 → LOW,  out=0 */
    fsm_add_transition(&edge, 0, 1, 1, 1); /* LOW,  in=1 → HIGH, out=1 (上升沿) */
    fsm_add_transition(&edge, 1, 0, 0, 0); /* HIGH, in=0 → LOW,  out=0 */
    fsm_add_transition(&edge, 1, 1, 1, 0); /* HIGH, in=1 → HIGH, out=0 */

    int edge_inputs[] = {0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};
    fsm_simulate(&edge, edge_inputs, 11);

    printf("FSM demo complete.\n");
    return 0;
}
