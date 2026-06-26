#ifndef SERDES_H
#define SERDES_H

#include <stdbool.h>
#include <stdint.h>

/* ================================================================
 * SerDes (Serializer/Deserializer) & Line Coding
 *
 * L1 Core Definitions: line coding types, PAM4, scrambling
 * L2 Core Concepts: NRZ, PAM4 modulation, DC balance
 * L3 Engineering Structures: 8b/10b encode/decode pipeline, 64b/66b
 * L4 Standards/Theorems: Shannon-Hartley theorem, Nyquist criterion
 * L5 Algorithms: 8b/10b D-character encoding, PRBS scrambler
 * L7 Applications: 100G/400G/800G Ethernet PHY modeling
 * L8 Advanced Topics: PAM4 for high-speed SerDes
 * ================================================================ */

/* --- Line Coding Types --- */
typedef enum {
    LINE_CODE_NRZ      = 0,
    LINE_CODE_8B10B    = 1,
    LINE_CODE_64B66B   = 2,
    LINE_CODE_128B130B = 3,
    LINE_CODE_PAM4     = 4
} LineCoding;

/* --- PAM4 Constellation --- */
#define PAM4_LEVELS 4

typedef struct {
    int8_t  symbol_map[PAM4_LEVELS];
    double  voltage_levels[PAM4_LEVELS];
    uint8_t gray_code[PAM4_LEVELS];
} PAM4Constellation;

/* --- 8b/10b Running Disparity --- */
typedef enum {
    RD_NEGATIVE = -1,
    RD_POSITIVE = +1
} RunningDisparity;

/* --- 64b/66b Sync Header --- */
typedef enum {
    SYNC_DATA = 0b01,
    SYNC_CTRL = 0b10
} SyncHeaderType;

/* --- Pre-computed 5b/6b and 3b/4b encoding tables --- */
#define ENC_5B6B_SIZE 32
#define ENC_3B4B_SIZE 8

typedef struct {
    uint8_t rd_minus;
    uint8_t rd_plus;
} Enc5b6bEntry;

typedef struct {
    uint8_t rd_minus;
    uint8_t rd_plus;
} Enc3b4bEntry;

/* --- SerDes Configuration --- */
typedef struct {
    LineCoding line_coding;
    int        lanes;
    double     bit_rate_gbps;
    double     baud_rate_gbaud;
    bool       scrambling_enabled;
    uint64_t   scrambler_poly;
} SerDesConfig;

/* --- PRBS Scrambler (LFSR-based, IEEE 802.3 Clause 49) --- */
typedef struct {
    uint64_t state;
    uint64_t polynomial;
    int      bit_width;
    int      seed;
} Scrambler;

/* --- SerDes Statistics --- */
typedef struct {
    uint64_t bits_tx;
    uint64_t bits_rx;
    uint64_t symbol_errors;
    uint64_t disparity_errors;
    uint64_t sync_losses;
    uint64_t code_violations;
    double   ber;
} SerDesStats;

/* --- Signal Integrity (Shannon-Hartley analysis) --- */
typedef struct {
    double snr_db;
    double snr_linear;
    double nyquist_bandwidth_hz;
    double shannon_capacity_bps;
    double spectral_efficiency;
    int    modulation_order;
    double required_snr_db;
} SignalIntegrity;

/* ================================================================
 * API Declarations
 * ================================================================ */

/* Configuration */
SerDesConfig *serdes_config_create(LineCoding code, int lanes, double bit_rate);
void          serdes_config_destroy(SerDesConfig *cfg);
const char   *serdes_line_code_name(LineCoding code);
double        serdes_baud_rate(const SerDesConfig *cfg);
double        serdes_encoding_overhead(const SerDesConfig *cfg);

/* 8b/10b Encoding (IEEE 802.3 Clause 36, IBM Patent expired) */
void          serdes_8b10b_init_tables(void);
int           serdes_8b10b_encode(uint8_t data_byte, bool is_control,
                                  RunningDisparity *rd, uint16_t *symbol);
int           serdes_8b10b_decode(uint16_t symbol, RunningDisparity *rd,
                                  uint8_t *decoded, bool *is_control);
bool          serdes_8b10b_is_valid_symbol(uint16_t symbol);

/* 64b/66b Encoding (IEEE 802.3 Clause 49) */
int           serdes_64b66b_encode(uint64_t data_word,
                                   SyncHeaderType sync_hdr,
                                   uint64_t *encoded);
int           serdes_64b66b_decode(uint64_t encoded,
                                   uint64_t *data_word,
                                   SyncHeaderType *sync_hdr);

/* Scrambler / Descrambler (PRBS, ITU-T O.150) */
void          scrambler_init(Scrambler *scr, uint64_t poly, int width, int seed);
uint8_t       scrambler_next_bit(Scrambler *scr);
uint64_t      scrambler_next_word(Scrambler *scr);
void          scrambler_process(Scrambler *scr, uint8_t *data, int len);
void          scrambler_reset(Scrambler *scr);

/* PAM4 Encode/Decode */
void          pam4_constellation_init(PAM4Constellation *pam);
int           pam4_encode_bits(uint8_t bits, int8_t *symbols_out, int *n_symbols);
uint8_t       pam4_decode_to_bits(const int8_t *symbols);
void          pam4_print_constellation(const PAM4Constellation *pam);

/* Signal Integrity Analysis (Shannon-Hartley Theorem) */
void          signal_integrity_analyze(double bandwidth_hz, double snr_db,
                                       int modulation_levels,
                                       SignalIntegrity *result);
double        shannon_capacity(double bandwidth_hz, double snr_linear);
double        nyquist_bitrate(double bandwidth_hz, int modulation_levels);

/* Statistics */
void          serdes_stats_init(SerDesStats *stats);
void          serdes_stats_print(const SerDesStats *stats);
void          serdes_stats_update_ber(SerDesStats *stats,
                                      uint64_t bits, uint64_t errors);

#endif /* SERDES_H */
