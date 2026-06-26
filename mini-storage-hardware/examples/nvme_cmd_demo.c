#include "nvme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    NVMeController ctrl;
    NVMeCommand cmd;
    NVMeCompletion comp;
    uint32_t i;

    printf("=== NVMe Command Demo ===\n\n");

    nvme_init(&ctrl);
    printf("[1] NVMe Controller initialized\n");
    nvme_print_regs(&ctrl);

    printf("\n[2] Submitting READ commands via I/O SQ[0]...\n");
    for (i = 0; i < 4; i++) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode       = NVME_OP_READ;
        cmd.command_id   = (uint16_t)i;
        cmd.namespace_id = 1;
        cmd.slba         = (uint64_t)(i * 8);
        cmd.nlb          = 7;

        int rc = nvme_submit_io_cmd(&ctrl, 0, &cmd);
        printf("    Submitted READ cmd %u: slba=%llu nlb=%u -> rc=%d\n",
               i, (unsigned long long)cmd.slba, cmd.nlb, rc);
    }

    printf("\n[3] Doorbell registers after submissions:\n");
    printf("    I/O SQ[0] Tail Doorbell: %u\n", ctrl.sq_tail_doorbell[1]);

    printf("\n[4] Submitting WRITE commands via I/O SQ[0]...\n");
    for (i = 0; i < 3; i++) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode       = NVME_OP_WRITE;
        cmd.command_id   = (uint16_t)(i + 4);
        cmd.namespace_id = 1;
        cmd.slba         = (uint64_t)(i * 16);
        cmd.nlb          = 15;

        int rc = nvme_submit_io_cmd(&ctrl, 0, &cmd);
        printf("    Submitted WRITE cmd %u: slba=%llu nlb=%u -> rc=%d\n",
               i, (unsigned long long)cmd.slba, cmd.nlb, rc);
    }

    printf("\n[5] Processing completion queue I/O CQ[0]...\n");
    for (i = 0; i < 7; i++) {
        if (nvme_process_cq(&ctrl, 1, &comp) == 0) {
            printf("    Completion: sq_id=%u sq_head=%u cmd_id=%u status=0x%04x\n",
                   comp.sq_id, comp.sq_head_ptr, comp.command_id, comp.status);
        }
    }

    printf("\n[6] Submitting admin IDENTIFY command...\n");
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode       = NVME_OP_IDENTIFY;
    cmd.command_id   = 100;
    cmd.namespace_id = 0;
    cmd.cdw10        = 1;

    int rc = nvme_submit_admin_cmd(&ctrl, &cmd);
    printf("    Admin IDENTIFY submitted -> rc=%d\n", rc);

    printf("\n[7] Processing admin completion queue...\n");
    if (nvme_process_cq(&ctrl, 0, &comp) == 0) {
        printf("    Admin Completion: sq_id=%u status=0x%04x\n",
               comp.sq_id, comp.status);
    }

    printf("\n[8] Final NVMe Controller State:\n");
    nvme_print_regs(&ctrl);

    printf("\n=== NVMe Command Demo Complete ===\n");
    return 0;
}
