#include "ftl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

int main(void) {
    FTL ftl;
    uint8_t write_buf[4096];
    uint8_t read_buf[4096];
    uint32_t i;

    printf("=== FTL Demo: Flash Translation Layer Simulator ===\n\n");

    ftl_init(&ftl, FTL_MAPPING_PAGE_LEVEL);
    printf("[1] FTL initialized with page-level mapping\n");
    printf("    Max LBAs: %d, Max Physical Pages: %d\n\n",
           FTL_MAX_LBAS, FTL_MAX_PHYSICAL_PAGES);

    printf("[2] Sequential Write: Writing LBAs 0-9\n");
    for (i = 0; i < 10; i++) {
        memset(write_buf, (int)(i + 0xA0), 4096);
        ftl_write(&ftl, i, write_buf);
        printf("    Written LBA %u\n", i);
    }

    printf("\n[3] Sequential Read: Reading LBAs 0-9\n");
    for (i = 0; i < 10; i++) {
        if (ftl_read(&ftl, i, read_buf) == 0) {
            printf("    LBA %u: first byte = 0x%02X (expected 0x%02X)  %s\n",
                   i, read_buf[0], (uint8_t)(i + 0xA0),
                   read_buf[0] == (uint8_t)(i + 0xA0) ? "OK" : "FAIL");
        }
    }

    printf("\n[4] Overwrite: Rewrite LBAs 0-4 with new data\n");
    for (i = 0; i < 5; i++) {
        memset(write_buf, (int)(i + 0xF0), 4096);
        ftl_write(&ftl, i, write_buf);
        printf("    Overwritten LBA %u (old page now INVALID)\n", i);
    }

    printf("\n[5] TRIM Operation: Trim LBAs 5-9\n");
    for (i = 5; i < 10; i++) {
        ftl_trim(&ftl, i);
        printf("    Trimmed LBA %u\n", i);
    }

    printf("\n[6] Write Amplification Demo: Random writes to LBAs 0-9\n");
    srand((unsigned int)time(NULL));
    uint64_t writes_before = ftl.stats.writes;
    for (i = 0; i < 100; i++) {
        uint32_t random_lba = (uint32_t)(rand() % 10);
        memset(write_buf, (int)(i & 0xFF), 4096);
        ftl_write(&ftl, random_lba, write_buf);
    }
    uint64_t writes_after = ftl.stats.writes;
    printf("    100 random writes: %llu physical writes (WA=%.2f)\n\n",
           (unsigned long long)(writes_after - writes_before),
           (double)(writes_after - writes_before) / 100.0);

    ftl_print_stats(&ftl);

    printf("\n=== FTL Demo Complete ===\n");
    return 0;
}
