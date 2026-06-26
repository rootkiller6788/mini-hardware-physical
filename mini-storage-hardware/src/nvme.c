#include "nvme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static bool sq_is_full(const NVMeQueue *sq) {
    uint32_t count;
    if (sq->tail >= sq->head) {
        count = sq->tail - sq->head;
    } else {
        count = sq->depth - sq->head + sq->tail;
    }
    return count >= sq->depth - 1;
}

static bool sq_is_empty(const NVMeQueue *sq) {
    return sq->head == sq->tail;
}

void nvme_init(NVMeController *ctrl) {
    uint32_t i;

    memset(ctrl, 0, sizeof(NVMeController));

    ctrl->admin_sq.id    = 0;
    ctrl->admin_sq.depth = NVME_QUEUE_DEPTH;
    ctrl->admin_sq.head  = 0;
    ctrl->admin_sq.tail  = 0;

    ctrl->admin_cq.id    = 0;
    ctrl->admin_cq.depth = NVME_QUEUE_DEPTH;
    ctrl->admin_cq.head  = 0;
    ctrl->admin_cq.tail  = 0;

    for (i = 0; i < NVME_MAX_IO_QUEUES; i++) {
        ctrl->io_sqs[i].id    = i + 1;
        ctrl->io_sqs[i].depth = NVME_QUEUE_DEPTH;
        ctrl->io_sqs[i].head  = 0;
        ctrl->io_sqs[i].tail  = 0;

        ctrl->io_cqs[i].id    = i + 1;
        ctrl->io_cqs[i].depth = NVME_QUEUE_DEPTH;
        ctrl->io_cqs[i].head  = 0;
        ctrl->io_cqs[i].tail  = 0;
    }

    for (i = 0; i < NVME_MAX_NAMESPACES; i++) {
        ctrl->ns_size[i] = 1024 * 1024 * 1024;
    }
}

static int sq_enqueue(NVMeQueue *sq, const NVMeCommand *cmd) {
    if (sq_is_full(sq)) return -1;
    sq->entries[sq->tail] = *cmd;
    sq->tail = (sq->tail + 1) % sq->depth;
    return 0;
}

int nvme_submit_admin_cmd(NVMeController *ctrl, NVMeCommand *cmd) {
    if (sq_enqueue(&ctrl->admin_sq, cmd) != 0) return -1;
    ctrl->sq_tail_doorbell[0] = ctrl->admin_sq.tail;
    return 0;
}

int nvme_submit_io_cmd(NVMeController *ctrl, uint32_t qid, NVMeCommand *cmd) {
    if (qid >= NVME_MAX_IO_QUEUES) return -1;
    if (sq_enqueue(&ctrl->io_sqs[qid], cmd) != 0) return -1;
    ctrl->sq_tail_doorbell[qid + 1] = ctrl->io_sqs[qid].tail;
    return 0;
}

int nvme_process_cq(NVMeController *ctrl, uint32_t qid,
                    NVMeCompletion *out_comp) {
    NVMeQueue *cq;

    if (qid == 0) {
        cq = &ctrl->admin_cq;
    } else if (qid <= NVME_MAX_IO_QUEUES) {
        cq = &ctrl->io_cqs[qid - 1];
    } else {
        return -1;
    }

    if (sq_is_empty(cq)) return -1;

    memset(out_comp, 0, sizeof(NVMeCompletion));
    out_comp->sq_head_ptr = cq->head;
    out_comp->sq_id       = (uint16_t)qid;
    out_comp->status      = (uint16_t)NVME_SC_SUCCESS;

    cq->head = (cq->head + 1) % cq->depth;
    ctrl->cq_head_doorbell[qid] = cq->head;

    return 0;
}

/* ── PRP (Physical Region Page) List ──
 *
 * L3: NVMe uses PRP or SGL for scatter-gather DMA.
 * PRP list is a page-sized array of 64-bit physical addresses.
 * Defined in nvme.h.
 */

void nvme_prp_list_init(PRPList *prp) {
    memset(prp, 0, sizeof(PRPList));
}

int nvme_prp_add_entry(PRPList *prp, uint64_t phys_addr) {
    if (prp->count >= NVME_PRP_ENTRIES_PER_PAGE) return -1;
    prp->entries[prp->count++] = phys_addr;
    return 0;
}

/* Build PRP from a logical buffer (simplified) */
int nvme_build_prp(const uint8_t *buffer, uint32_t length,
                   PRPList *prp) {
    uint32_t offset = 0;
    uint32_t i;

    nvme_prp_list_init(prp);
    (void)buffer;

    /* Simulate PRP entries for logical addresses */
    for (i = 0; offset < length && prp->count < NVME_PRP_ENTRIES_PER_PAGE; i++) {
        prp->entries[prp->count] = (uint64_t)(uintptr_t)(buffer + offset);
        prp->count++;
        offset += NVME_SECTOR_SIZE;
    }
    return 0;
}

/* ── Identify Controller Data ──
 *
 * L1/L3: NVMe Identify command returns controller capabilities.
 * Defined in nvme.h per NVMe 1.4 specification.
 */

void nvme_identify_controller(IdentifyController *id) {
    memset(id, 0, sizeof(IdentifyController));
    id->pci_vid  = 0x144D;
    id->pci_svid = 0x144D;
    strncpy(id->sn, "S4EUNG0M123456", sizeof(id->sn));
    strncpy(id->mn, "NVMe-mini SSD Controller", sizeof(id->mn));
    strncpy(id->fr, "1.0.0", sizeof(id->fr));
    id->mdts    = 5;
    id->ver     = 0x00010400;
    id->nn      = NVME_MAX_NAMESPACES;
    id->oncs    = 0x001F;
    id->sqes    = 0x66;
    id->cqes    = 0x44;
    id->maxcmd  = 0;
}

