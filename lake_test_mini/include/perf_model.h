#ifndef PERF_MODEL_H
#define PERF_MODEL_H

/**
 * perf_model.h — Analytical Performance Modeling for Hardware Testing
 *
 * L4: Standards/Theorems — Implements the Roofline Model (Williams et al.,
 *     CACM 2009), Amdahl's Law (1967), Gustafson's Law (1988), and
 *     the Universal Scalability Law (Gunther, 1993). These provide
 *     theoretical bounds that hardware benchmarks must validate against.
 *
 * L8: Advanced — ML-based performance prediction using linear regression
 *     on hardware counter data. Fits a model that predicts query latency
 *     from cache miss rates, IPC, and memory bandwidth utilization.
 *
 * L9: Industry — Discusses cloud instance benchmarking methodology
 *     (PerfKitBenchmarker, SPEC Cloud) and the challenges of
 *     multi-tenant performance isolation.
 *
 * Universities: Stanford CS149, CMU 15-418, UC Berkeley CS267
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lake_test_core.h"

/* ============================================================================
 * L4: Roofline Model (Williams, Waterman, Patterson — CACM 2009)
 *
 * Models performance as a function of arithmetic intensity:
 *   Attainable GFLOP/s = min(Peak GFLOP/s, Peak GB/s * AI)
 * where AI = FLOPs / Bytes transferred.
 * ============================================================================ */

/** Roofline model parameters */
typedef struct {
    double peak_gflops;        /* Theoretical peak compute */
    double peak_bandwidth_gbps; /* Theoretical peak memory bandwidth */
    double l1_bandwidth_gbps;
    double l2_bandwidth_gbps;
    double l3_bandwidth_gbps;
    double dram_bandwidth_gbps;
} RooflineParams;

/** A single point on the roofline chart */
typedef struct {
    double arithmetic_intensity;  /* FLOP / Byte */
    double achieved_gflops;
    double bandwidth_used_gbps;
    bool   is_compute_bound;
    bool   is_memory_bound;
    double utilization;           /* fraction of peak attainable */
} RooflinePoint;

/** Complete roofline analysis */
typedef struct {
    RooflineParams  machine;
    RooflinePoint  *points;
    size_t          num_points;
    double          ridge_point;      /* AI at which compute-bound meets BW-bound */
    double          avg_utilization;
    double          max_utilization;
} RooflineAnalysis;

/* ============================================================================
 * L4: Scalability Models
 * ============================================================================ */

/** Amdahl's Law parameters */
typedef struct {
    double serial_fraction;     /* (1-P) — inherently sequential portion */
    double parallel_fraction;   /* P — parallelizable portion */
    uint32_t num_processors;
    double predicted_speedup;
    double efficiency;          /* speedup / N */
} AmdahlModel;

/** Gustafson's Law parameters (scaled speedup) */
typedef struct {
    double serial_fraction;
    double parallel_fraction;
    uint32_t num_processors;
    double scaled_speedup;      /* N + (1-N)*s where s = serial fraction */
    double scaled_efficiency;
} GustafsonModel;

/** Universal Scalability Law parameters (Gunther, 1993) */
typedef struct {
    double sigma;               /* Contention coefficient (0 = no contention) */
    double kappa;               /* Coherency delay coefficient (0 = no penalty) */
    uint32_t num_processors;
    double predicted_throughput;
    double peak_throughput;
    uint32_t peak_processors;   /* N* where contention overwhelms scaling */
} UslModel;

/* ============================================================================
 * L3: Performance Predictor Using Hardware Counter Data
 *
 * Linear model: Latency = b0 + b1*IPC + b2*L2_miss_rate + b3*BW_util
 * Uses ordinary least squares (OLS) to fit coefficients from measured data.
 * Reference: Lee & Brooks, "Accurate and Efficient Regression Modeling
 * for Microarchitectural Performance Prediction" (ASPLOS 2006).
 * ============================================================================ */

/** Performance counter sample for regression */
typedef struct {
    double ipc;
    double l1_miss_rate;
    double l2_miss_rate;
    double l3_miss_rate;
    double branch_miss_rate;
    double tlb_miss_rate;
    double bw_utilization;
    double measured_latency_ms;    /* Response variable */
} PerfSample;

/** Regression-based performance predictor */
typedef struct {
    double coefficients[8];     /* b0 (intercept) + 7 feature coefficients */
    double r_squared;           /* Model fit quality */
    double adjusted_r_squared;
    double rmse;                /* Root mean squared error */
    double f_statistic;
    double p_value;
    uint32_t num_samples;
    uint32_t num_features;
    char    feature_names[8][32];
} PerfPredictor;

