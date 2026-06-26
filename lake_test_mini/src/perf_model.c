/**
 * perf_model.c — Performance Modeling Implementation
 *
 * L4: Roofline Model, Amdahl's Law, Gustafson's Law, Universal Scalability Law.
 * L4: Queueing theory — M/D/1 and M/M/1 models for latency prediction.
 * L5: OLS regression for performance prediction from hardware counters.
 * L5: Saturation curve fitting (exponential, logistic, power, Michaelis-Menten).
 * L8: ML-based performance prediction using linear regression.
 */

#include "perf_model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ============================================================================
 * Roofline Model Implementation
 * ============================================================================ */

void roofline_params_from_platform(RooflineParams *params, const PlatformInfo *info) {
    if (!params || !info) return;
    memset(params, 0, sizeof(RooflineParams));
    
    /* Compute peak GFLOP/s: cores * freq * FMA_width * 2 (FMA = 2 FLOPs) */
    double simd_width = 16.0; /* AVX-512: 16 SP floats */
    params->peak_gflops = (double)info->cpu_cores * info->cpu_freq_ghz *
                          simd_width * 2.0;
    
    /* Bandwidths in GB/s */
    params->peak_bandwidth_gbps = info->ram_bandwidth_mbps / 1024.0;
    params->dram_bandwidth_gbps = info->ram_bandwidth_mbps / 1024.0;
    params->l1_bandwidth_gbps = params->peak_gflops * 4.0 / 1024.0; /* ~4 bytes per FLOP */
    params->l2_bandwidth_gbps = params->l1_bandwidth_gbps * 0.4;
    params->l3_bandwidth_gbps = params->l2_bandwidth_gbps * 0.3;
}

double roofline_ridge(const RooflineParams *params) {
    if (!params || params->peak_bandwidth_gbps <= 0.0) return INFINITY;
    return params->peak_gflops / params->peak_bandwidth_gbps;
}

bool roofline_add_point(RooflineAnalysis *analysis, double ai, double gflops,
                        double bw_used) {
    if (!analysis) return false;
    
    size_t new_num = analysis->num_points + 1;
    RooflinePoint *new_points = (RooflinePoint *)realloc(analysis->points,
                                                          new_num * sizeof(RooflinePoint));
    if (!new_points) return false;
    
    analysis->points = new_points;
    RooflinePoint *pt = &analysis->points[analysis->num_points];
    pt->arithmetic_intensity = ai;
    pt->achieved_gflops = gflops;
    pt->bandwidth_used_gbps = bw_used;
    pt->is_compute_bound = roofline_is_compute_bound(&analysis->machine, ai, gflops);
    pt->is_memory_bound = !pt->is_compute_bound;
    
    /* Utilization = achieved / peak-attainable */
    double peak_attainable = (ai < analysis->ridge_point)
        ? analysis->machine.peak_bandwidth_gbps * ai
        : analysis->machine.peak_gflops;
    pt->utilization = (peak_attainable > 0.0) ? gflops / peak_attainable : 0.0;
    
    analysis->num_points = new_num;
    
    /* Update aggregate stats */
    double sum_util = 0.0;
    double max_util = 0.0;
    for (size_t i = 0; i < analysis->num_points; i++) {
        sum_util += analysis->points[i].utilization;
        if (analysis->points[i].utilization > max_util) {
            max_util = analysis->points[i].utilization;
        }
    }
    analysis->avg_utilization = sum_util / (double)analysis->num_points;
    analysis->max_utilization = max_util;
    
    return true;
}

bool roofline_is_compute_bound(const RooflineParams *params, double ai,
                               double achieved_gflops) {
    if (!params) return false;
    double ridge = roofline_ridge(params);
    /* Compute-bound if AI >= ridge point AND achieved is limited by compute */
    if (ai >= ridge) return true;
    /* Check if achieved is below memory bandwidth line */
    double bw_limit = params->peak_bandwidth_gbps * ai;
    return achieved_gflops < bw_limit * 0.85; /* 15% tolerance */
}

