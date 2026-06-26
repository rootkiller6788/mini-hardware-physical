#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coherence.h"

int main(void)
{
    CoherenceController ctrl;

    printf("========================================\n");
    printf("  Cache Coherence (MESI) Protocol Demo\n");
    printf("========================================\n");
    printf("  2 cores, each with private L1 cache\n");
    printf("========================================\n\n");

    coherence_init(&ctrl, PROTO_MESI, 2);

    uint8_t buf[64];

    printf("Step 1: Core 0 reads address 0x1000\n");
    coherence_read(&ctrl, 0, 0x1000, buf);
    printf("  Core 0 state after read: ");
    {
        uint32_t idx = (0x1000 / 64) % COHERENCE_CACHE_LINES;
        printf("%s\n", coherence_state_name(ctrl.caches[0].lines[idx].state));
    }
    printf("  (Core 0 gets line in E state - Exclusive, no other copies)\n\n");

    printf("Step 2: Core 1 reads address 0x1000\n");
    coherence_read(&ctrl, 1, 0x1000, buf);
    {
        uint32_t idx = (0x1000 / 64) % COHERENCE_CACHE_LINES;
        printf("  Core 0: %s\n",
               coherence_state_name(ctrl.caches[0].lines[idx].state));
        printf("  Core 1: %s\n",
               coherence_state_name(ctrl.caches[1].lines[idx].state));
    }
    printf("  (Both cores in S state - Shared)\n\n");

    printf("Step 3: Core 0 writes to address 0x1000 (value=0xAB)\n");
    memset(buf, 0xAB, 64);
    coherence_write(&ctrl, 0, 0x1000, buf);
    {
        uint32_t idx = (0x1000 / 64) % COHERENCE_CACHE_LINES;
        printf("  Core 0: %s\n",
               coherence_state_name(ctrl.caches[0].lines[idx].state));
        printf("  Core 1: %s (invalidated!)\n",
               coherence_state_name(ctrl.caches[1].lines[idx].state));
    }
    printf("  (Core 0 in M state, Core 1 invalidated)\n\n");

    printf("Step 4: Core 1 reads address 0x2000 (different line)\n");
    coherence_read(&ctrl, 1, 0x2000, buf);
    {
        uint32_t idx = (0x2000 / 64) % COHERENCE_CACHE_LINES;
        printf("  Core 1 line for 0x2000: %s\n",
               coherence_state_name(ctrl.caches[1].lines[idx].state));
    }
    printf("  (Core 1 gets new line in E state)\n\n");

    printf("Step 5: Core 1 writes to address 0x2000 (value=0xCD)\n");
    memset(buf, 0xCD, 64);
    coherence_write(&ctrl, 1, 0x2000, buf);
    {
        uint32_t idx = (0x2000 / 64) % COHERENCE_CACHE_LINES;
        printf("  Core 1 line for 0x2000: %s\n",
               coherence_state_name(ctrl.caches[1].lines[idx].state));
    }
    printf("  (Core 1 is now in M state for 0x2000)\n\n");

    printf("Step 6: Core 0 reads address 0x1000 again (still in M)\n");
    coherence_read(&ctrl, 0, 0x1000, buf);
    {
        uint32_t idx = (0x1000 / 64) % COHERENCE_CACHE_LINES;
        printf("  Core 0: %s (still hit in its cache)\n",
               coherence_state_name(ctrl.caches[0].lines[idx].state));
    }
    printf("  (Core 0 still has the line in M state)\n\n");

    printf("Step 7: Core 1 tries to read 0x1000 (miss in Core 1, causes snoop)\n");
    coherence_read(&ctrl, 1, 0x1000, buf);
    {
        uint32_t idx = (0x1000 / 64) % COHERENCE_CACHE_LINES;
        printf("  Core 0: %s\n",
               coherence_state_name(ctrl.caches[0].lines[idx].state));
        printf("  Core 1: %s\n",
               coherence_state_name(ctrl.caches[1].lines[idx].state));
    }
    printf("  (Core 0 flushed to shared, Core 1 gets shared copy)\n\n");

    printf("Step 8: Simultaneous addresses - 0x3000\n");
    coherence_read(&ctrl, 0, 0x3000, buf);
    coherence_read(&ctrl, 1, 0x3000, buf);
    {
        uint32_t idx = (0x3000 / 64) % COHERENCE_CACHE_LINES;
        printf("  Core 0: %s\n",
               coherence_state_name(ctrl.caches[0].lines[idx].state));
        printf("  Core 1: %s\n",
               coherence_state_name(ctrl.caches[1].lines[idx].state));
    }
    printf("  (Both shared)\n\n");

    printf("========================================\n");
    printf("  Final State Summary\n");
    printf("========================================\n\n");

    coherence_print_states(&ctrl);
    printf("\n");
    coherence_print_stats(&ctrl);

    printf("\n========================================\n");
    printf("  MESI State Transitions Observed:\n");
    printf("========================================\n");
    printf("  I -> E  : Read miss, no other copies\n");
    printf("  E -> S  : Remote read on same line\n");
    printf("  I -> S  : Read miss, other copies exist\n");
    printf("  S -> M  : Write hit, invalidate others\n");
    printf("  M -> S  : Remote read on modified line\n");
    printf("  S -> I  : Remote write on shared line\n");
    printf("  M -> I  : Remote write on modified line\n");
    printf("========================================\n");

    return 0;
}
