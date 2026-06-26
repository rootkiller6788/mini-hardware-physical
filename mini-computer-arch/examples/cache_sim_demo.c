#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"

#define L1_SIZE     (32 * 1024)
#define LINE_SIZE   64
#define ASSOC       8

static uint32_t access_trace[] = {
    0x00001000, 0x00001040, 0x00001080, 0x00001000,
    0x00002000, 0x00002040, 0x00002080, 0x00002000,
    0x00001000, 0x00001040, 0x00003000, 0x00003040,
    0x00004000, 0x00001000, 0x00002000, 0x00004040,
    0x00005000, 0x00005040, 0x00005000, 0x00001080,
    0x00006000, 0x00006040, 0x00006080, 0x00006000,
    0x00001000, 0x00002000, 0x00003000, 0x00004000,
    0x00005000, 0x00006000, 0x00001040, 0x00002040,
};
#define TRACE_SIZE (sizeof(access_trace) / sizeof(access_trace[0]))

int main(void)
{
    Cache l1;
    uint8_t data_buffer[8];

    printf("========================================\n");
    printf("  Cache Simulator Demo\n");
    printf("  L1 Cache: 32KB, 64B lines, 8-way LRU\n");
    printf("========================================\n\n");

    cache_init(&l1, L1_SIZE, LINE_SIZE, ASSOC, LRU, WRITE_BACK);

    printf("Running memory access trace (%u accesses)...\n\n", (unsigned)TRACE_SIZE);

    for (size_t i = 0; i < TRACE_SIZE; i++) {
        uint32_t addr = access_trace[i];
        uint32_t tag, index, offset;
        cache_decompose_address(&l1, addr, &tag, &index, &offset);

        bool hit = cache_read(&l1, addr, data_buffer);

        printf("  [%2zu] Addr=0x%08X  Tag=0x%04X  Set=%3u  Off=%u  -> %s\n",
               i + 1, addr, tag, index, offset,
               hit ? "HIT " : "MISS");
    }

    printf("\n");
    cache_print_stats(&l1);

    printf("\nAMAT (1ns hit, 50ns miss penalty): %.2f ns\n",
           cache_amat(&l1, 1.0, 50.0));

    free(l1.sets);

    return 0;
}