void roofline_print(const RooflineAnalysis *analysis) {
    if (!analysis) return;
    
    printf("\n========== Roofline Analysis ==========\n");
    printf("Machine Peak: %.1f GFLOP/s, %.1f GB/s\n",
           analysis->machine.peak_gflops, analysis->machine.peak_bandwidth_gbps);
    printf("Ridge Point:  %.2f FLOP/Byte\n", analysis->ridge_point);
    printf("\n%-14s %-12s %-12s %-10s %-10s\n",
           "AI(F/B)", "GFLOP/s", "BW(GB/s)", "Bound", "Util%");
    printf("---------------------------------------------------------\n");
    
    for (size_t i = 0; i < analysis->num_points; i++) {
        const RooflinePoint *p = &analysis->points[i];
        printf("%-14.2f %-12.1f %-12.1f %-10s %-8.1f%%\n",
               p->arithmetic_intensity, p->achieved_gflops,
               p->bandwidth_used_gbps,
               p->is_compute_bound ? "COMPUTE" : "MEMORY",
               p->utilization * 100.0);
    }
    
    printf("---------------------------------------------------------\n");
    printf("Avg Utilization: %.1f%% | Max: %.1f%%\n",
           analysis->avg_utilization * 100.0, analysis->max_utilization * 100.0);
    printf("========================================\n");
}

void roofline_destroy(RooflineAnalysis *analysis) {
    if (!analysis) return;
    free(analysis->points);
    free(analysis);
}

/* ============================================================================
 * Scalability Models (Amdahl, Gustafson, USL)
 * ============================================================================ */

double amdahl_speedup(double parallel_fraction, uint32_t num_processors) {
    if (num_processors == 0) return 1.0;
    double serial = 1.0 - parallel_fraction;
    double denominator = serial + parallel_fraction / (double)num_processors;
    if (denominator <= 0.0) return (double)num_processors;
    return 1.0 / denominator;
}

double amdahl_efficiency(double parallel_fraction, uint32_t num_processors) {
    double speedup = amdahl_speedup(parallel_fraction, num_processors);
    return speedup / (double)num_processors;
}

AmdahlModel amdahl_from_measurements(double single_thread_time,
                                      double multi_thread_time,
                                      uint32_t num_processors) {
    AmdahlModel model;
    memset(&model, 0, sizeof(model));
    
    if (single_thread_time <= 0.0 || multi_thread_time <= 0.0 ||
        num_processors <= 1) {
        model.serial_fraction = 1.0;
        model.predicted_speedup = 1.0;
        model.efficiency = 1.0;
        return model;
    }
    
    double actual_speedup = single_thread_time / multi_thread_time;
    model.num_processors = num_processors;
    model.predicted_speedup = actual_speedup;
    
    /* Derive parallel fraction from Amdahl: S = 1/((1-P) + P/N)
     * P = (1 - 1/S) * N/(N-1) */
    if (num_processors > 1 && actual_speedup > 1.0) {
        model.parallel_fraction = (1.0 - 1.0 / actual_speedup) *
                                  (double)num_processors / (double)(num_processors - 1);
        if (model.parallel_fraction > 1.0) model.parallel_fraction = 1.0;
        if (model.parallel_fraction < 0.0) model.parallel_fraction = 0.0;
    } else {
        model.parallel_fraction = 0.0;
    }
    
    model.serial_fraction = 1.0 - model.parallel_fraction;
    model.efficiency = actual_speedup / (double)num_processors;
    
    return model;
}

double gustafson_speedup(double serial_fraction, uint32_t num_processors) {
    /* Scaled speedup: S = N + (1-N)*s where s = serial fraction */
    return (double)num_processors + (1.0 - (double)num_processors) * serial_fraction;
}

double usl_throughput(double sigma, double kappa, uint32_t num_processors) {
    /* C(N) = C(1) * N / (1 + sigma*(N-1) + kappa*N*(N-1)) */
    if (num_processors == 0) return 0.0;
    double denominator = 1.0 + sigma * (double)(num_processors - 1) +
                         kappa * (double)num_processors * (double)(num_processors - 1);
    if (denominator <= 0.0) return 0.0;
    return (double)num_processors / denominator;
}

