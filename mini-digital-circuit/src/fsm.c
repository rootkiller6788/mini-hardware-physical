#include "fsm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FSM fsm_create(FSMType type) {
    FSM fsm;
    fsm.type            = type;
    fsm.state_count     = 0;
    fsm.trans_count     = 0;
    fsm.current_state   = 0;
    return fsm;
}

int fsm_add_state(FSM* fsm, const char* name, bool is_accept) {
    if (fsm->state_count >= FSM_MAX_STATES) return -1;
    FSMState* s = &fsm->states[fsm->state_count];
    strncpy(s->name, name, FSM_MAX_NAME_LEN - 1);
    s->name[FSM_MAX_NAME_LEN - 1] = '\0';
    s->is_accept = is_accept;
    return fsm->state_count++;
}

void fsm_add_transition(FSM* fsm, int from, int input, int to, int mealy_out) {
    if (fsm->trans_count >= FSM_MAX_TRANSITIONS) return;
    FSMTransition* t = &fsm->transitions[fsm->trans_count];
    t->from_state   = from;
    t->input        = input;
    t->to_state     = to;
    t->mealy_output = mealy_out;
    fsm->trans_count++;
}

void fsm_reset(FSM* fsm) {
    fsm->current_state = 0;
}

int fsm_step(FSM* fsm, int input) {
    /* 查找匹配的转换 */
    for (int i = 0; i < fsm->trans_count; i++) {
        FSMTransition* t = &fsm->transitions[i];
        if (t->from_state == fsm->current_state && t->input == input) {
            int old_state = fsm->current_state;
            fsm->current_state = t->to_state;

            /* 打印状态转换信息 */
            printf("  [%s] --(%d)--> [%s]",
                   fsm->states[old_state].name, input,
                   fsm->states[t->to_state].name);

            if (fsm->type == FSM_MEALY) {
                printf("  output=%d", t->mealy_output);
            } else {
                printf("  output=%d", fsm->states[t->to_state].is_accept ? 1 : 0);
            }
            printf("\n");
            return t->to_state;
        }
    }
    return fsm->current_state; /* 无匹配则保持 */
}

int fsm_step_output(const FSM* fsm) {
    if (fsm->type == FSM_MEALY) {
        /* 返回前一步的输出（由最后匹配的转换给出） */
        return 0;
    } else {
        return fsm->states[fsm->current_state].is_accept ? 1 : 0;
    }
}

void fsm_simulate(FSM* fsm, const int* inputs, int length) {
    printf("FSM simulation start (type=%s, start=%s):\n",
           fsm->type == FSM_MOORE ? "Moore" : "Mealy",
           fsm->states[fsm->current_state].name);
    for (int i = 0; i < length; i++) {
        fsm_step(fsm, inputs[i]);
    }
    printf("Final state: %s\n\n", fsm->states[fsm->current_state].name);
}
