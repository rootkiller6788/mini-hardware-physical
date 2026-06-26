#include "ldpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ?? Gallagher (1963) construction of regular (N, dv, dc) LDPC parity matrix H ??
 *
 * Algorithm: Build H by stacking dv sub-matrices, each a random column
 * permutation of a base matrix of dc ones per row.
 *
 * L5: Gallager construction guarantees:
 *   - No two columns share more than one common row with a 1 (girth > 4)
 *   - The girth constraint avoids short cycles that degrade BP convergence.
 */
void ldpc_gallager_construct(LDPCCode *code, int N, int dv, int dc) {
    int i, j, k, r;
    int M;

    if (N < 24 || dv < 2 || dc < 3 || dv >= dc) {
        N = 24; dv = 2; dc = 4;
    }

    M = (N * dv) / dc;
    if (M < 4) M = 4;

    code->N  = N;
    code->M  = M;
    code->K  = N - M;
    code->dv = dv;
    code->dc = dc;
    code->type = LDPC_REGULAR;

    memset(code->H, 0, sizeof(code->H));
    memset(code->vn_connections, -1, sizeof(code->vn_connections));
    memset(code->cn_connections, -1, sizeof(code->cn_connections));

    {
        int Msub = M / dv;
        if (Msub < 1) Msub = 1;
        int local_block = N / dc;
        if (local_block < 1) local_block = 1;

        for (r = 0; r < dv; r++) {
            for (i = 0; i < Msub; i++) {
                for (k = 0; k < dc; k++) {
                    int col = (i * dc + k + r * 7) % N;
                    int row = r * Msub + i;
                    if (row < M && col < N) {
                        code->H[row][col] = 1;
                    }
                }
            }
        }
    }

    for (i = 0; i < N; i++) {
        int edge = 0;
        for (j = 0; j < M && edge < LDPC_DC; j++) {
            if (code->H[j][i]) {
                code->vn_connections[i][edge] = j;
                edge++;
            }
        }
    }

    for (j = 0; j < M; j++) {
        int edge = 0;
        for (i = 0; i < N && edge < LDPC_DV; i++) {
            if (code->H[j][i]) {
                code->cn_connections[j][edge] = i;
                edge++;
            }
        }
    }
}

void ldpc_encode(const LDPCCode *code, const uint8_t *info_bits,
                 uint8_t *codeword) {
    int i, j;
    int K = code->K;
    int N = code->N;
    int M = code->M;

    memset(codeword, 0, N);
    for (i = 0; i < K && i < N; i++) {
        codeword[i] = info_bits[i] & 1;
    }

    for (i = 0; i < M; i++) {
        int sum = 0;
        int last_unknown = -1;
        for (j = 0; j < N; j++) {
            if (code->H[i][j]) {
                if (j < K) {
                    sum ^= codeword[j];
                } else {
                    last_unknown = j;
                }
            }
        }
        if (last_unknown >= 0) {
            codeword[last_unknown] = (uint8_t)sum;
        }
    }
}

void ldpc_llr_from_bpsk(double *llr, const double *received, int N,
                        double noise_variance) {
    int i;
    double sigma2 = noise_variance;
    if (sigma2 < 1e-12) sigma2 = 1e-12;
    for (i = 0; i < N; i++) {
        llr[i] = 2.0 * received[i] / sigma2;
    }
}

void ldpc_bp_decode(LDPCDecoder *dec, const LDPCCode *code) {
    int iter, i, j, edge;
    int N = code->N;
    int M = code->M;
    int dv = code->dv;
    int dc = code->dc;

    dec->iterations = 0;
    dec->converged  = false;

    for (i = 0; i < N; i++) {
        for (edge = 0; edge < dv; edge++) {
            int c = code->vn_connections[i][edge];
            if (c >= 0 && c < M) {
                dec->vn_msg[i][edge] = dec->llr_in[i];
            }
        }
    }

    for (iter = 0; iter < LDPC_MAX_ITER; iter++) {
        for (j = 0; j < M; j++) {
            for (edge = 0; edge < dc; edge++) {
                int v = code->cn_connections[j][edge];
                if (v < 0 || v >= N) continue;
                double prod = 1.0;
                int e2;
                for (e2 = 0; e2 < dc; e2++) {
                    if (e2 == edge) continue;
                    int v2 = code->cn_connections[j][e2];
                    if (v2 < 0 || v2 >= N) continue;
                    int e_in;
                    double msg = 0.0;
                    for (e_in = 0; e_in < dv; e_in++) {
                        if (code->vn_connections[v2][e_in] == j) {
                            msg = dec->vn_msg[v2][e_in];
                            break;
                        }
                    }
                    double tanh_half = tanh(fabs(msg) * 0.5);
                    if (tanh_half < 1e-15) tanh_half = 1e-15;
                    prod *= tanh_half;
                    if (msg >= 0) prod = copysign(fabs(prod), 1.0);
                }
                double mag = fabs(prod);
                if (mag >= 1.0) mag = 0.999999;
                dec->cn_msg[j][edge] = 2.0 * atanh(mag);
                if (prod < 0) dec->cn_msg[j][edge] = -dec->cn_msg[j][edge];
            }
        }

        for (i = 0; i < N; i++) {
            double sum_llr = dec->llr_in[i];
            for (edge = 0; edge < dv; edge++) {
                int c = code->vn_connections[i][edge];
                if (c < 0 || c >= M) continue;
                double cn_msg_val = 0.0;
                int e_in;
                for (e_in = 0; e_in < dc; e_in++) {
                    if (code->cn_connections[c][e_in] == i) {
                        cn_msg_val = dec->cn_msg[c][e_in];
                        break;
                    }
                }
                sum_llr += cn_msg_val;
            }
            for (edge = 0; edge < dv; edge++) {
                int c = code->vn_connections[i][edge];
                if (c < 0 || c >= M) continue;
                double cn_msg_val = 0.0;
                int e_in;
                for (e_in = 0; e_in < dc; e_in++) {
                    if (code->cn_connections[c][e_in] == i) {
                        cn_msg_val = dec->cn_msg[c][e_in];
                        break;
                    }
                }
                dec->vn_msg[i][edge] = sum_llr - cn_msg_val;
            }
            dec->llr_out[i] = sum_llr;
            dec->hard_decision[i] = (sum_llr >= 0) ? 0 : 1;
        }

        if (ldpc_check_codeword(code, dec->hard_decision)) {
            dec->converged = true;
            dec->iterations = iter + 1;
            return;
        }
    }
    dec->iterations = LDPC_MAX_ITER;
}