void nvme_print_identify(const IdentifyController *id) {
    printf("NVMe Identify Controller:\n");
    printf("  PCI VID:  0x%04X\n", id->pci_vid);
    printf("  Model:    %.40s\n", id->mn);
    printf("  Serial:   %.20s\n", id->sn);
    printf("  Firmware: %.8s\n", id->fr);
    printf("  Version:  %u.%u.%u\n",
           (id->ver >> 16) & 0xFFFF,
           (id->ver >> 8) & 0xFF,
           id->ver & 0xFF);
    printf("  Max Data Transfer Size: %u pages\n", 1u << id->mdts);
    printf("  Number of Namespaces: %u\n", id->nn);
}

/* ── Namespace Management ──
 *
 * L3: NVMe namespaces are logical block address ranges.
 * Defined in nvme.h.
 */

void nvme_identify_namespace(IdentifyNamespace *ns, uint32_t nsid,
                             uint64_t ns_size) {
    memset(ns, 0, sizeof(IdentifyNamespace));
    ns->nsze  = ns_size / NVME_SECTOR_SIZE;
    ns->ncap  = ns->nsze;
    ns->nuse  = ns->nsze;
    ns->nlbaf = 1;
    ns->flbas = 0;
    (void)nsid;
}

/* ── Round-Robin/Weighted Round-Robin Arbitration ──
 *
 * L5: NVMe supports RR and WRR arbitration for SQ scheduling.
 * Defined in nvme.h.
 */
#define NVME_AB_DEFAULT 64

void nvme_arbiter_init(NVMeArbiter *arb, uint32_t burst) {
    uint32_t i;
    memset(arb, 0, sizeof(NVMeArbiter));
    arb->arb_burst = burst;
    for (i = 0; i < NVME_MAX_IO_QUEUES; i++) {
        arb->weights[i] = 0;        /* 0 = round-robin */
        arb->remaining_burst[i] = burst;
    }
}

void nvme_arbiter_set_weight(NVMeArbiter *arb, uint32_t qid, uint8_t weight) {
    if (qid >= NVME_MAX_IO_QUEUES) return;
    arb->weights[qid] = weight;
}

/* Select next SQ to process based on WRR */
int nvme_arbiter_next_sq(NVMeArbiter *arb) {
    uint32_t i;
    uint32_t start = arb->current_sq;

    for (i = 0; i < NVME_MAX_IO_QUEUES; i++) {
        uint32_t qid = (start + i) % NVME_MAX_IO_QUEUES;

        if (arb->weights[qid] == 0) {
            /* RR mode: each queue gets one turn */
            if (arb->remaining_burst[qid] > 0) {
                arb->remaining_burst[qid]--;
                if (arb->remaining_burst[qid] == 0)
                    arb->remaining_burst[qid] = arb->arb_burst;
                arb->current_sq = (qid + 1) % NVME_MAX_IO_QUEUES;
                return (int)qid;
            }
            arb->remaining_burst[qid] = arb->arb_burst;
        } else {
            /* WRR mode: process weight-based credits */
            uint32_t credits = arb->weights[qid];
            if (arb->remaining_burst[qid] > 0) {
                uint32_t take = (credits < arb->remaining_burst[qid])
                                ? credits : arb->remaining_burst[qid];
                arb->remaining_burst[qid] -= take;
                if (arb->remaining_burst[qid] == 0)
                    arb->remaining_burst[qid] = arb->arb_burst;
                arb->current_sq = (qid + 1) % NVME_MAX_IO_QUEUES;
                return (int)qid;
            }
            arb->remaining_burst[qid] = arb->arb_burst;
        }
    }
    return -1;  /* no queue available */
}

void nvme_print_regs(const NVMeController *ctrl) {
    uint32_t i;

    printf("NVMe Controller Registers:\n");
    printf("  Admin SQ: head=%u tail=%u depth=%u\n",
           ctrl->admin_sq.head, ctrl->admin_sq.tail, ctrl->admin_sq.depth);
    printf("  Admin CQ: head=%u tail=%u depth=%u\n",
           ctrl->admin_cq.head, ctrl->admin_cq.tail, ctrl->admin_cq.depth);
    printf("  Doorbell - Admin SQ Tail: %u\n", ctrl->sq_tail_doorbell[0]);
    printf("  Doorbell - Admin CQ Head: %u\n", ctrl->cq_head_doorbell[0]);

    for (i = 0; i < NVME_MAX_IO_QUEUES; i++) {
        printf("  I/O SQ[%u]: head=%u tail=%u depth=%u\n",
               i, ctrl->io_sqs[i].head, ctrl->io_sqs[i].tail,
               ctrl->io_sqs[i].depth);
        printf("  I/O CQ[%u]: head=%u tail=%u depth=%u\n",
               i, ctrl->io_cqs[i].head, ctrl->io_cqs[i].tail,
               ctrl->io_cqs[i].depth);
        printf("  Doorbell - I/O SQ[%u] Tail: %u\n",
               i, ctrl->sq_tail_doorbell[i + 1]);
        printf("  Doorbell - I/O CQ[%u] Head: %u\n",
               i, ctrl->cq_head_doorbell[i + 1]);
    }

    for (i = 0; i < NVME_MAX_NAMESPACES; i++) {
        printf("  Namespace[%u] Size: %llu bytes\n",
               i, (unsigned long long)ctrl->ns_size[i]);
    }
}
