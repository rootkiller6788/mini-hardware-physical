#ifndef SSD_CONTROLLER_H
#define SSD_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "ftl.h"

#define SSDC_CHANNELS        4
#define SSDC_PLANES_PER_CH   4
#define SSDC_QUEUE_DEPTH     64
#define SSDC_SRAM_SIZE       (1024 * 1024)
#define SSDC_NAND_READ_NS    50000UL
#define SSDC_NAND_WRITE_NS   900000UL
#define SSDC_NAND_ERASE_NS   3000000UL
#define SSDC_TRANSFER_RATE   400

typedef enum {
    IO_READ,
    IO_WRITE,
    IO_ERASE
} IOType;

typedef enum {
    IO_PENDING,
    IO_ISSUED,
    IO_COMPLETE,
    IO_ERROR
} IOStatus;

typedef struct {
    FlashPlane planes[SSDC_PLANES_PER_CH];
    uint64_t   busy_cycles;
    uint32_t   transfer_rate;
} NANDChannel;

typedef struct {
    IOType     type;
    uint32_t   lba;
    uint32_t   length;
    uint8_t    data[4096];
    IOStatus   status;
    uint32_t   cmd_id;
} IOCommand;

typedef struct {
    IOCommand queue[SSDC_QUEUE_DEPTH];
    uint32_t  head;
    uint32_t  tail;
    uint32_t  count;
} CommandQueue;

typedef struct {
    NANDChannel channels[SSDC_CHANNELS];
    CommandQueue issue_queue;
    CommandQueue completion_queue;
    uint8_t      sram[SSDC_SRAM_SIZE];
    uint32_t     sram_used;
    FTL          ftl;
    uint64_t     current_cycle;
    uint64_t     next_read_ready;
    uint64_t     next_write_ready;
    uint64_t     busy_channels;
} SSDController;

void ssdc_init(SSDController *ctrl);
int  ssdc_submit_io(SSDController *ctrl, IOType type, uint32_t lba,
                    const uint8_t *data);
void ssdc_process(SSDController *ctrl, uint64_t cycles);
int  ssdc_complete(SSDController *ctrl, IOCommand *out_cmd);
void ssdc_print_queue(const SSDController *ctrl);

#endif
