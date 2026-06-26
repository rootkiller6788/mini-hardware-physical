#include "register_file.h"
#include <stdio.h>
#include <string.h>

void regf_init(RegisterFile* rf) {
    if (!rf) return;
    memset(rf->regs, 0, sizeof(rf->regs));
    memset(rf->read_ports, 0, sizeof(rf->read_ports));
    memset(rf->read_valid, 0, sizeof(rf->read_valid));
    memset(rf->write_ports, 0, sizeof(rf->write_ports));
    memset(rf->write_addrs, 0, sizeof(rf->write_addrs));
    memset(rf->write_en, 0, sizeof(rf->write_en));
}

uint32_t regf_read(const RegisterFile* rf, uint8_t index) {
    if (!rf || index >= REGF_SIZE) return 0;
    if (index == 0) return 0;
    return rf->regs[index];
}

void regf_write(RegisterFile* rf, uint8_t index, uint32_t value) {
    if (!rf || index >= REGF_SIZE) return;
    if (index == 0) return;
    rf->regs[index] = value;
}

void regf_clock(RegisterFile* rf) {
    if (!rf) return;
    for (int i = 0; i < REGF_WRITE_PORTS; i++) {
        if (rf->write_en[i]) {
            regf_write(rf, rf->write_addrs[i], rf->write_ports[i]);
            rf->write_en[i] = false;
        }
    }
}

void regf_dump(const RegisterFile* rf) {
    if (!rf) return;
    printf("--- Register File (32 registers) ---\n");
    for (int i = 0; i < REGF_SIZE; i++) {
        printf("  x%-2d = 0x%08X  (%u)", i, rf->regs[i], rf->regs[i]);
        if ((i + 1) % 4 == 0) printf("\n");
    }
    printf("\n-------------------------------------\n");
}
