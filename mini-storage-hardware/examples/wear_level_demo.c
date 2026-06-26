#include "wear_leveling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

int main(void) {
    FTL ftl;
    WearLeveler wl;
    uint8_t data[4096];
    uint32_t i, j;

    printf("=== Wear Leveling Demo ===\n\n");

    ftl_init(&ftl, FTL_MAPPING_PAGE_LEVEL);

    wl_init(&wl, &ftl, WL_DYNAMIC, WL_DEFAULT_THRESHOLD);
    printf("[1] Wear Leveler initialized (DYNAMIC, threshold=%u)\n",
           WL_DEFAULT_THRESHOLD);

    printf("\n[2] Simulating uneven wear: writing LBAs 0-4 repeatedly\n");
    printf("    Most LBAs never touched -> erase counts become uneven\n\n");

    for (j = 0; j < 20; j++) {
        for (i = 0; i < 5; i++) {
            memset(data, (int)(j & 0xFF), 4096);
            ftl_write(&ftl, i, data);
        }
    }

    printf("[3] Stats after uneven writes:\n");
    wl_print_stats(&wl);

    printf("\n[4] Checking if wear leveling is needed...\n");
    if (wl_check_and_balance(&wl)) {
        printf("    Wear leveling triggered! Data movement performed.\n");
    } else {
        printf("    No wear leveling needed (spread within threshold).\n");
    }

    printf("\n[5] Writing more hot data to widen the gap...\n");
    for (j = 0; j < 50; j++) {
        for (i = 0; i < 3; i++) {
            memset(data, (int)(j & 0xFF), 4096);
            ftl_write(&ftl, i, data);
        }
    }

    wl_update_stats(&wl);
    printf("\n[6] Stats after additional hot writes:\n");
    wl_print_stats(&wl);

    printf("\n[7] Wear leveling check again:\n");
    if (wl_check_and_balance(&wl)) {
        printf("    Wear leveling triggered! Data rebalanced.\n");
    }

    wl_update_stats(&wl);
    printf("\n[8] Final Stats:\n");
    wl_print_stats(&wl);

    printf("\n=== Wear Leveling Demo Complete ===\n");
    return 0;
}
