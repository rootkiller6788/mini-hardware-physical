/**
 * fsm.h — L2: Finite State Machine Definitions
 *
 * Knowledge coverage:
 *   L1: FSMType, FSMState, FSMTransition, FSM structs
 *   L2: Moore vs Mealy machines, state encoding
 *   L3: State transition table, state graph representation
 *   L4: Myhill-Nerode theorem, state equivalence classes
 *   L5: State minimization (partition refinement), pattern matching FSM
 *
 * References:
 *   MIT 6.004 — FSMs (L6)
 *   Berkeley CS 150 — Finite State Machines
 *   Kohavi & Jha — Switching and Finite Automata Theory
 */
#ifndef FSM_H
#define FSM_H

#include <stdbool.h>

#define FSM_MAX_STATES        64
#define FSM_MAX_TRANSITIONS   256
#define FSM_MAX_NAME_LEN      64
#define FSM_MAX_INPUTS         16
#define FSM_MAX_OUTPUTS        16

/* ---- L1: FSM Types ---- */

/** FSM model type:
 *  MOORE: output depends only on current state
 *  MEALY: output depends on current state AND input */
typedef enum {
    FSM_MOORE,
    FSM_MEALY
} FSMType;

/* ---- L1: FSM Building Blocks ---- */

/** FSMState: a named state with Moore output value */
typedef struct {
    char    name[FSM_MAX_NAME_LEN];
    int     moore_output;    /* output when in this state (Moore only) */
    bool    is_initial;      /* true for reset state */
    bool    is_accept;       /* true for accept/final state */
    int     encoding;        /* state encoding (binary, one-hot, gray) */
} FSMState;

/** FSMTransition: edge from state to state on input, with Mealy output */
typedef struct {
    int     from_state;      /* source state index */
    int     input;           /* input symbol */
    int     to_state;        /* destination state index */
    int     mealy_output;    /* output during this transition (Mealy only) */
} FSMTransition;

/** FSM: the complete finite state machine */
typedef struct {
    FSMType         type;
    FSMState        states[FSM_MAX_STATES];
    int             state_count;
    FSMTransition   transitions[FSM_MAX_TRANSITIONS];
    int             trans_count;
    int             current_state;
    int             last_output;
} FSM;

/* ---- L5: State Encoding ---- */

typedef enum {
    ENC_BINARY,      /* sequential binary: 000, 001, 010, 011, ... */
    ENC_ONE_HOT,     /* one bit per state: 0001, 0010, 0100, 1000 */
    ENC_GRAY         /* Gray code ordering */
} StateEncoding;

/* ---- L3: State Graph (adjacency list for analysis) ---- */

#define FSM_GRAPH_MAX_PATHS 512

typedef struct {
    int     from;
    int     input;
    int     to;
    int     output;
} FSMEdge;

typedef struct {
    int     num_states;
    int     num_inputs;
    int     num_edges;
    FSMEdge edges[FSM_GRAPH_MAX_PATHS];
} FSMGraph;

/* ---- L1: API ---- */

/** Create an empty FSM of given type. Complexity: O(1) */
FSM     fsm_create(FSMType type);

/** Add a state. Returns state index or -1 on overflow. Complexity: O(1) */
int     fsm_add_state(FSM* fsm, const char* name, int moore_output,
                       bool is_initial, bool is_accept);

/** Add a state transition. Complexity: O(1) */
void    fsm_add_transition(FSM* fsm, int from, int input, int to,
                           int mealy_output);

/** Reset FSM to initial state. Complexity: O(S) to find initial state */
void    fsm_reset(FSM* fsm);

/** Step FSM with one input. Returns new state index.
 *  Updates current_state and last_output.
 *  Complexity: O(T) where T = number of transitions */
int     fsm_step(FSM* fsm, int input);

/** Get output for current state (Moore) or last transition (Mealy).
 *  Complexity: O(1) */
int     fsm_get_output(const FSM* fsm);

/** Simulate FSM with a sequence of inputs. Prints state trace.
 *  Complexity: O(L * T) where L = input length */
void    fsm_simulate(FSM* fsm, const int* inputs, int length);

/** Check if current state is an accept state. Complexity: O(1) */
bool    fsm_is_accept(const FSM* fsm);

/** Count number of states. Complexity: O(1) */
int     fsm_state_count(const FSM* fsm);

/* ---- L5: State Minimization (Partition Refinement) ---- */

/** Minimize a DFA using partition refinement (Hopcroft's algorithm
 *  adapted for small state machines). Returns number of states after
 *  minimization. The FSM is modified in place.
 *  Complexity: O(S^2 * I) where S = states, I = input alphabet size */
int     fsm_minimize(FSM* fsm);

/** Merge equivalent states. Two states are equivalent if they have
 *  the same output and transitions to equivalent states on all inputs.
 *  Returns the number of merged state pairs. Complexity: O(S^2) */
int     fsm_merge_equivalent(FSM* fsm);

/** Convert Moore FSM to Mealy (and vice versa).
 *  Returns true on success. Complexity: O(S * T) */
bool    fsm_convert_type(FSM* fsm, FSMType new_type);

/* ---- L3: State Graph Operations ---- */

/** Build adjacency graph from FSM. Complexity: O(T) */
FSMGraph fsm_to_graph(const FSM* fsm);

/** Find all reachable states from initial. Returns count.
 *  Uses BFS. Complexity: O(S + T) */
int     fsm_reachable_states(const FSM* fsm, int* reachable, int max_states);

/** Check if FSM is fully specified (has transition for every
 *  state/input pair). Returns true if complete. Complexity: O(S * I) */
bool    fsm_is_complete(const FSM* fsm, int num_inputs);

/** Print state transition table. Complexity: O(S * T) */
void    fsm_print_table(const FSM* fsm);

/** Print FSM as DOT graph for visualization. Complexity: O(S + T) */
void    fsm_print_dot(const FSM* fsm);

/* ---- L6: Pattern Matching FSM ---- */

/** Build a string-matching FSM for a given pattern (KMP automaton).
 *  Pattern is a string of characters; FSM accepts when pattern found.
 *  Returns the FSM ready for simulation.
 *  Complexity: O(M) where M = pattern length */
FSM     fsm_kmp_build(const char* pattern);

/** Build a regex-like FSM for simple patterns:
 *  Supports: literal chars, . (any), * (Kleene star), | (alternation)
 *  Returns the constructed FSM.
 *  Complexity: O(N) where N = pattern length */
FSM     fsm_regex_build(const char* pattern);

#endif /* FSM_H */
