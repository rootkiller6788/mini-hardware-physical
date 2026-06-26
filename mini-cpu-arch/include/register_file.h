#ifndef REGISTER_FILE_H
#define REGISTER_FILE_H

#include <stdbool.h>
#include <stdint.h>

#define REGF_SIZE       32
#define REGF_READ_PORTS 4
#define REGF_WRITE_PORTS 2

typedef struct {
    uint32_t regs[REGF_SIZE];
    uint32_t read_ports[REGF_READ_PORTS];
    bool     read_valid[REGF_READ_PORTS];
    uint32_t write_ports[REGF_WRITE_PORTS];
    uint8_t  write_addrs[REGF_WRITE_PORTS];
    bool     write_en[REGF_WRITE_PORTS];
} RegisterFile;

void regf_init(RegisterFile* rf);
uint32_t regf_read(const RegisterFile* rf, uint8_t index);
void     regf_write(RegisterFile* rf, uint8_t index, uint32_t value);
void     regf_dump(const RegisterFile* rf);
void     regf_clock(RegisterFile* rf);

#endif
