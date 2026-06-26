#ifndef LDPC_H
#define LDPC_H

#include <stdbool.h>
#include <stdint.h>

/* LDPC (Low-Density Parity Check) codes — primary ECC in modern SSDs.
 *
 * L8: Advanced topics — iterative message-passing decoding.
 * L5: Min-Sum belief propagation algorithm.
 * L4: Shannon's channel capacity theorem: C = B·log₂(1+SNR).
 *     LDPC codes can approach capacity within ~0.0045 dB (BIAWGN).
 *
 * Reference: Gallager (1963), MacKay & Neal (1996).
 */

#define LDPC_N            96
#define LDPC_K            48
#define LDPC_M            48
#define LDPC_RATE         0.5
#define LDPC_DC           6
#define LDPC_DV           3
#define LDPC_MAX_ITER     20

typedef enum {
    LDPC_REGULAR,
    LDPC_IRREGULAR
} LDPCType;

typedef struct {
    double  llr_in[LDPC_N];
    double  llr_out[LDPC_N];
    double  vn_msg[LDPC_N][LDPC_DC];
    double  cn_msg[LDPC_M][LDPC_DV];
    int     hard_decision[LDPC_N];
    int     iterations;
    bool    converged;
} LDPCDecoder;

typedef struct {
    uint8_t  H[LDPC_M][LDPC_N];
    int      vn_connections[LDPC_N][LDPC_DC];
    int      cn_connections[LDPC_M][LDPC_DV];
    LDPCType type;
    int      N, M, K;
    int      dv, dc;
} LDPCCode;

void ldpc_gallager_construct(LDPCCode *code, int N, int dv, int dc);
void ldpc_encode(const LDPCCode *code, const uint8_t *info_bits,
                 uint8_t *codeword);
void ldpc_llr_from_bpsk(double *llr, const double *received, int N,
                        double noise_variance);
void ldpc_bp_decode(LDPCDecoder *dec, const LDPCCode *code);
void ldpc_min_sum_decode(LDPCDecoder *dec, const LDPCCode *code,
                         double scaling_factor);
bool ldpc_check_codeword(const LDPCCode *code, const int *hard_decision);
double ldpc_ber_bpsk_awgn(double snr_linear);
double ldpc_shannon_limit(double rate);

#endif