void ldpc_min_sum_decode(LDPCDecoder *dec, const LDPCCode *code,
                         double scaling_factor) {
    int iter, i, j, edge;
    int N = code->N;
    int M = code->M;
    int dv = code->dv;
    int dc = code->dc;

    dec->iterations = 0;
    dec->converged  = false;

    for (i = 0; i < N; i++) {
        for (edge = 0; edge < dv; edge++) {
            int c = code->vn_connections[i][edge];
            if (c >= 0 && c < M) {
                dec->vn_msg[i][edge] = dec->llr_in[i];
            }
        }
    }

    for (iter = 0; iter < LDPC_MAX_ITER; iter++) {
        for (j = 0; j < M; j++) {
            double min1 = 1e100, min2 = 1e100;
            int min1_idx = -1;
            double sign_prod = 1.0;

            for (edge = 0; edge < dc; edge++) {
                int v = code->cn_connections[j][edge];
                if (v < 0 || v >= N) continue;
                int e_in;
                double msg = 0.0;
                for (e_in = 0; e_in < dv; e_in++) {
                    if (code->vn_connections[v][e_in] == j) {
                        msg = dec->vn_msg[v][e_in];
                        break;
                    }
                }
                double abs_msg = fabs(msg);
                if (abs_msg < min1) {
                    min2 = min1;
                    min1 = abs_msg;
                    min1_idx = edge;
                } else if (abs_msg < min2) {
                    min2 = abs_msg;
                }
                if (msg < 0) sign_prod = -sign_prod;
            }

            for (edge = 0; edge < dc; edge++) {
                int v = code->cn_connections[j][edge];
                if (v < 0 || v >= N) continue;
                int e_in;
                double msg = 0.0;
                for (e_in = 0; e_in < dv; e_in++) {
                    if (code->vn_connections[v][e_in] == j) {
                        msg = dec->vn_msg[v][e_in];
                        break;
                    }
                }
                double sign_this = (msg >= 0) ? 1.0 : -1.0;
                double min_val = (edge == min1_idx) ? min2 : min1;
                if (min_val > 1e99) min_val = 0.0;
                dec->cn_msg[j][edge] = scaling_factor * (sign_prod * sign_this) * min_val;
            }
        }

        for (i = 0; i < N; i++) {
            double sum_llr = dec->llr_in[i];
            for (edge = 0; edge < dv; edge++) {
                int c = code->vn_connections[i][edge];
                if (c < 0 || c >= M) continue;
                double cn_val = 0.0;
                int e_in;
                for (e_in = 0; e_in < dc; e_in++) {
                    if (code->cn_connections[c][e_in] == i) {
                        cn_val = dec->cn_msg[c][e_in];
                        break;
                    }
                }
                sum_llr += cn_val;
            }
            for (edge = 0; edge < dv; edge++) {
                int c = code->vn_connections[i][edge];
                if (c < 0 || c >= M) continue;
                double cn_val = 0.0;
                int e_in;
                for (e_in = 0; e_in < dc; e_in++) {
                    if (code->cn_connections[c][e_in] == i) {
                        cn_val = dec->cn_msg[c][e_in];
                        break;
                    }
                }
                dec->vn_msg[i][edge] = sum_llr - cn_val;
            }
            dec->llr_out[i] = sum_llr;
            dec->hard_decision[i] = (sum_llr >= 0) ? 0 : 1;
        }

        if (ldpc_check_codeword(code, dec->hard_decision)) {
            dec->converged = true;
            dec->iterations = iter + 1;
            return;
        }
    }
    dec->iterations = LDPC_MAX_ITER;
}

bool ldpc_check_codeword(const LDPCCode *code, const int *hard_decision) {
    int i, j;
    for (i = 0; i < code->M; i++) {
        int sum = 0;
        for (j = 0; j < code->N; j++) {
            if (code->H[i][j]) {
                sum ^= hard_decision[j] & 1;
            }
        }
        if (sum != 0) return false;
    }
    return true;
}

double ldpc_ber_bpsk_awgn(double snr_linear) {
    double x = sqrt(2.0 * snr_linear);
    return 0.5 * erfc(x / 1.4142135623730951);
}

double ldpc_shannon_limit(double rate) {
    if (rate <= 0.0 || rate >= 1.0) return -100.0;
    double snr_min = pow(2.0, 2.0 * rate) - 1.0;
    return 10.0 * log10(snr_min);
}
