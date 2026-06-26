#include "flow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("=== mini-network-hardware: Flow Control Demo ===\n\n");

    printf("[1] 802.3x PAUSE Frame\n");
    uint8_t src[6] = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
    PauseFrame pf;
    pause_frame_build(&pf, src, 1000);
    pause_frame_print(&pf);
    printf("    Pause time at 10G: %.2f us\n\n", 1000.0 * 0.0512);

    printf("[2] PAUSE Frame Parse\n");
    uint8_t raw[64];
    memset(raw, 0, 64);
    raw[0]=0x01; raw[1]=0x80; raw[2]=0xC2; raw[3]=0x00; raw[4]=0x00; raw[5]=0x01;
    memcpy(raw+6, src, 6);
    raw[12]=0x88; raw[13]=0x08;
    raw[14]=0x00; raw[15]=0x01;
    raw[16]=0x03; raw[17]=0xE8;  /* 1000 quanta */
    PauseFrame parsed;
    if (pause_frame_parse(raw, 64, &parsed) == 0) {
        pause_frame_print(&parsed);
    }

    printf("\n[3] PFC (Priority Flow Control)\n");
    PFCState pfc;
    pfc_init(&pfc);
    pfc_set_pause(&pfc, 3, 500);
    pfc_set_pause(&pfc, 7, 200);
    pfc_print_state(&pfc);
    printf("    P3 paused: %s\n", pfc_is_paused(&pfc, 3) ? "YES" : "NO");
    pfc_clear_pause(&pfc, 3);
    printf("    P3 after clear: %s\n\n", pfc_is_paused(&pfc, 3) ? "YES" : "NO");

    printf("[4] Token Bucket Rate Limiter\n");
    TokenBucket tb;
    token_bucket_init(&tb, 125000000.0, 1500.0); /* 1 Gbps, MTU burst */
    uint64_t now = 1000000000ULL; /* 1 second */
    printf("    Rate: %.2f MB/s, Max burst: %.0f bytes\n",
           tb.rate/1e6, tb.max_bucket);
    bool passed = token_bucket_consume(&tb, 1500.0, now);
    printf("    Send 1500B @ t=1s: %s\n", passed ? "PASS" : "DROP");
    passed = token_bucket_consume(&tb, 1500000.0, now);
    printf("    Send 1.5MB burst @ t=1s: %s\n", passed ? "PASS" : "DROP");
    token_bucket_print_stats(&tb);

    printf("\n[5] Leaky Bucket Traffic Shaper\n");
    LeakyBucket lb;
    leaky_bucket_init(&lb, 1000000.0, 50000.0); /* 1 MB/s, 50KB queue */
    leaky_bucket_enqueue(&lb, 10000.0, now);
    leaky_bucket_enqueue(&lb, 10000.0, now);
    leaky_bucket_enqueue(&lb, 40000.0, now); /* This overflows */
    leaky_bucket_print_stats(&lb);

    printf("\n[6] Congestion Control (RED/ECN)\n");
    CongestionControl cc;
    congestion_init(&cc);
    uint64_t action;
    congestion_check(&cc, 0.3, &action);
    printf("    Queue 30%%: action=%llu (0=pass)\n", (unsigned long long)action);
    congestion_check(&cc, 0.6, &action);
    printf("    Queue 60%%: action=%llu (1=mark)\n", (unsigned long long)action);
    congestion_check(&cc, 0.95, &action);
    printf("    Queue 95%%: action=%llu (2=drop)\n", (unsigned long long)action);

    printf("\n[7] Little's Law Verification\n");
    double lambda = 1000.0;  /* 1000 packets/sec */
    double W = 0.005;        /* 5 ms wait time */
    double L = littles_law_queue_length(lambda, W);
    printf("    Arrival: %.0f pkt/s, Wait: %.3f s\n", lambda, W);
    printf("    Avg queue length (L = lambda * W): %.1f packets\n", L);
    printf("    Verify: W = L / lambda = %.3f s\n",
           littles_law_wait_time(L, lambda));

    printf("\n=== Demo Complete ===\n");
    return 0;
}
