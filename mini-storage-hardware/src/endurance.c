#include "endurance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * SSD Endurance and Reliability Modeling.
 * L4: JEDEC JESD218/219: Enterprise SSD endurance standards.
 *     DWPD = Drive Writes Per Day. TBW = Total Bytes Written.
 * L4: Weibull distribution for reliability engineering.
 * L8: Bathtub curve and write cliff prediction.
 */

void endurance_spec_init(EnduranceSpec *spec, double capacity_gb,
                         double dwpd, double warranty_years, double waf) {
    memset(spec, 0, sizeof(EnduranceSpec));
    spec->capacity_gb = capacity_gb;
    spec->dwpd = dwpd;
    spec->warranty_years = warranty_years;
    spec->waf_target = waf;
    spec->tbw = endurance_compute_tbw(dwpd, capacity_gb, warranty_years);
    if (dwpd >= 10.0) spec->eclass = ENDURANCE_ENTERPRISE;
    else if (dwpd >= 1.0) spec->eclass = ENDURANCE_MIXED;
    else spec->eclass = ENDURANCE_READ_INTENSIVE;
}

void endurance_tracker_init(EnduranceTracker *tracker,
                            const EnduranceSpec *spec, double op_pct) {
    memset(tracker, 0, sizeof(EnduranceTracker));
    tracker->capacity_gb = spec->capacity_gb;
    tracker->overprovisioning_pct = op_pct;
    tracker->pe_cycles_rated = 10000.0;
    tracker->pe_cycles_used = 0.0;
    tracker->life_used_pct = 0.0;
    tracker->waf = spec->waf_target;
    tracker->power_on_hours = 0;
    bathtub_init(&tracker->bathtub, 0.7, 4380.0, 2e6, 3.0, 43800.0);
}

void endurance_record_write(EnduranceTracker *t, double bytes,
                            double waf_current) {
    t->total_bytes_written += bytes;
    t->waf = waf_current;
    double capacity_bytes = t->capacity_gb * 1e9;
    double effective_capacity = capacity_bytes *
        (1.0 + t->overprovisioning_pct / 100.0);
    if (effective_capacity > 0.0) {
        t->pe_cycles_used = t->total_bytes_written * t->waf /
                            effective_capacity;
    }
    t->life_used_pct = endurance_life_used(t->pe_cycles_used,
                                           t->pe_cycles_rated);
    if (t->power_on_hours > 24) {
        double days = (double)t->power_on_hours / 24.0;
        double bytes_per_day = t->total_bytes_written / days;
        t->dwpd_actual = endurance_compute_dwpd(bytes_per_day,
                                                 t->capacity_gb);
    }
}

double endurance_life_used(double pe_actual, double pe_rated) {
    if (pe_rated <= 0.0) return 0.0;
    return (pe_actual / pe_rated) * 100.0;
}

double endurance_compute_dwpd(double bytes_written_per_day,
                              double capacity_gb) {
    double capacity_bytes = capacity_gb * 1e9;
    if (capacity_bytes <= 0.0) return 0.0;
    return bytes_written_per_day / capacity_bytes;
}

double endurance_compute_tbw(double dwpd, double capacity_gb,
                             double warranty_years) {
    return dwpd * capacity_gb * 365.0 * warranty_years / 1000.0;
}

/* Weibull CDF: F(t) = 1 - exp(-(t/eta)^beta) */
double weibull_cdf(double time_hours, double beta, double eta) {
    if (eta <= 0.0 || time_hours < 0.0) return 0.0;
    return 1.0 - exp(-pow(time_hours / eta, beta));
}

/* Weibull failure rate: lambda(t) = (beta/eta) * (t/eta)^(beta-1) */
double weibull_failure_rate(double time_hours, double beta, double eta) {
    if (eta <= 0.0 || time_hours <= 0.0) return 0.0;
    return (beta / eta) * pow(time_hours / eta, beta - 1.0);
}

double weibull_mttf(double beta, double eta) {
    return eta * gamma_lanczos(1.0 + 1.0 / beta);
}