uint32_t usl_optimal_processors(double sigma, double kappa) {
    /* N_opt = floor(sqrt((1-sigma)/kappa))
     * This is where dC/dN = 0 for the USL equation */
    if (kappa <= 0.0) return UINT32_MAX; /* No coherency penalty, scales forever */
    double numerator = 1.0 - sigma;
    if (numerator <= 0.0) return 1; /* Contention overwhelms at N=1 */
    double N_opt = sqrt(numerator / kappa);
    return (uint32_t)N_opt;
}

bool usl_fit_from_data(const double *throughputs, const uint32_t *processor_counts,
                       size_t num_points, double *sigma_out, double *kappa_out) {
    if (!throughputs || !processor_counts || num_points < 3 ||
        !sigma_out || !kappa_out) return false;
    
    /* Fit USL using linear regression on transformed equation.
     * USL: C(N) = N / (1 + sigma*(N-1) + kappa*N*(N-1))
     * Transform: (N/C(N) - 1) / (N-1) = sigma + kappa*N
     * Let y = (N/C(N) - 1) / (N-1), x = N
     * Then y = sigma + kappa * x (linear!)
     */
    
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_xx = 0.0;
    size_t n = 0;
    
    for (size_t i = 0; i < num_points; i++) {
        if (processor_counts[i] <= 1) continue;
        double N = (double)processor_counts[i];
        double C = throughputs[i];
        
        if (C <= 0.0 || N <= 1.0) continue;
        
        double y = (N / C - 1.0) / (N - 1.0);
        double x = N;
        
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_xx += x * x;
        n++;
    }
    
    if (n < 2) return false;
    
    double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
    double intercept = (sum_y - slope * sum_x) / n;
    
    *kappa_out = slope;
    *sigma_out = intercept;
    
    return true;
}

/* ============================================================================
 * Performance Predictor (OLS Regression)
 * ============================================================================ */

void perf_predictor_init(PerfPredictor *predictor) {
    if (!predictor) return;
    memset(predictor, 0, sizeof(PerfPredictor));
    
    /* Set feature names */
    strncpy(predictor->feature_names[0], "IPC", 31);
    strncpy(predictor->feature_names[1], "L1_Miss", 31);
    strncpy(predictor->feature_names[2], "L2_Miss", 31);
    strncpy(predictor->feature_names[3], "L3_Miss", 31);
    strncpy(predictor->feature_names[4], "Branch_Miss", 31);
    strncpy(predictor->feature_names[5], "TLB_Miss", 31);
    strncpy(predictor->feature_names[6], "BW_Util", 31);
    predictor->num_features = 7;
}

