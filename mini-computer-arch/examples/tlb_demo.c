#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "virtual_memory.h"

#define NUM_PAGES   1024
#define PAGE_SIZE   4096
#define TLB_SIZE    16

static uint32_t test_addresses[] = {
    0x00000000, 0x00001000, 0x00002000, 0x00003000,
    0x00000040, 0x00001040, 0x00002040, 0x00000080,
    0x00004000, 0x00005000, 0x00000000, 0x00001000,
    0x00006000, 0x00007000, 0x00002000, 0x00003000,
    0x000000C0, 0x000010C0, 0x00008000, 0x00009000,
};
#define TEST_COUNT (sizeof(test_addresses) / sizeof(test_addresses[0]))

int main(void)
{
    PageTable pt;
    TLB tlb;

    printf("========================================\n");
    printf("  Virtual Memory + TLB Demo\n");
    printf("========================================\n");
    printf("  Page Table: %u pages x %u bytes\n", NUM_PAGES, PAGE_SIZE);
    printf("  TLB:        %u entries, LRU policy\n", TLB_SIZE);
    printf("========================================\n\n");

    vm_init(&pt, NUM_PAGES, PAGE_SIZE);
    tlb_init(&tlb, TLB_SIZE, TLB_LRU);

    printf("Translating virtual addresses...\n\n");
    printf("%-6s %-14s %-10s %-10s %-10s %-14s\n",
           "Step", "VA", "VPN", "Offset", "TLB", "PA");
    printf("------------------------------------------------------------\n");

    for (size_t i = 0; i < TEST_COUNT; i++) {
        uint32_t va = test_addresses[i];
        uint32_t vpn = vm_get_vpn(va, pt.page_size);
        uint32_t offset = vm_get_offset(va, pt.page_size);
        uint32_t pfn = 0;
        const char *tlb_result;

        if (tlb_lookup(&tlb, vpn, &pfn)) {
            tlb_result = "HIT";
        } else {
            tlb_result = "MISS";
            uint32_t pa = vm_translate(&pt, va);
            pfn = pa / pt.page_size;
            tlb_insert(&tlb, vpn, pfn);
        }

        uint32_t physical_addr = (pfn * pt.page_size) + offset;

        printf("[%2zu]   VA=0x%08X  VPN=%4u  Off=%4u  TLB=%-4s  PA=0x%08X\n",
               i + 1, va, vpn, offset, tlb_result, physical_addr);
    }

    printf("\n");
    tlb_print_stats(&tlb);
    printf("\n");
    vm_print_page_table(&pt);

    free(pt.entries);

    return 0;
}
