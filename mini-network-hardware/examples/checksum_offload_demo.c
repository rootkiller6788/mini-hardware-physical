#include "offload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void print_hex(const char *label, const uint8_t *data, int len)
{
    printf("%s", label);
    int show = len < 64 ? len : 64;
    for (int i = 0; i < show; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n    ");
    }
    if (len > 64) printf("... (%d total bytes)", len);
    printf("\n");
}

int main(void)
{
    printf("=== mini-network-hardware: Checksum & TSO Offload Demo ===\n\n");

    OffloadEngine *csum_eng = offload_engine_create(OFFLOAD_CSUM);
    OffloadEngine *tso_eng  = offload_engine_create(OFFLOAD_TSO);

    printf("[1] IP Header Checksum Computation\n");
    printf("    ================================================\n");
    printf("    IP Header Checksum uses the one's complement of\n");
    printf("    the one's complement sum of 16-bit words.\n");
    printf("    ================================================\n\n");

    uint8_t ip_header[] = {
        0x45, 0x00,             /* Version/IHL, DSCP/ECN    */
        0x00, 0x3C,             /* Total Length = 60        */
        0x1C, 0x46,             /* Identification           */
        0x40, 0x00,             /* Flags/Fragment Offset    */
        0x40, 0x06,             /* TTL=64, Protocol=TCP     */
        0x00, 0x00,             /* Checksum field (zeroed)  */
        0xC0, 0xA8, 0x01, 0x64, /* Src IP: 192.168.1.100   */
        0xC0, 0xA8, 0x01, 0x01  /* Dst IP: 192.168.1.1     */
    };

    print_hex("    IP header (checksum=0):    ", ip_header, (int)sizeof(ip_header));

    uint16_t computed_csum = offload_csum_compute(ip_header, (int)sizeof(ip_header));
    printf("    Computed checksum:         0x%04X\n", computed_csum);

    ip_header[10] = (uint8_t)(computed_csum >> 8);
    ip_header[11] = (uint8_t)(computed_csum & 0xFF);

    print_hex("    IP header (checksum set):  ", ip_header, (int)sizeof(ip_header));

    uint16_t verify_csum = offload_csum_compute(ip_header, (int)sizeof(ip_header));
    printf("    Verify (should be 0x0000):  0x%04X\n", verify_csum);
    printf("    Verification:                %s\n",
           verify_csum == 0x0000 ? "PASS" : "FAIL");

    printf("\n[2] Checksum Verification Tests\n");
    uint8_t simple_data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint16_t simple_csum = offload_csum_compute(simple_data, 4);
    printf("    One's complement sum of 0xDEADBEEF (4 bytes):\n");
    printf("      0xDEAD + 0xBEEF = 0x19D9C\n");
    printf("      Wrap carry: 0x9D9C + 1 = 0x9D9D\n");
    printf("      Complement: ~0x9D9D = 0x%04X\n", simple_csum);

    printf("\n    Verification with offload_checksum_verify():\n");
    bool csum_ok = offload_checksum_verify(csum_eng, simple_data, 4, simple_csum);
    printf("    Result: %s\n", csum_ok ? "PASS" : "FAIL");

    printf("\n[3] TCP Segmentation Offload (TSO)\n");
    printf("    ================================================\n");
    printf("    TSO splits large TCP packets into MSS-sized\n");
    printf("    segments at the NIC hardware level, offloading\n");
    printf("    the CPU from segmentation work.\n");
    printf("    ================================================\n\n");

    int mss = 1460;
    int large_packet_len = 6000;
    uint8_t *large_packet = (uint8_t *)malloc((size_t)large_packet_len);
    if (large_packet) {
        for (int i = 0; i < large_packet_len; i++) {
            large_packet[i] = (uint8_t)(i & 0xFF);
        }
    }

    printf("    Large packet size: %d bytes\n", large_packet_len);
    printf("    MSS (Maximum Segment Size): %d bytes\n", mss);
    printf("    Expected segments: %d\n",
           (large_packet_len + mss - 1) / mss);

    uint8_t *segments = NULL;
    int n_segments = 0;

    printf("\n    Performing TSO segmentation...\n");
    if (offload_tso_segment(tso_eng, large_packet, large_packet_len,
                             mss, &segments, &n_segments) == 0) {
        printf("    Created %d segments\n", n_segments);

        for (int i = 0; i < n_segments; i++) {
            int offset = i * mss;
            int seg_len = (i == n_segments - 1)
                ? (large_packet_len - offset) : mss;
            printf("\n    Segment %d:\n", i);
            printf("      Offset: %d\n", offset);
            printf("      Length: %d bytes\n", seg_len);
            printf("      First bytes: ");
            for (int j = 0; j < 8 && j < seg_len; j++) {
                printf("%02x ", segments[offset + j]);
            }
            printf("\n      Last bytes:  ");
            int start = seg_len > 8 ? seg_len - 8 : 0;
            for (int j = start; j < seg_len; j++) {
                printf("%02x ", segments[offset + j]);
            }
            printf("\n");
        }

        int total_seg_len = n_segments * mss;
        printf("\n    Total segmented size (with padding): %d bytes\n", total_seg_len);

        free(segments);
    } else {
        printf("    ERROR: TSO segmentation failed\n");
    }

    printf("\n[4] Offload Engine Statistics\n");
    offload_print_stats(csum_eng);
    printf("\n");
    offload_print_stats(tso_eng);

    printf("\n[5] TCP Checksum Pseudo-header Example\n");
    uint8_t tcp_pseudo[] = {
        0xC0, 0xA8, 0x01, 0x64, /* Src IP    */
        0xC0, 0xA8, 0x01, 0x01, /* Dst IP    */
        0x00, 0x06,             /* Reserved, Protocol=TCP */
        0x00, 0x14              /* TCP Segment Length=20  */
    };
    uint16_t pseudo_csum = offload_csum_compute(tcp_pseudo, (int)sizeof(tcp_pseudo));
    printf("    TCP pseudo-header checksum: 0x%04X\n", pseudo_csum);

    printf("\n[6] Offload Type Names\n");
    for (int i = 0; i < OFFLOAD_COUNT; i++) {
        printf("    Type %d: %s\n", i, offload_type_name((OffloadType)i));
    }

    free(large_packet);
    offload_engine_destroy(csum_eng);
    offload_engine_destroy(tso_eng);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
