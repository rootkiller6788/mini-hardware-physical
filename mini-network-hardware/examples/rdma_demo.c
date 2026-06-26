#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    printf("=== mini-network-hardware: RDMA Demo ===\n\n");

    RDMAContext ctx;
    rdma_init(&ctx);
    printf("[1] RDMA Context Initialized (PD=%u)\n", ctx.protection_domain);

    printf("\n[2] Memory Region Registration\n");
    uint8_t local_mem1[4096];
    uint8_t local_mem2[4096];
    memset(local_mem1, 0xAB, sizeof(local_mem1));
    memset(local_mem2, 0x00, sizeof(local_mem2));

    if (rdma_reg_mr(&ctx, local_mem1, sizeof(local_mem1)) == 0) {
        printf("    Registered MR for local_mem1 (4096 bytes)\n");
    }
    if (rdma_reg_mr(&ctx, local_mem2, sizeof(local_mem2)) == 0) {
        printf("    Registered MR for local_mem2 (4096 bytes)\n");
    }
    rdma_dump_mr(&ctx);

    printf("\n[3] Queue Pair Creation\n");
    int qpn1 = rdma_create_qp(&ctx, RDMA_QP_RESET);
    int qpn2 = rdma_create_qp(&ctx, RDMA_QP_RESET);
    printf("    Created QP %d (RESET state)\n", qpn1);
    printf("    Created QP %d (RESET state)\n", qpn2);

    printf("\n[4] QP State Transitions\n");
    printf("    Modifying QP %d: RESET -> INIT\n", qpn1);
    rdma_modify_qp(&ctx, qpn1, RDMA_QP_INIT);
    rdma_dump_qp(&ctx, qpn1);

    printf("    Modifying QP %d: INIT -> RTR\n", qpn1);
    rdma_modify_qp(&ctx, qpn1, RDMA_QP_RTR);
    rdma_dump_qp(&ctx, qpn1);

    printf("    Modifying QP %d: RTR -> RTS\n", qpn1);
    rdma_modify_qp(&ctx, qpn1, RDMA_QP_RTS);
    rdma_dump_qp(&ctx, qpn1);

    printf("\n[5] RDMA Remote Write (One-sided, CPU bypass)\n");
    const char *rdma_data = "RDMA WRITE DATA: Zero-copy, kernel bypass!";
    int data_len = (int)strlen(rdma_data) + 1;

    printf("    Source data:      \"%s\"\n", rdma_data);
    printf("    Source length:    %d bytes\n", data_len);
    printf("    Destination MR:   local_mem2\n");
    printf("    Initial dest[0..19]: ");
    for (int i = 0; i < 20; i++) printf("%02x ", local_mem2[i]);
    printf("\n");

    int written = rdma_remote_write(&ctx, qpn1, local_mem2,
                                     rdma_data, data_len);
    printf("    Bytes written:    %d\n", written);

    printf("    After write dest[0..19]: ");
    for (int i = 0; i < 20; i++) printf("%02x ", local_mem2[i]);
    printf("\n");

    printf("\n[6] Completion Queue Polling\n");
    uint32_t result = 0;
    if (rdma_poll_cq(&ctx, &result) == 0) {
        printf("    CQ poll succeeded, result=%u\n", result);
    } else {
        printf("    CQ empty (expected if no work request posted)\n");
    }

    printf("\n[7] RDMA Post Send (SEND op)\n");
    RDMAWQE wqe;
    memset(&wqe, 0, sizeof(wqe));
    wqe.opcode = RDMA_OP_SEND;
    wqe.addr = (uint64_t)(uintptr_t)local_mem1;
    wqe.length = 64;
    wqe.lkey = ctx.memory_regions[0].lkey;
    wqe.rkey = 0;

    if (rdma_post_send(&ctx, qpn1, &wqe) == 0) {
        printf("    WQE posted to QP %d SQ successfully\n", qpn1);
    } else {
        printf("    Failed to post WQE\n");
    }

    if (rdma_poll_cq(&ctx, &result) == 0) {
        printf("    CQ poll: completion result=%u\n", result);
    }

    printf("\n[8] Data Verification\n");
    if (memcmp(local_mem2, rdma_data, (size_t)data_len) == 0) {
        printf("    VERIFIED: Destination contains correct data!\n");
    } else {
        printf("    MISMATCH: Destination data does not match source\n");
    }
    printf("    Dest content: \"%s\"\n", (char *)local_mem2);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