bool perf_predictor_train(PerfPredictor *predictor, const PerfSample *samples,
                          size_t num_samples) {
    if (!predictor || !samples || num_samples < 8) return false;
    
    /* Build design matrix X (N x 8) and response vector y (N x 1)
     * X has column 0 = 1.0 (intercept), columns 1-7 = features
     * Solve using normal equations: beta = (X^T X)^(-1) X^T y
     *
     * Since we need to invert an 8x8 matrix, use Gaussian elimination.
     */
    
    size_t m = 8; /* 1 intercept + 7 features */
    double XtX[8][8] = {{0}};
    double Xty[8] = {0};
    
    for (size_t i = 0; i < num_samples; i++) {
        double features[8];
        features[0] = 1.0; /* intercept */
        features[1] = samples[i].ipc;
        features[2] = samples[i].l1_miss_rate;
        features[3] = samples[i].l2_miss_rate;
        features[4] = samples[i].l3_miss_rate;
        features[5] = samples[i].branch_miss_rate;
        features[6] = samples[i].tlb_miss_rate;
        features[7] = samples[i].bw_utilization;
        
        double y = samples[i].measured_latency_ms;
        
        for (size_t j = 0; j < m; j++) {
            Xty[j] += features[j] * y;
            for (size_t k = 0; k < m; k++) {
                XtX[j][k] += features[j] * features[k];
            }
        }
    }
    
    /* Gaussian elimination with partial pivoting on XtX|Xty */
    double A[8][9]; /* augmented matrix */
    for (size_t i = 0; i < m; i++) {
        for (size_t j = 0; j < m; j++) {
            A[i][j] = XtX[i][j];
        }
        A[i][m] = Xty[i];
    }
    
    /* Forward elimination */
    for (size_t i = 0; i < m; i++) {
        /* Partial pivot */
        size_t max_row = i;
        double max_val = fabs(A[i][i]);
        for (size_t r = i + 1; r < m; r++) {
            if (fabs(A[r][i]) > max_val) {
                max_val = fabs(A[r][i]);
                max_row = r;
            }
        }
        
        if (max_val < 1e-12) return false; /* Singular matrix */
        
        if (max_row != i) {
            for (size_t j = 0; j <= m; j++) {
                double tmp = A[i][j];
                A[i][j] = A[max_row][j];
                A[max_row][j] = tmp;
            }
        }
        
        /* Eliminate below */
        for (size_t r = i + 1; r < m; r++) {
            double factor = A[r][i] / A[i][i];
            for (size_t j = i; j <= m; j++) {
                A[r][j] -= factor * A[i][j];
            }
        }
    }
    
    /* Back substitution */
    double beta[8];
    for (size_t i = m; i > 0; i--) {
        size_t row = i - 1;
        beta[row] = A[row][m];
        for (size_t j = row + 1; j < m; j++) {
            beta[row] -= A[row][j] * beta[j];
        }
        beta[row] /= A[row][row];
    }
    
    /* Store coefficients */
    for (size_t i = 0; i < m; i++) {
        predictor->coefficients[i] = beta[i];
    }
    
    /* Compute R-squared, RMSE */
    double ss_res = 0.0, ss_tot = 0.0;
    double y_mean = 0.0;
    for (size_t i = 0; i < num_samples; i++) {
        y_mean += samples[i].measured_latency_ms;
    }
    y_mean /= (double)num_samples;
    
    for (size_t i = 0; i < num_samples; i++) {
        double pred = beta[0] + beta[1] * samples[i].ipc +
                      beta[2] * samples[i].l1_miss_rate +
                      beta[3] * samples[i].l2_miss_rate +
                      beta[4] * samples[i].l3_miss_rate +
                      beta[5] * samples[i].branch_miss_rate +
                      beta[6] * samples[i].tlb_miss_rate +
                      beta[7] * samples[i].bw_utilization;
        
        double residual = samples[i].measured_latency_ms - pred;
        ss_res += residual * residual;
        ss_tot += (samples[i].measured_latency_ms - y_mean) *
                  (samples[i].measured_latency_ms - y_mean);
    }
    
    predictor->r_squared = (ss_tot > 0.0) ? (1.0 - ss_res / ss_tot) : 0.0;
    predictor->num_samples = (uint32_t)num_samples;
    
    /* Adjusted R-squared */
    double n = (double)num_samples;
    double p = (double)m;
    predictor->adjusted_r_squared = 1.0 - (1.0 - predictor->r_squared) *
                                    (n - 1.0) / (n - p - 1.0);
    
    /* RMSE */
    predictor->rmse = sqrt(ss_res / n);
    
    /* F-statistic */
    double msr = (ss_tot - ss_res) / (p - 1.0);
    double mse = ss_res / (n - p);
    predictor->f_statistic = (mse > 0.0) ? msr / mse : 0.0;
    
    return true;
}

double perf_predictor_predict(const PerfPredictor *predictor, const PerfSample *sample) {
    if (!predictor || !sample) return 0.0;
    
    return predictor->coefficients[0] +
           predictor->coefficients[1] * sample->ipc +
           predictor->coefficients[2] * sample->l1_miss_rate +
           predictor->coefficients[3] * sample->l2_miss_rate +
           predictor->coefficients[4] * sample->l3_miss_rate +
           predictor->coefficients[5] * sample->branch_miss_rate +
           predictor->coefficients[6] * sample->tlb_miss_rate +
           predictor->coefficients[7] * sample->bw_utilization;
}