/* ============================================================================
 * L6: Hardware Saturation Curve Fitting
 *
 * Models "diminishing returns" as hardware resources are scaled:
 * - Latency vs throughput trade-off (M/D/1 queueing model)
 * - Memory bandwidth vs working set size (cache saturation)
 * - IOPS vs queue depth (NVMe saturation)
 * Uses least-squares fitting to exponential and logistic curves.
 * ============================================================================ */

/** Saturation curve type */
typedef enum {
    SAT_EXPONENTIAL = 0,    /* y = a * (1 - exp(-b*x)) */
    SAT_LOGISTIC = 1,       /* y = L / (1 + exp(-k*(x-x0))) */
    SAT_POWER = 2,          /* y = a * x^b */
    SAT_MICHAELIS = 3       /* y = Vmax * x / (Km + x) */
} SaturationCurveType;

/** Fitted saturation curve parameters */
typedef struct {
    SaturationCurveType type;
    double param_a;     /* Scale / Asymptote */
    double param_b;     /* Rate / Shape */
    double param_c;     /* Additional (e.g., x-offset for logistic) */
    double r_squared;
    double saturation_point; /* x at which y reaches 95% of asymptote */
} FittedCurve;

/* ============================================================================
 * L1: API Declarations
 * ============================================================================ */

/* --- Roofline Model --- */

/** Initialize roofline parameters from platform info */
void roofline_params_from_platform(RooflineParams *params, const PlatformInfo *info);

/** Compute the roofline ridge point (arithmetic intensity at boundary) */
double roofline_ridge(const RooflineParams *params);

/** Add a data point to the roofline analysis */
bool roofline_add_point(RooflineAnalysis *analysis, double ai, double gflops,
                        double bw_used);

/** Evaluate whether a point is compute-bound or memory-bound */
bool roofline_is_compute_bound(const RooflineParams *params, double ai,
                               double achieved_gflops);

/** Print roofline analysis */
void roofline_print(const RooflineAnalysis *analysis);

/** Free roofline analysis */
void roofline_destroy(RooflineAnalysis *analysis);

/* --- Scalability Models --- */

/** Compute Amdahl's Law speedup */
double amdahl_speedup(double parallel_fraction, uint32_t num_processors);

/** Compute Amdahl's Law efficiency */
double amdahl_efficiency(double parallel_fraction, uint32_t num_processors);

/** Compute Amdahl model from measurements */
AmdahlModel amdahl_from_measurements(double single_thread_time,
                                      double multi_thread_time,
                                      uint32_t num_processors);

/** Compute Gustafson's Law scaled speedup */
double gustafson_speedup(double serial_fraction, uint32_t num_processors);

/** Compute Universal Scalability Law throughput */
double usl_throughput(double sigma, double kappa, uint32_t num_processors);

/** Find optimal processor count for USL (where dC/dN = 0) */
uint32_t usl_optimal_processors(double sigma, double kappa);

/** Fit USL parameters from measured data points */
bool usl_fit_from_data(const double *throughputs, const uint32_t *processor_counts,
                       size_t num_points, double *sigma_out, double *kappa_out);

/* --- Performance Predictor --- */

/** Initialize a performance predictor */
void perf_predictor_init(PerfPredictor *predictor);

/** Train the predictor using OLS regression on sample data */
bool perf_predictor_train(PerfPredictor *predictor, const PerfSample *samples,
                          size_t num_samples);

/** Predict latency for a given set of counter values */
double perf_predictor_predict(const PerfPredictor *predictor, const PerfSample *sample);

/** Print predictor summary (coefficients, R², RMSE) */
void perf_predictor_print(const PerfPredictor *predictor);

/** Convert a BenchResult to a PerfSample for the predictor */
void perf_sample_from_result(PerfSample *sample, const BenchResult *result);

/* --- Saturation Curve Fitting --- */

/** Fit a saturation curve to (x,y) data points using least squares */
FittedCurve saturation_curve_fit(const double *x, const double *y, size_t n,
                                  SaturationCurveType type);

/** Evaluate a fitted curve at point x */
double saturation_curve_eval(const FittedCurve *curve, double x);

/** Print fitted curve parameters */
void saturation_curve_print(const FittedCurve *curve);

/**
 * L4: Queueing Theory — M/D/1 model for I/O latency.
 *     E[W] = (lambda * E[S^2]) / (2 * (1 - rho))
 *     where lambda = arrival rate, S = service time, rho = utilization.
 *     For deterministic service time: E[S^2] = S^2.
 */
double queueing_md1_latency(double arrival_rate, double service_time);

/**
 * L4: Queueing Theory — M/M/1 model for CPU scheduling.
 *     E[N] = rho / (1 - rho), E[T] = 1 / (mu - lambda).
 */
double queueing_mm1_response_time(double arrival_rate, double service_rate);

#endif /* PERF_MODEL_H */