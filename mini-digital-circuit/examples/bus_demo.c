#include <stdio.h>
#include "bus_arbiter.h"

int main(void) {
    printf("===== Bus Arbitration Demo =====\n\n");
    printf("--- Fixed Priority ---\n");
    BusArbiter fp = bus_arbiter_create(ARB_FIXED_PRIORITY, 4);
    bus_arbiter_request(&fp, 2); bus_arbiter_request(&fp, 0);
    int grant = bus_arbiter_arbitrate(&fp);
    printf("M0 and M2 request -> Grant: M%d\n", grant);
    bus_arbiter_print_state(&fp);

    printf("\n--- Round Robin ---\n");
    BusArbiter rr = bus_arbiter_create(ARB_ROUND_ROBIN, 4);
    bus_arbiter_request(&rr, 0); bus_arbiter_request(&rr, 1);
    bus_arbiter_request(&rr, 2); bus_arbiter_request(&rr, 3);
    for (int i = 0; i < 6; i++) {
        int g = bus_arbiter_arbitrate(&rr);
        printf("Cycle %d: Grant M%d\n", i, g);
        if (g >= 0) { bus_arbiter_release(&rr, g); bus_arbiter_request(&rr, g); }
    }

    printf("\n--- TDMA ---\n");
    BusArbiter tdma = bus_arbiter_create(ARB_TDMA, 4);
    bus_arbiter_tdma_configure(&tdma, 4);
    bus_arbiter_request(&tdma, 0); bus_arbiter_request(&tdma, 2);
    for (int i = 0; i < 8; i++) {
        int g = bus_arbiter_arbitrate(&tdma);
        printf("Slot %d: Grant M%d\n", i, g);
    }
    printf("\nBus demo complete.\n");
    return 0;
}