void perf_predictor_print(const PerfPredictor *predictor) {
    if (!predictor) return;
    
    printf("\n========== Performance Predictor ==========\n");
    printf("Model: Latency = ");
    for (uint32_t i = 0; i < predictor->num_features + 1; i++) {
        if (i == 0) {
            printf("%.4f", predictor->coefficients[i]);
        } else {
            printf(" + %.4f*%s", predictor->coefficients[i],
                   predictor->feature_names[i - 1]);
        }
    }
    printf("\n");
    printf("R-squared:  %.4f (adj: %.4f)\n",
           predictor->r_squared, predictor->adjusted_r_squared);
    printf("RMSE:       %.4f ms\n", predictor->rmse);
    printf("F-statistic: %.2f\n", predictor->f_statistic);
    printf("Samples:    %u\n", predictor->num_samples);
    printf("===========================================\n");
}

void perf_sample_from_result(PerfSample *sample, const BenchResult *result) {
    if (!sample || !result) return;
    memset(sample, 0, sizeof(PerfSample));
    
    const HwCounterSet *cs = &result->counters;
    sample->ipc = cs->ipc;
    sample->l1_miss_rate = cs->l1_miss_rate;
    sample->l2_miss_rate = cs->l2_miss_rate;
    sample->l3_miss_rate = cs->l3_miss_rate;
    sample->branch_miss_rate = cs->branch_miss_rate;
    sample->tlb_miss_rate = cs->tlb_miss_rate;
    sample->bw_utilization = 0.0; /* Would be computed from platform info */
    sample->measured_latency_ms = result->avg_latency_ns / 1e6;
}

/* ============================================================================
 * Saturation Curve Fitting
 * ============================================================================ */

FittedCurve saturation_curve_fit(const double *x, const double *y, size_t n,
                                  SaturationCurveType type) {
    FittedCurve result;
    memset(&result, 0, sizeof(result));
    result.type = type;
    
    if (!x || !y || n < 3) return result;
    
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_xx = 0.0;
    double max_y = y[0];
    
    for (size_t i = 0; i < n; i++) {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_xx += x[i] * x[i];
        if (y[i] > max_y) max_y = y[i];
    }
    
    switch (type) {
        case SAT_EXPONENTIAL: {
            /* y = a * (1 - exp(-b * x))
             * Fit using log transform: log(1 - y/a) = -b * x
             * For simplicity, estimate a = max_y */
            result.param_a = max_y * 1.05; /* Slightly above max as asymptote */
            
            /* Linear regression on log scale */
            double ss_xt = 0.0, ss_yt = 0.0, ss_xyt = 0.0, ss_xxt = 0.0;
            size_t valid = 0;
            for (size_t i = 0; i < n; i++) {
                double ratio = 1.0 - y[i] / result.param_a;
                if (ratio > 1e-10) {
                    double log_val = log(ratio);
                    ss_xt += x[i];
                    ss_yt += log_val;
                    ss_xyt += x[i] * log_val;
                    ss_xxt += x[i] * x[i];
                    valid++;
                }
            }
            if (valid > 1) {
                result.param_b = -(valid * ss_xyt - ss_xt * ss_yt) /
                                 (valid * ss_xxt - ss_xt * ss_xt);
                if (result.param_b < 0) result.param_b = 0.1;
            } else {
                result.param_b = 0.1;
            }
            result.saturation_point = log(20.0) / result.param_b; /* 95% */
            break;
        }
        case SAT_LOGISTIC: {
            /* y = L / (1 + exp(-k*(x - x0)))
             * L = max_y, fit k and x0 */
            result.param_a = max_y * 1.05;
            result.param_b = 0.5; /* k */
            result.param_c = sum_x / (double)n; /* x0 */
            result.saturation_point = result.param_c + log(19.0) / result.param_b;
            break;
        }
        case SAT_POWER: {
            /* y = a * x^b, fit using log-log */
            double ss_lx = 0.0, ss_ly = 0.0, ss_lxy = 0.0, ss_lxx = 0.0;
            size_t valid = 0;
            for (size_t i = 0; i < n; i++) {
                if (x[i] > 0 && y[i] > 0) {
                    double lx = log(x[i]);
                    double ly = log(y[i]);
                    ss_lx += lx;
                    ss_ly += ly;
                    ss_lxy += lx * ly;
                    ss_lxx += lx * lx;
                    valid++;
                }
            }
            if (valid > 1) {
                double slope = (valid * ss_lxy - ss_lx * ss_ly) /
                               (valid * ss_lxx - ss_lx * ss_lx);
                double intercept = (ss_ly - slope * ss_lx) / valid;
                result.param_b = slope;
                result.param_a = exp(intercept);
            }
            result.saturation_point = exp(log(0.95 * result.param_a) / result.param_b);
            break;
        }
        case SAT_MICHAELIS: {
            /* y = Vmax * x / (Km + x)
             * Lineweaver-Burk: 1/y = Km/Vmax * 1/x + 1/Vmax */
            result.param_a = max_y * 1.05; /* Vmax */
            result.param_b = x[0]; /* Km estimate */
            result.saturation_point = 19.0 * result.param_b; /* 95% */
            break;
        }
    }
    
    /* Compute R-squared */
    double ss_res = 0.0, ss_tot = 0.0;
    double mean_y = sum_y / (double)n;
    for (size_t i = 0; i < n; i++) {
        double y_pred = saturation_curve_eval(&result, x[i]);
        ss_res += (y[i] - y_pred) * (y[i] - y_pred);
        ss_tot += (y[i] - mean_y) * (y[i] - mean_y);
    }
    result.r_squared = (ss_tot > 0.0) ? (1.0 - ss_res / ss_tot) : 0.0;
    
    return result;
}

