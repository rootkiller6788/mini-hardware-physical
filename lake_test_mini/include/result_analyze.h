#ifndef RESULT_ANALYZE_H
#define RESULT_ANALYZE_H

/**
 * result_analyze.h — Statistical Analysis and Reporting for Hardware Benchmarks
 *
 * L6: Canonical Problem — Building a comprehensive hardware performance
 *     report from raw benchmark data. This is the "test results interpreter"
 *     that transforms raw measurements into actionable insights for system
 *     architects and data engineers.
 *
 * L7: Application — Feed benchmark results into data-engine(7) to inform
 *     query planner decisions (which scan strategy to use, optimal parallelism,
 *     buffer pool sizing based on cache hierarchy).
 *
 * L5: Statistical Methods — Outlier detection (IQR and Z-score methods),
 *     confidence intervals (Student's t-distribution), ANOVA for comparing
 *     test configurations, and Cohen's d for effect size.
 *
 * Universities: CMU 15-445 (experiment design), Stanford CS229 (statistics)
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lake_test_core.h"
#include "cache_bench.h"
#include "mem_bandwidth.h"
#include "io_profile.h"
#include "perf_model.h"

/* ============================================================================
 * L1: Result Analysis Types
 * ============================================================================ */

/** Statistical confidence level */
typedef enum {
    CONFIDENCE_90 = 0,
    CONFIDENCE_95 = 1,
    CONFIDENCE_99 = 2,
    CONFIDENCE_999 = 3
} ConfidenceLevel;

/** Outlier detection method */
typedef enum {
    OUTLIER_ZSCORE = 0,    /* |z| > threshold (default 3.0) */
    OUTLIER_IQR = 1,       /* outside Q1-1.5*IQR to Q3+1.5*IQR */
    OUTLIER_MAD = 2        /* Median Absolute Deviation method */
} OutlierMethod;

/** Descriptive statistics for a data series */
typedef struct {
    double min;
    double max;
    double mean;
    double median;
    double variance;
    double stddev;
    double skewness;
    double kurtosis;
    double p25;             /* 25th percentile */
    double p50;             /* 50th percentile (median) */
    double p75;             /* 75th percentile */
    double p90;
    double p95;
    double p99;
    double p999;
    size_t count;
    size_t outlier_count;
} DescriptiveStats;

/** Confidence interval */
typedef struct {
    double lower_bound;
    double upper_bound;
    double margin_of_error;
    double std_error;
    double t_critical;      /* Student's t value for given df and alpha */
    ConfidenceLevel level;
} ConfidenceInterval;

/** ANOVA result (one-way, comparing multiple test configs) */
typedef struct {
    double ss_between;      /* Sum of squares between groups */
    double ss_within;       /* Sum of squares within groups */
    double ss_total;        /* Total sum of squares */
    double ms_between;      /* Mean square between groups */
    double ms_within;       /* Mean square within groups */
    double f_statistic;     /* F = MS_between / MS_within */
    double p_value;
    double f_critical;      /* F critical value for alpha=0.05 */
    bool   is_significant;  /* True if p < 0.05 */
    uint32_t df_between;    /* Degrees of freedom between groups */
    uint32_t df_within;     /* Degrees of freedom within groups */
    double eta_squared;     /* Effect size: SS_between / SS_total */
} AnovaResult;

/** Cohen's d effect size between two groups */
typedef struct {
    double d_value;
    double pooled_stddev;
    char   interpretation[32];  /* negligible/small/medium/large */
} CohensD;

/** Complete benchmark analysis report */
typedef struct {
    char              report_title[128];
    DescriptiveStats  throughput_stats;
    DescriptiveStats  latency_stats;
    ConfidenceInterval throughput_ci;
    ConfidenceInterval latency_ci;
    AnovaResult       *anova_results;
    size_t            num_anova;
    CohensD           *effect_sizes;
    size_t            num_effect_sizes;
    /* System characterization */
    double            detected_cache_line_bytes;
    uint64_t          detected_l1_size;
    uint64_t          detected_l2_size;
    uint64_t          detected_l3_size;
    double            peak_bandwidth_mbps;
    double            peak_iops;
    /* Recommendations */
    uint32_t          recommended_thread_count;
    uint64_t          recommended_buffer_pool_mb;
    char              recommendation_text[1024];
} AnalysisReport;

/* ============================================================================
 * L5: Time Series Decomposition for Latency Data
 *
 * Separates latency measurements into trend, seasonal, and residual
 * components using classical additive decomposition:
 *   Y(t) = T(t) + S(t) + R(t)
 * Useful for detecting periodic interference (GC pauses, OS jitter).
 * ============================================================================ */

