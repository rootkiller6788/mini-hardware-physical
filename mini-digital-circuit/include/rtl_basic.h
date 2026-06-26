#ifndef RTL_BASIC_H
#define RTL_BASIC_H

#include <stdbool.h>
#include <stdint.h>

#define RTL_MAX_PORTS   32
#define RTL_MAX_SIGNALS 64
#define RTL_MAX_NAME    32

typedef enum {
    RTL_PORT_IN,
    RTL_PORT_OUT
} RTLPortDir;

typedef struct {
    char       name[RTL_MAX_NAME];
    RTLPortDir direction;
    int        width;
} RTLPort;

typedef struct {
    char     name[RTL_MAX_NAME];
    int      width;
    uint64_t value;
} RTLSignal;

typedef struct {
    char       name[RTL_MAX_NAME];
    RTLPort    ports[RTL_MAX_PORTS];
    int        port_count;
    RTLSignal  signals[RTL_MAX_SIGNALS];
    int        signal_count;
} RTLModule;

RTLModule rtl_mod_create(const char* name);
void      rtl_add_port(RTLModule* m, const char* name, RTLPortDir dir, int width);
void      rtl_set_signal(RTLModule* m, const char* name, uint64_t value);
uint64_t  rtl_get_signal(const RTLModule* m, const char* name);
void      rtl_evaluate(RTLModule* m);

/* Common RTL building blocks */
RTLModule rtl_mux_create(const char* name, int n_inputs, int sel_width);
RTLModule rtl_decoder_create(const char* name, int n_inputs);
RTLModule rtl_encoder_create(const char* name, int n_inputs);

#endif