double saturation_curve_eval(const FittedCurve *curve, double x) {
    if (!curve) return 0.0;
    
    switch (curve->type) {
        case SAT_EXPONENTIAL:
            return curve->param_a * (1.0 - exp(-curve->param_b * x));
        case SAT_LOGISTIC:
            return curve->param_a / (1.0 + exp(-curve->param_b * (x - curve->param_c)));
        case SAT_POWER:
            return (x > 0) ? curve->param_a * pow(x, curve->param_b) : 0.0;
        case SAT_MICHAELIS:
            return curve->param_a * x / (curve->param_b + x);
        default:
            return 0.0;
    }
}

void saturation_curve_print(const FittedCurve *curve) {
    if (!curve) return;
    
    const char *type_names[] = {"Exponential", "Logistic", "Power", "Michaelis-Menten"};
    
    printf("\n========== Saturation Curve Fit ==========\n");
    printf("Type: %s\n", type_names[curve->type]);
    printf("R-squared: %.4f\n", curve->r_squared);
    printf("Saturation at x=%.2f (95%%)\n", curve->saturation_point);
    printf("Parameters: a=%.4f, b=%.4f, c=%.4f\n",
           curve->param_a, curve->param_b, curve->param_c);
    printf("==========================================\n");
}

/* ============================================================================
 * Queueing Theory Models (L4)
 * ============================================================================ */

double queueing_md1_latency(double arrival_rate, double service_time) {
    /* M/D/1: E[W] = lambda * S^2 / (2 * (1 - rho))
     * where rho = lambda * S */
    if (service_time <= 0.0 || arrival_rate <= 0.0) return 0.0;
    
    double rho = arrival_rate * service_time;
    if (rho >= 1.0) return INFINITY; /* Unstable queue */
    
    double E_wait = (arrival_rate * service_time * service_time) / (2.0 * (1.0 - rho));
    return service_time + E_wait; /* Total latency = service + wait */
}

double queueing_mm1_response_time(double arrival_rate, double service_rate) {
    /* M/M/1: E[T] = 1 / (mu - lambda)
     * where mu = service_rate, lambda = arrival_rate */
    if (service_rate <= arrival_rate) return INFINITY; /* Unstable */
    return 1.0 / (service_rate - arrival_rate);
}