#ifndef ENDURANCE_H
#define ENDURANCE_H

#include <stdbool.h>
#include <stdint.h>

/* SSD Endurance and Reliability Modeling.
 *
 * L4: Standards — JEDEC JESD218 (SSD endurance), JESD219 (workload).
 * L4: Weibull distribution for failure analysis.
 * L8: Bathtub curve modeling, AFR estimation, write cliff prediction.
 *
 * Key formulas:
 *   DWPD = (TBW * 1000) / (capacity_GB * 365 * warranty_years)
 *   TBW  = capacity_GB * DWPD * 365 * warranty_years / WAF
 *   AFR  = 1 - exp(-8760 / MTBF)
 */

#define ENDURANCE_MAX_HISTORY 1024

typedef enum {
    ENDURANCE_ENTERPRISE,  /* DWPD >= 10 */
    ENDURANCE_MIXED,       /* DWPD 1-3   */
    ENDURANCE_READ_INTENSIVE /* DWPD < 1  */
} EnduranceClass;

typedef struct {
    double capacity_gb;
    double dwpd;
    double tbw;
    double warranty_years;
    double waf_target;
    EnduranceClass eclass;
} EnduranceSpec;

/* Weibull distribution parameters:
 *   F(t) = 1 - exp(-(t/η)^β)
 *   β < 1: infant mortality (decreasing failure rate)
 *   β = 1: random failures (constant failure rate, exponential)
 *   β > 1: wear-out (increasing failure rate)
 */
typedef struct {
    double beta;   /* shape parameter */
    double eta;    /* scale parameter (characteristic life) */
} WeibullParams;

/* Bathtub curve: piecewise failure rate λ(t) */
typedef enum {
    BATHTUB_INFANT,    /* decreasing λ, β<1, first ~1 year */
    BATHTUB_USEFUL,    /* constant λ, β=1, main lifetime */
    BATHTUB_WEAROUT    /* increasing λ, β>1, end of life */
} BathtubPhase;

typedef struct {
    BathtubPhase phase;
    double       time_hours;
    double       failure_rate; /* failures per million hours (FPMH) */
    double       cumulative_failures;
    WeibullParams weibull;
} BathtubModel;

/* Endurance tracking per drive */
typedef struct {
    double       total_bytes_written;
    double       total_bytes_read;
    double       capacity_gb;
    double       waf;             /* write amplification factor */
    double       overprovisioning_pct;
    double       pe_cycles_used;  /* average P/E cycles consumed */
    double       pe_cycles_rated;
    double       life_used_pct;   /* percentage of rated life consumed */
    double       dwpd_actual;
    double       afr_estimate;
    uint64_t     power_on_hours;
    BathtubModel bathtub;
} EnduranceTracker;

void endurance_spec_init(EnduranceSpec *spec, double capacity_gb,
                         double dwpd, double warranty_years, double waf);
void endurance_tracker_init(EnduranceTracker *tracker,
                            const EnduranceSpec *spec, double op_pct);

/* Update tracker after N bytes written */
void endurance_record_write(EnduranceTracker *t, double bytes,
                            double waf_current);

/* Compute life used percentage from actual P/E cycles */
double endurance_life_used(double pe_actual, double pe_rated);

/* DWPD calculation from operational metrics */
double endurance_compute_dwpd(double bytes_written_per_day,
                              double capacity_gb);

/* TBW from DWPD: TBW = DWPD * capacity_GB * 365 * warranty / 1000 */
double endurance_compute_tbw(double dwpd, double capacity_gb,
                             double warranty_years);

/* Weibull CDF: F(t) = 1 - exp(-(t/eta)^beta) */
double weibull_cdf(double time_hours, double beta, double eta);

/* Weibull failure rate: λ(t) = (β/η) * (t/η)^(β-1) */
double weibull_failure_rate(double time_hours, double beta, double eta);

/* Weibull MTTF: η * Γ(1 + 1/β) */
double weibull_mttf(double beta, double eta);

/* Gamma function (Lanczos approximation) for Weibull MTTF */
double gamma_lanczos(double x);

/* AFR from MTBF: AFR = 1 - exp(-8760 / MTBF_hours) */
double endurance_afr(double mtbf_hours);

/* Write cliff detection: when d(RBER)/d(PE) exceeds threshold */
bool endurance_write_cliff_warning(const EnduranceTracker *t,
                                   double rber_current, double rber_prev);

/* Bathtub curve modeling */
void bathtub_init(BathtubModel *b, double infant_beta, double infant_end_hours,
                  double useful_mtbf, double wearout_eta, double wearout_beta);
void bathtub_evaluate(BathtubModel *b, double time_hours);
const char *bathtub_phase_name(BathtubPhase phase);

void endurance_print_report(const EnduranceTracker *t);

#endif
