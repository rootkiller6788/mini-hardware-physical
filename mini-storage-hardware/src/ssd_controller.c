#include "ssd_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static bool cq_is_full(const CommandQueue *cq) {
    return cq->count >= SSDC_QUEUE_DEPTH;
}

static bool cq_is_empty(const CommandQueue *cq) {
    return cq->count == 0;
}

static int cq_enqueue(CommandQueue *cq, const IOCommand *cmd) {
    if (cq_is_full(cq)) return -1;
    cq->queue[cq->tail] = *cmd;
    cq->tail = (cq->tail + 1) % SSDC_QUEUE_DEPTH;
    cq->count++;
    return 0;
}

static int cq_dequeue(CommandQueue *cq, IOCommand *out_cmd) {
    if (cq_is_empty(cq)) return -1;
    *out_cmd = cq->queue[cq->head];
    cq->head = (cq->head + 1) % SSDC_QUEUE_DEPTH;
    cq->count--;
    return 0;
}

void ssdc_init(SSDController *ctrl) {
    uint32_t i;

    memset(ctrl, 0, sizeof(SSDController));
    ctrl->current_cycle = 0;
    ctrl->busy_channels = 0;

    for (i = 0; i < SSDC_CHANNELS; i++) {
        ctrl->channels[i].busy_cycles   = 0;
        ctrl->channels[i].transfer_rate = SSDC_TRANSFER_RATE;
    }

    ftl_init(&ctrl->ftl, FTL_MAPPING_PAGE_LEVEL);
}

int ssdc_submit_io(SSDController *ctrl, IOType type, uint32_t lba,
                   const uint8_t *data) {
    IOCommand cmd;
    cmd.type   = type;
    cmd.lba    = lba;
    cmd.length = 1;
    cmd.status = IO_PENDING;
    cmd.cmd_id = ctrl->issue_queue.count;

    if (data && (type == IO_WRITE)) {
        memcpy(cmd.data, data, 4096);
    } else {
        memset(cmd.data, 0, 4096);
    }

    return cq_enqueue(&ctrl->issue_queue, &cmd);
}

void ssdc_process(SSDController *ctrl, uint64_t cycles) {
    uint64_t target = ctrl->current_cycle + cycles;

    while (ctrl->current_cycle < target) {
        while (!cq_is_empty(&ctrl->issue_queue) && ctrl->busy_channels < SSDC_CHANNELS) {
            IOCommand cmd;
            if (cq_dequeue(&ctrl->issue_queue, &cmd) == 0) {
                cmd.status = IO_ISSUED;
                ctrl->busy_channels++;

                switch (cmd.type) {
                case IO_READ: {
                    int rc = ftl_read(&ctrl->ftl, cmd.lba, cmd.data);
                    cmd.status = (rc == 0) ? IO_COMPLETE : IO_ERROR;
                    break;
                }
                case IO_WRITE: {
                    int rc = ftl_write(&ctrl->ftl, cmd.lba, cmd.data);
                    cmd.status = (rc == 0) ? IO_COMPLETE : IO_ERROR;
                    break;
                }
                case IO_ERASE:
                    cmd.status = IO_COMPLETE;
                    break;
                }

                cq_enqueue(&ctrl->completion_queue, &cmd);
                ctrl->busy_channels--;
            }
        }

        ctrl->current_cycle++;
    }
}

int ssdc_complete(SSDController *ctrl, IOCommand *out_cmd) {
    return cq_dequeue(&ctrl->completion_queue, out_cmd);
}

void ssdc_print_queue(const SSDController *ctrl) {
    printf("SSD Controller State:\n");
    printf("  Current Cycle:      %llu\n",
           (unsigned long long)ctrl->current_cycle);
    printf("  Issue Queue Depth:  %u / %u\n",
           ctrl->issue_queue.count, SSDC_QUEUE_DEPTH);
    printf("  Completion Queue:   %u / %u\n",
           ctrl->completion_queue.count, SSDC_QUEUE_DEPTH);
    printf("  Busy Channels:      %llu / %u\n",
           (unsigned long long)ctrl->busy_channels, SSDC_CHANNELS);
    printf("  SRAM Used:          %u / %u bytes\n",
           ctrl->sram_used, SSDC_SRAM_SIZE);
    ftl_print_stats(&ctrl->ftl);
}
