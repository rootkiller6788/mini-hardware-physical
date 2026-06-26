#include "gc.h"
#include "wear_leveling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    FTL ftl;
    GarbageCollector gc;
    uint8_t data[4096];
    uint32_t i, j;

    printf("=== Garbage Collection Demo ===\n\n");

    ftl_init(&ftl, FTL_MAPPING_PAGE_LEVEL);
    gc_init(&gc, &ftl, GC_GREEDY, GC_DEFAULT_OP_PCT, GC_DEFAULT_THRESHOLD);

    printf("[1] GC initialized (GREEDY, OP=%u%%, threshold=%u%%)\n\n",
           GC_DEFAULT_OP_PCT, GC_DEFAULT_THRESHOLD);

    printf("[2] Filling SSD with sequential writes...\n");
    for (i = 0; i < FTL_MAX_LBAS; i++) {
        memset(data, (int)(i & 0xFF), 4096);
        ftl_write(&ftl, i, data);
    }
    printf("    All %u LBAs written. Free pages: %u\n\n",
           FTL_MAX_LBAS, ftl.free_pages);

    printf("[3] Overwriting a small range to create invalid pages...\n");
    for (j = 0; j < 10; j++) {
        for (i = 0; i < 500; i++) {
            memset(data, (int)((j * 500 + i) & 0xFF), 4096);
            ftl_write(&ftl, i, data);
        }
    }
    printf("    10 rounds x 500 overwrites done.\n\n");

    ftl_print_stats(&ftl);

    printf("\n[4] Triggering Garbage Collection...\n");
    int gc_result = gc_trigger(&gc);
    printf("    GC result: %d (victim selected, migrated, erased)\n\n", gc_result);

    printf("[5] Running multiple GC passes...\n");
    for (i = 0; i < 5; i++) {
        int result = gc_trigger(&gc);
        printf("    GC pass %u: result=%d\n", i, result);
    }

    printf("\n[6] Final Metrics:\n");
    gc_print_metrics(&gc);
    ftl_print_stats(&ftl);

    printf("\n=== GC Demo Complete ===\n");
    return 0;
}
