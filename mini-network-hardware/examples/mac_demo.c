#include "mac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    printf("=== mini-network-hardware: MAC Layer Demo ===\n\n");

    uint8_t mac_bytes[6];
    const char *test_str = "aa:bb:cc:dd:ee:ff";

    printf("[1] MAC Address Parsing\n");
    printf("    Input string:  %s\n", test_str);
    if (mac_addr_parse(test_str, mac_bytes) == 0) {
        char buf[32];
        mac_addr_to_str(mac_bytes, buf);
        printf("    Parsed (round-trip): %s\n", buf);
    } else {
        printf("    ERROR: Failed to parse MAC address\n");
        return 1;
    }

    uint8_t dst_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t src_mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

    char dst_str[32], src_str[32];
    mac_addr_to_str(dst_mac, dst_str);
    mac_addr_to_str(src_mac, src_str);

    printf("\n[2] Ethernet Frame Construction\n");
    printf("    Dst MAC:       %s\n", dst_str);
    printf("    Src MAC:       %s\n", src_str);
    printf("    EtherType:     0x0800 (IPv4)\n");

    const char *payload_text = "Hello, Ethernet! This is a test payload.";
    int payload_len = (int)strlen(payload_text);

    MACFrame frame;
    mac_frame_build(&frame, dst_mac, src_mac, 0x0800,
                    (const uint8_t *)payload_text, payload_len);

    printf("    Payload:       \"%s\"\n", payload_text);
    printf("    CRC32 (FCS):   0x%08X\n", frame.fcs);

    printf("\n[3] CRC32 Verification\n");
    uint32_t test_data[] = {0xDEADBEEF};
    uint32_t crc = mac_crc32((const uint8_t *)test_data, 4);
    printf("    CRC32 of 0xDEADBEEF: 0x%08X\n", crc);

    bool valid = mac_frame_check(&frame);
    printf("    Frame FCS check: %s\n", valid ? "PASS" : "FAIL");

    printf("\n[4] Frame Details\n");
    mac_print_frame(&frame);

    printf("\n[5] MAC Statistics\n");
    MACStats stats;
    mac_stats_init(&stats);
    for (int i = 0; i < 100; i++) mac_stats_record_tx(&stats);
    for (int i = 0; i < 95; i++) mac_stats_record_rx(&stats);
    for (int i = 0; i < 3; i++) mac_stats_record_collision(&stats);
    mac_stats_record_error(&stats);
    mac_stats_print(&stats);

    printf("\n[6] Edge Case: Empty Payload Frame\n");
    MACFrame empty_frame;
    mac_frame_build(&empty_frame, dst_mac, src_mac, 0x0806, NULL, 0);
    mac_print_frame(&empty_frame);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
