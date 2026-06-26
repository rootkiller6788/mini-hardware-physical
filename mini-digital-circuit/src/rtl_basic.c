#include "rtl_basic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RTLModule rtl_mod_create(const char* name) {
    RTLModule m;
    strncpy(m.name, name, RTL_MAX_NAME - 1);
    m.name[RTL_MAX_NAME - 1] = '\0';
    m.port_count   = 0;
    m.signal_count = 0;
    return m;
}

void rtl_add_port(RTLModule* m, const char* name, RTLPortDir dir, int width) {
    if (m->port_count >= RTL_MAX_PORTS) return;
    RTLPort* p = &m->ports[m->port_count];
    strncpy(p->name, name, RTL_MAX_NAME - 1);
    p->name[RTL_MAX_NAME - 1] = '\0';
    p->direction = dir;
    p->width = width;
    m->port_count++;
}

void rtl_set_signal(RTLModule* m, const char* name, uint64_t value) {
    /* 查找已存在的信号 */
    for (int i = 0; i < m->signal_count; i++) {
        if (strcmp(m->signals[i].name, name) == 0) {
            m->signals[i].value = value;
            return;
        }
    }
    /* 不存在则新建 */
    if (m->signal_count >= RTL_MAX_SIGNALS) return;
    RTLSignal* s = &m->signals[m->signal_count];
    strncpy(s->name, name, RTL_MAX_NAME - 1);
    s->name[RTL_MAX_NAME - 1] = '\0';
    s->width = 8; /* 默认 8 位 */
    s->value = value;
    m->signal_count++;
}

uint64_t rtl_get_signal(const RTLModule* m, const char* name) {
    for (int i = 0; i < m->signal_count; i++) {
        if (strcmp(m->signals[i].name, name) == 0) {
            return m->signals[i].value;
        }
    }
    return 0;
}

void rtl_evaluate(RTLModule* m) {
    (void)m;
    /* 组合逻辑模块此时通过 set_signal/get_signal 来手动完成评估 */
}

/* ---- 多路选择器 (Mux) ---- */
RTLModule rtl_mux_create(const char* name, int n_inputs, int sel_width) {
    RTLModule m = rtl_mod_create(name);
    rtl_add_port(&m, "out", RTL_PORT_OUT, 1);

    for (int i = 0; i < n_inputs; i++) {
        char port_name[16];
        snprintf(port_name, sizeof(port_name), "in%d", i);
        rtl_add_port(&m, port_name, RTL_PORT_IN, 1);
    }
    rtl_add_port(&m, "sel", RTL_PORT_IN, sel_width);
    rtl_set_signal(&m, "out", 0);
    return m;
}

/* ---- 解码器 (Decoder) ---- */
RTLModule rtl_decoder_create(const char* name, int n_inputs) {
    RTLModule m = rtl_mod_create(name);
    int n_outputs = 1 << n_inputs;
    rtl_add_port(&m, "in",  RTL_PORT_IN,  n_inputs);
    for (int i = 0; i < n_outputs; i++) {
        char port_name[16];
        snprintf(port_name, sizeof(port_name), "out%d", i);
        rtl_add_port(&m, port_name, RTL_PORT_OUT, 1);
    }
    return m;
}

/* ---- 编码器 (Encoder) ---- */
RTLModule rtl_encoder_create(const char* name, int n_inputs) {
    RTLModule m = rtl_mod_create(name);
    for (int i = 0; i < n_inputs; i++) {
        char port_name[16];
        snprintf(port_name, sizeof(port_name), "in%d", i);
        rtl_add_port(&m, port_name, RTL_PORT_IN, 1);
    }
    rtl_add_port(&m, "out", RTL_PORT_OUT, 3);
    return m;
}