/* Gamma function via Lanczos approximation (accuracy ~1e-10) */
double gamma_lanczos(double x) {
    double g = 7.0;
    double c[9] = {
        0.99999999999980993, 676.5203681218851,
        -1259.1392167224028, 771.32342877765313,
        -176.61502916214059, 12.507343278686905,
        -0.13857109526572012, 9.9843695780195716e-6,
        1.5056327351493116e-7
    };
    int i;
    double z = x;
    if (z <= 0.0) return gamma_lanczos(z + 1.0) / z;
    z -= 1.0;
    double sum = c[0];
    for (i = 1; i < 9; i++) sum += c[i] / (z + (double)i);
    double t = z + g + 0.5;
    return sqrt(2.0 * 3.14159265358979323846) *
           pow(t, z + 0.5) * exp(-t) * sum;
}

/* AFR = 1 - exp(-8760 / MTBF_hours) */
double endurance_afr(double mtbf_hours) {
    if (mtbf_hours <= 0.0) return 1.0;
    return 1.0 - exp(-8760.0 / mtbf_hours);
}

bool endurance_write_cliff_warning(const EnduranceTracker *t,
                                   double rber_current, double rber_prev) {
    if (t->pe_cycles_used < t->pe_cycles_rated * 0.7) return false;
    if (rber_prev <= 0.0) return false;
    double rber_ratio = rber_current / rber_prev;
    if (rber_ratio > 2.0 && t->pe_cycles_used > t->pe_cycles_rated * 0.85)
        return true;
    return false;
}

void bathtub_init(BathtubModel *b, double infant_beta,
                  double infant_end_hours, double useful_mtbf,
                  double wearout_eta, double wearout_beta) {
    memset(b, 0, sizeof(BathtubModel));
    b->phase = BATHTUB_INFANT;
    b->time_hours = 0.0;
    b->failure_rate = 0.0;
    b->cumulative_failures = 0.0;
    b->weibull.beta = infant_beta;
    b->weibull.eta  = infant_end_hours;
    (void)useful_mtbf; (void)wearout_eta; (void)wearout_beta;
}

void bathtub_evaluate(BathtubModel *b, double time_hours) {
    b->time_hours = time_hours;
    if (time_hours < 4380.0) {
        b->phase = BATHTUB_INFANT;
        b->weibull.beta = 0.7;
        b->weibull.eta  = 8760.0;
    } else if (time_hours < 43800.0) {
        b->phase = BATHTUB_USEFUL;
        b->weibull.beta = 1.0;
        b->weibull.eta  = 2e6;
    } else {
        b->phase = BATHTUB_WEAROUT;
        b->weibull.beta = 3.0;
        b->weibull.eta  = 87600.0;
    }
    b->failure_rate = weibull_failure_rate(time_hours,
                                           b->weibull.beta, b->weibull.eta);
    b->cumulative_failures = weibull_cdf(time_hours,
                                         b->weibull.beta, b->weibull.eta);
}

const char *bathtub_phase_name(BathtubPhase phase) {
    switch (phase) {
    case BATHTUB_INFANT:  return "Infant Mortality";
    case BATHTUB_USEFUL:  return "Useful Life";
    case BATHTUB_WEAROUT: return "Wear-out";
    default: return "Unknown";
    }
}

void endurance_print_report(const EnduranceTracker *t) {
    printf("SSD Endurance Report:\n");
    printf("  Capacity: %.0f GB\n", t->capacity_gb);
    printf("  Total Bytes Written: %.2f TB\n",
           t->total_bytes_written / 1e12);
    printf("  WAF: %.2f\n", t->waf);
    printf("  Over-provisioning: %.1f%%\n", t->overprovisioning_pct);
    printf("  P/E Cycles Used: %.0f / %.0f (%.1f%%)\n",
           t->pe_cycles_used, t->pe_cycles_rated, t->life_used_pct);
    printf("  DWPD Actual: %.3f\n", t->dwpd_actual);
    printf("  AFR Estimate: %.4f%%\n", t->afr_estimate * 100.0);
    printf("  Power-on Hours: %llu\n",
           (unsigned long long)t->power_on_hours);
    printf("  Bathtub Phase: %s\n",
           bathtub_phase_name(t->bathtub.phase));
    printf("  Failure Rate: %.6f FPMH\n",
           t->bathtub.failure_rate * 1e6);
    printf("  Cumulative Failures: %.2f%%\n",
           t->bathtub.cumulative_failures * 100.0);
}
