#ifndef FSM_H
#define FSM_H

#include <stdbool.h>

#define FSM_MAX_STATES      32
#define FSM_MAX_TRANSITIONS 128
#define FSM_MAX_NAME_LEN    32

typedef enum {
    FSM_MOORE,
    FSM_MEALY
} FSMType;

typedef struct {
    char name[FSM_MAX_NAME_LEN];
    bool is_accept;
} FSMState;

typedef struct {
    int  from_state;
    int  input;
    int  to_state;
    int  mealy_output;
} FSMTransition;

typedef struct {
    FSMType       type;
    FSMState      states[FSM_MAX_STATES];
    int           state_count;
    FSMTransition transitions[FSM_MAX_TRANSITIONS];
    int           trans_count;
    int           current_state;
} FSM;

FSM   fsm_create(FSMType type);
int   fsm_add_state(FSM* fsm, const char* name, bool is_accept);
void  fsm_add_transition(FSM* fsm, int from, int input, int to, int mealy_out);
void  fsm_reset(FSM* fsm);
int   fsm_step(FSM* fsm, int input);
int   fsm_step_output(const FSM* fsm);
void  fsm_simulate(FSM* fsm, const int* inputs, int length);

#endif