/** Time series decomposition result */
typedef struct {
    double *trend;
    double *seasonal;
    double *residual;
    size_t  length;
    double  trend_strength;    /* Var(trend) / Var(detrended) */
    double  seasonality_strength;
    double  noise_ratio;       /* Var(residual) / Var(original) */
} TimeSeriesDecomp;

/* ============================================================================
 * L1: API Declarations
 * ============================================================================ */

/* --- Descriptive Statistics --- */

/** Compute descriptive statistics from an array of values */
DescriptiveStats stats_compute(const double *data, size_t count);

/** Compute descriptive statistics with outlier removal */
DescriptiveStats stats_compute_clean(const double *data, size_t count,
                                      OutlierMethod method, double threshold);

/** Print descriptive statistics */
void stats_print(const DescriptiveStats *stats, const char *label);

/** Merge two sets of descriptive statistics (combining independent runs) */
DescriptiveStats stats_merge(const DescriptiveStats *a, const DescriptiveStats *b);

/* --- Confidence Intervals --- */

/** Compute confidence interval using Student's t-distribution */
ConfidenceInterval stats_confidence_interval(const double *data, size_t count,
                                              ConfidenceLevel level);

/** Print confidence interval */
void ci_print(const ConfidenceInterval *ci, const char *label);

/* --- Outlier Detection --- */

/** Detect and return indices of outliers using Z-score method */
size_t *stats_detect_outliers_zscore(const double *data, size_t count,
                                      double threshold, size_t *outlier_count);

/** Detect and return indices of outliers using IQR method */
size_t *stats_detect_outliers_iqr(const double *data, size_t count,
                                   size_t *outlier_count);

/** Remove outliers and return cleaned data array */
double *stats_remove_outliers(const double *data, size_t count,
                               OutlierMethod method, double threshold,
                               size_t *cleaned_count);

/* --- ANOVA --- */

/** Perform one-way ANOVA comparing multiple groups */
AnovaResult stats_anova(const double **groups, const size_t *group_sizes,
                        size_t num_groups);

/** Print ANOVA results */
void anova_print(const AnovaResult *result);

/** Free ANOVA result */
void anova_destroy(AnovaResult *result);

/* --- Effect Size --- */

/** Compute Cohen's d between two groups */
CohensD stats_cohens_d(const double *group1, size_t n1,
                        const double *group2, size_t n2);

/** Print Cohen's d interpretation */
void cohens_d_print(const CohensD *d);

/* --- Time Series Decomposition --- */

/** Perform classical additive decomposition on latency time series */
TimeSeriesDecomp stats_time_series_decompose(const double *series, size_t length,
                                              size_t period);

/** Print time series decomposition summary */
void ts_decomp_print(const TimeSeriesDecomp *decomp);

/** Free time series decomposition */
void ts_decomp_destroy(TimeSeriesDecomp *decomp);

/* --- Cross-Module Integration --- */

/** Extract benchmark results into descriptive statistics for throughput */
DescriptiveStats analyze_extract_throughput(const BenchResult *results, size_t count);

/** Extract benchmark results into descriptive statistics for latency */
DescriptiveStats analyze_extract_latency(const BenchResult *results, size_t count);

/** Generate a complete analysis report from a TestSuite */
AnalysisReport *analyze_generate_report(const TestSuite *suite);

/** Print the full analysis report */
void analyze_report_print(const AnalysisReport *report);

/** Free an analysis report */
void analyze_report_destroy(AnalysisReport *report);

/**
 * L7: Generate hardware recommendations for a data lake query engine.
 *     Takes benchmark results and produces sizing guidance for buffer pool,
 *     parallelism degree, and I/O configuration.
 */
void analyze_recommend_lake_config(const BenchResult *results, size_t count,
                                    double target_qps, char *recommendation,
                                    size_t rec_buf_size);

/**
 * L4: Apply the Central Limit Theorem — verify that sample means are 
 *     approximately normally distributed (for n >= 30).
 *     Returns the normalized skewness of the sampling distribution.
 */
double analyze_clt_verify(const double *data, size_t count, size_t sample_size,
                           size_t num_samples, double *sampling_means_out);

/** Compare two test configurations using Welch's t-test */
typedef struct {
    double t_statistic;
    double p_value;
    double degrees_of_freedom;
    bool   significant;    /* True if p < 0.05 */
    double mean_difference;
} WelchTTest;

WelchTTest analyze_welch_ttest(const double *group1, size_t n1,
                                const double *group2, size_t n2);

void welch_ttest_print(const WelchTTest *test);

#endif /* RESULT_ANALYZE_H */