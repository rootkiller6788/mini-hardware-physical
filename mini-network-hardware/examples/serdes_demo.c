#include "serdes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    printf("=== mini-network-hardware: SerDes & Line Coding Demo ===\n\n");

    printf("[1] Line Coding Types\n");
    for (int i = 0; i <= 4; i++) {
        printf("    %d: %s\n", i, serdes_line_code_name((LineCoding)i));
    }

    printf("\n[2] SerDes Configuration\n");
    SerDesConfig *cfg = serdes_config_create(LINE_CODE_64B66B, 4, 100.0);
    if (cfg) {
        printf("    100G with 4 lanes of 64b/66b:\n");
        printf("    Baud rate per lane: %.2f Gbaud\n", cfg->baud_rate_gbaud);
        printf("    Encoding overhead:  %.1f%%\n",
               serdes_encoding_overhead(cfg) * 100.0);
        serdes_config_destroy(cfg);
    }

    printf("\n[3] 8b/10b Encoding/Decoding\n");
    serdes_8b10b_init_tables();
    RunningDisparity rd = RD_NEGATIVE;
    uint8_t test_data[] = {0x00, 0x55, 0xAA, 0xFF, 0xBC};
    for (int i = 0; i < 5; i++) {
        uint16_t sym;
        if (serdes_8b10b_encode(test_data[i], false, &rd, &sym) == 0) {
            uint8_t dec;
            bool is_ctrl;
            if (serdes_8b10b_decode(sym, &rd, &dec, &is_ctrl) == 0) {
                printf("    0x%02X -> 0x%03X -> 0x%02X %s RD=%+d\n",
                       test_data[i], sym, dec,
                       (dec == test_data[i]) ? "OK" : "FAIL",
                       (int)rd);
            }
        }
    }

    printf("\n[4] 64b/66b Encoding\n");
    uint64_t data = 0xDEADBEEFCAFEBABEULL;
    uint64_t encoded, decoded;
    SyncHeaderType hdr = SYNC_DATA, rx_hdr;
    serdes_64b66b_encode(data, hdr, &encoded);
    serdes_64b66b_decode(encoded, &decoded, &rx_hdr);
    printf("    Data:    0x%016llX\n", (unsigned long long)data);
    printf("    Encoded: 0x%016llX\n", (unsigned long long)encoded);
    printf("    Decoded: 0x%016llX %s\n",
           (unsigned long long)decoded,
           (decoded == data && rx_hdr == hdr) ? "OK" : "FAIL");

    printf("\n[5] PRBS Scrambler\n");
    Scrambler scr;
    scrambler_init(&scr, (1ULL<<7)|(1ULL<<6)|1, 7, 0x5A);
    printf("    PRBS7 (x^7+x^6+1) first 16 bits: ");
    for (int i = 0; i < 16; i++) {
        printf("%u", scrambler_next_bit(&scr));
    }
    printf("\n");

    uint8_t data_buf[] = "Hello, SerDes!";
    int buf_len = (int)strlen((char*)data_buf);
    printf("    Original:  %s\n", data_buf);
    scrambler_reset(&scr);
    scrambler_process(&scr, data_buf, buf_len);
    printf("    Scrambled: ");
    for (int i = 0; i < buf_len; i++) printf("%02X ", data_buf[i]);
    printf("\n");
    scrambler_reset(&scr);
    scrambler_process(&scr, data_buf, buf_len);
    printf("    Restored:  %s\n", data_buf);

    printf("\n[6] PAM4 Modulation\n");
    PAM4Constellation pam;
    pam4_constellation_init(&pam);
    pam4_print_constellation(&pam);
    for (int b = 0; b < 4; b++) {
        int8_t sym;
        int ns;
        pam4_encode_bits((uint8_t)b, &sym, &ns);
        uint8_t dec = pam4_decode_to_bits(&sym);
        printf("    Bits=%02u -> Symbol=%+2d -> Bits=%02u %s\n",
               b, sym, dec, (dec == (uint8_t)b) ? "OK" : "FAIL");
    }

    printf("\n[7] Shannon-Hartley Analysis\n");
    SignalIntegrity si;
    signal_integrity_analyze(25e9, 20.0, 4, &si);
    printf("    Channel: 25 GHz BW, 20 dB SNR, PAM4\n");
    printf("    Shannon capacity: %.2f Gbps\n",
           si.shannon_capacity_bps / 1e9);
    printf("    Nyquist max rate: %.2f Gbps\n",
           nyquist_bitrate(si.nyquist_bandwidth_hz, 4) / 1e9);
    printf("    Spectral efficiency: %.2f bps/Hz\n",
           si.spectral_efficiency);
    printf("    Required SNR: %.1f dB\n", si.required_snr_db);

    printf("\n[8] SerDes Statistics\n");
    SerDesStats stats;
    serdes_stats_init(&stats);
    serdes_stats_update_ber(&stats, 1000000000ULL, 100);
    serdes_stats_print(&stats);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
