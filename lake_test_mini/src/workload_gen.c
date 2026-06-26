/**
 * workload_gen.c — Workload Generation Implementation
 *
 * L5: Zipfian key generation using fast rejection sampling (Gray & Shenoy 2000).
 * L5: Cost estimation for query operators (Volcano-style cost model).
 * L4: Little's Law and Amdahl's Law for query execution analysis.
 * L7: TPC-H, TPC-DS, and log analytics workload profiles.
 */

#include "workload_gen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Simple PRNG for reproducible workloads */
static uint64_t wl_rand_state = 1234567;

static uint64_t wl_rand_next(void) {
    uint64_t x = wl_rand_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    wl_rand_state = x;
    return x;
}

static double wl_rand_uniform(void) {
    return (double)(wl_rand_next() & 0xFFFFFFFFFFFFULL) / (double)0xFFFFFFFFFFFFULL;
}

static void wl_rand_seed(uint64_t seed) {
    wl_rand_state = seed ? seed : 1234567;
}

/* ============================================================================
 * Workload Configuration Initializers
 * ============================================================================ */

void workload_config_init_tpch(WorkloadConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(WorkloadConfig));
    
    strncpy(cfg->workload_name, "TPC-H Decision Support", sizeof(cfg->workload_name) - 1);
    cfg->num_tables = 8;
    cfg->total_data_gb = 100;
    cfg->num_queries = 22;
    cfg->duration_sec = 3600.0;
    cfg->enable_skew = false;
    cfg->skew_factor = 0.0;
    cfg->primary_format = LAKE_FORMAT_PARQUET;
    
    /* Sequential scans dominate TPC-H */
    cfg->scan_pattern.key_space_size = 100000000;
    cfg->scan_pattern.num_accesses = 1000000;
    cfg->scan_pattern.zipf_alpha = 0.0;
    cfg->scan_pattern.sequential_ratio = 0.9;
    cfg->scan_pattern.read_ratio = 1.0;
    
    cfg->lookup_pattern.key_space_size = 1000000;
    cfg->lookup_pattern.num_accesses = 100000;
    cfg->lookup_pattern.zipf_alpha = 1.2;
    cfg->lookup_pattern.sequential_ratio = 0.0;
    cfg->lookup_pattern.read_ratio = 1.0;
    
    cfg->join_pattern.key_space_size = 1000000;
    cfg->join_pattern.num_accesses = 500000;
    cfg->join_pattern.zipf_alpha = 0.8;
    cfg->join_pattern.sequential_ratio = 0.3;
    cfg->join_pattern.read_ratio = 1.0;
}

void workload_config_init_tpcds(WorkloadConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(WorkloadConfig));
    
    strncpy(cfg->workload_name, "TPC-DS Complex Analytics", sizeof(cfg->workload_name) - 1);
    cfg->num_tables = 24;
    cfg->total_data_gb = 500;
    cfg->num_queries = 99;
    cfg->duration_sec = 7200.0;
    cfg->enable_skew = true;
    cfg->skew_factor = 1.0;
    cfg->primary_format = LAKE_FORMAT_PARQUET;
    
    cfg->scan_pattern.key_space_size = 500000000;
    cfg->scan_pattern.num_accesses = 5000000;
    cfg->scan_pattern.zipf_alpha = 0.3;
    cfg->scan_pattern.sequential_ratio = 0.7;
    cfg->scan_pattern.read_ratio = 1.0;
    
    cfg->lookup_pattern.key_space_size = 5000000;
    cfg->lookup_pattern.num_accesses = 500000;
    cfg->lookup_pattern.zipf_alpha = 1.5;
    cfg->lookup_pattern.sequential_ratio = 0.05;
    cfg->lookup_pattern.read_ratio = 1.0;
    
    cfg->join_pattern.key_space_size = 5000000;
    cfg->join_pattern.num_accesses = 2000000;
    cfg->join_pattern.zipf_alpha = 1.0;
    cfg->join_pattern.sequential_ratio = 0.2;
    cfg->join_pattern.read_ratio = 1.0;
}

void workload_config_init_log(WorkloadConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(WorkloadConfig));
    
    strncpy(cfg->workload_name, "Log Analytics", sizeof(cfg->workload_name) - 1);
    cfg->num_tables = 1;
    cfg->total_data_gb = 1000;
    cfg->num_queries = 1000;
    cfg->duration_sec = 3600.0;
    cfg->enable_skew = false;
    cfg->skew_factor = 0.0;
    cfg->primary_format = LAKE_FORMAT_PARQUET;
    
    /* Log analytics: mostly sequential scans of time-ordered data */
    cfg->scan_pattern.key_space_size = 1000000000;
    cfg->scan_pattern.num_accesses = 10000000;
    cfg->scan_pattern.zipf_alpha = 0.0;
    cfg->scan_pattern.sequential_ratio = 0.95;
    cfg->scan_pattern.read_ratio = 1.0;
    
    cfg->lookup_pattern.key_space_size = 10000000;
    cfg->lookup_pattern.num_accesses = 100000;
    cfg->lookup_pattern.zipf_alpha = 0.5;
    cfg->lookup_pattern.sequential_ratio = 0.1;
    cfg->lookup_pattern.read_ratio = 1.0;
    
    cfg->join_pattern.key_space_size = 1000000;
    cfg->join_pattern.num_accesses = 50000;
    cfg->join_pattern.zipf_alpha = 0.0;
    cfg->join_pattern.sequential_ratio = 0.0;
    cfg->join_pattern.read_ratio = 1.0;
}

/* ============================================================================
 * Zipfian Sequence Generation (L5: Gray & Shenoy algorithm)
 * ============================================================================ */

AccessSequence *workload_gen_zipf_sequence(uint64_t key_space, uint64_t num_keys,
                                            double alpha, uint64_t seed) {
    if (key_space == 0 || num_keys == 0) return NULL;
    
    wl_rand_seed(seed);
    
    AccessSequence *seq = (AccessSequence *)calloc(1, sizeof(AccessSequence));
    if (!seq) return NULL;
    
    seq->keys = (uint64_t *)calloc(num_keys, sizeof(uint64_t));
    if (!seq->keys) {
        free(seq);
        return NULL;
    }
    seq->num_keys = num_keys;
    
    /* Precompute normalization constant H(N, alpha)
     * H(N, alpha) = sum_{k=1..N} 1/k^alpha */
    double harmonic = 0.0;
    for (uint64_t k = 1; k <= key_space && k <= 1000000; k++) {
        harmonic += 1.0 / pow((double)k, alpha);
    }
    
    /* Fast rejection sampling method */
    uint64_t range = key_space;
    for (uint64_t i = 0; i < num_keys; i++) {
        double u = wl_rand_uniform();
        double cumulative = 0.0;
        uint64_t key = 1;
        
        /* Linear search through CDF (suitable for moderate key_space).
         * For large key_space, binary search would be used in production. */
        for (uint64_t k = 1; k <= range && k <= 1000000; k++) {
            cumulative += (1.0 / pow((double)k, alpha)) / harmonic;
            if (cumulative >= u) {
                key = k;
                break;
            }
        }
        
        seq->keys[i] = key;
    }
    
    /* Compute actual entropy and skew */
    seq->actual_entropy = workload_gen_entropy(seq->keys, num_keys);
    seq->actual_skew = workload_gen_gini(seq->keys, num_keys);
    
    return seq;
}

AccessSequence *workload_gen_sequential(uint64_t start_key, uint64_t num_keys,
                                         uint64_t stride) {
    if (num_keys == 0) return NULL;
    
    AccessSequence *seq = (AccessSequence *)calloc(1, sizeof(AccessSequence));
    if (!seq) return NULL;
    
    seq->keys = (uint64_t *)calloc(num_keys, sizeof(uint64_t));
    if (!seq->keys) {
        free(seq);
        return NULL;
    }
    seq->num_keys = num_keys;
    
    for (uint64_t i = 0; i < num_keys; i++) {
        seq->keys[i] = start_key + i * stride;
    }
    
    seq->actual_entropy = workload_gen_entropy(seq->keys, num_keys);
    seq->actual_skew = workload_gen_gini(seq->keys, num_keys);
    
    return seq;
}

AccessSequence *workload_gen_mixed(uint64_t key_space, uint64_t num_keys,
                                    double seq_ratio, double alpha, uint64_t seed) {
    if (num_keys == 0) return NULL;
    
    wl_rand_seed(seed);
    
    AccessSequence *seq = (AccessSequence *)calloc(1, sizeof(AccessSequence));
    if (!seq) return NULL;
    
    seq->keys = (uint64_t *)calloc(num_keys, sizeof(uint64_t));
    if (!seq->keys) {
        free(seq);
        return NULL;
    }
    seq->num_keys = num_keys;
    
    uint64_t seq_count = (uint64_t)((double)num_keys * seq_ratio);
    uint64_t rand_count = num_keys - seq_count;
    
    /* Generate sequential portion */
    for (uint64_t i = 0; i < seq_count; i++) {
        seq->keys[i] = i % key_space;
    }
    
    /* Generate Zipfian portion */
    double harmonic = 0.0;
    for (uint64_t k = 1; k <= key_space && k <= 1000000; k++) {
        harmonic += 1.0 / pow((double)k, alpha);
    }
    
    for (uint64_t i = 0; i < rand_count; i++) {
        double u = wl_rand_uniform();
        double cumulative = 0.0;
        uint64_t key = 1;
        
        for (uint64_t k = 1; k <= key_space && k <= 1000000; k++) {
            cumulative += (1.0 / pow((double)k, alpha)) / harmonic;
            if (cumulative >= u) {
                key = k;
                break;
            }
        }
        seq->keys[seq_count + i] = key;
    }
    
    seq->actual_entropy = workload_gen_entropy(seq->keys, num_keys);
    seq->actual_skew = workload_gen_gini(seq->keys, num_keys);
    
    return seq;
}

/* ============================================================================
 * Statistical Measures
 * ============================================================================ */

double workload_gen_gini(const uint64_t *keys, size_t num_keys) {
    if (!keys || num_keys == 0) return 0.0;
    
    /* Compute frequency of each unique key */
    /* First find the maximum key value */
    uint64_t max_key = 0;
    for (size_t i = 0; i < num_keys; i++) {
        if (keys[i] > max_key) max_key = keys[i];
    }
    
    /* Use a simple histogram approach for moderate key ranges */
    uint64_t hist_size = (max_key < 100000) ? max_key + 1 : 100001;
    uint64_t *freq = (uint64_t *)calloc(hist_size, sizeof(uint64_t));
    if (!freq) return 0.0;
    
    for (size_t i = 0; i < num_keys; i++) {
        if (keys[i] < hist_size) {
            freq[keys[i]]++;
        }
    }
    
    /* Sort frequencies (bubble sort — adequate for small counts) */
    size_t unique = 0;
    for (uint64_t k = 1; k < hist_size; k++) {
        if (freq[k] > 0) unique++;
    }
    
    double *sorted = (double *)malloc(unique * sizeof(double));
    if (!sorted) { free(freq); return 0.0; }
    
    size_t idx = 0;
    for (uint64_t k = 1; k < hist_size && idx < unique; k++) {
        if (freq[k] > 0) {
            sorted[idx++] = (double)freq[k];
        }
    }
    
    /* Sort ascending */
    for (size_t i = 0; i < unique; i++) {
        for (size_t j = i + 1; j < unique; j++) {
            if (sorted[i] > sorted[j]) {
                double tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    /* Gini coefficient: G = (2 * sum(i * y_i)) / (n * sum(y_i)) - (n+1)/n
     * where y_i are sorted ascending, i is 1-indexed.
     * For equal values, G = 0. For maximal inequality, G → 1. */
    double sum_y = 0.0;
    for (size_t i = 0; i < unique; i++) sum_y += sorted[i];

    if (sum_y == 0.0) { free(freq); free(sorted); return 0.0; }

    double weighted_sum = 0.0;
    for (size_t i = 0; i < unique; i++) {
        weighted_sum += (double)(i + 1) * sorted[i];
    }

    double n = (double)unique;
    double gini = (2.0 * weighted_sum) / (n * sum_y) - (n + 1.0) / n;
    
    free(freq);
    free(sorted);
    return gini;
}

double workload_gen_entropy(const uint64_t *keys, size_t num_keys) {
    if (!keys || num_keys == 0) return 0.0;
    
    uint64_t max_key = 0;
    for (size_t i = 0; i < num_keys; i++) {
        if (keys[i] > max_key) max_key = keys[i];
    }
    
    uint64_t hist_size = (max_key < 100000) ? max_key + 1 : 100001;
    uint64_t *freq = (uint64_t *)calloc(hist_size, sizeof(uint64_t));
    if (!freq) return 0.0;
    
    for (size_t i = 0; i < num_keys; i++) {
        if (keys[i] < hist_size) {
            freq[keys[i]]++;
        }
    }
    
    double entropy = 0.0;
    double total = (double)num_keys;
    for (uint64_t k = 0; k < hist_size; k++) {
        if (freq[k] > 0) {
            double p = (double)freq[k] / total;
            entropy -= p * log2(p);
        }
    }
    
    free(freq);
    return entropy;
}

void workload_gen_seq_destroy(AccessSequence *seq) {
    if (!seq) return;
    free(seq->keys);
    free(seq);
}

/* ============================================================================
 * Complete Workload Generation
 * ============================================================================ */

GeneratedWorkload *workload_generate(const WorkloadConfig *cfg) {
    if (!cfg) return NULL;
    
    GeneratedWorkload *wl = (GeneratedWorkload *)calloc(1, sizeof(GeneratedWorkload));
    if (!wl) return NULL;
    
    wl->config = *cfg;
    wl->num_queries = cfg->num_queries;
    wl->queries = (QueryPlan *)calloc(cfg->num_queries, sizeof(QueryPlan));
    
    if (!wl->queries) {
        free(wl);
        return NULL;
    }
    
    /* Generate synthetic query plans with cost estimation */
    CostModelParams cost_params;
    cost_model_init_default(&cost_params);
    
    for (uint64_t i = 0; i < cfg->num_queries; i++) {
        /* Each query is a table scan + optional filter + optional aggregation */
        wl->queries[i].query_id = i;
        snprintf(wl->queries[i].query_name, sizeof(wl->queries[i].query_name),
                 "Query_%lu", (unsigned long)i);
        
        /* Allocate operators */
        wl->queries[i].num_operators = 3; /* scan, filter, agg */
        wl->queries[i].operators = (QueryOperator *)calloc(3, sizeof(QueryOperator));
        if (!wl->queries[i].operators) continue;
        
        /* Table scan */
        wl->queries[i].operators[0].type = OP_TABLE_SCAN;
        wl->queries[i].operators[0].estimated_rows_in = 10000000;
        wl->queries[i].operators[0].estimated_rows_out = 10000000;
        wl->queries[i].operators[0].estimated_bytes_in =
            wl->queries[i].operators[0].estimated_rows_in * 256.0;
        wl->queries[i].operators[0].estimated_bytes_out =
            wl->queries[i].operators[0].estimated_bytes_in;
        wl->queries[i].operators[0].operator_id = 0;
        wl->queries[i].operators[0].parent_id = 0;
        
        CostEstimate scan_cost = cost_estimate_table_scan(&cost_params,
            (uint64_t)wl->queries[i].operators[0].estimated_rows_in, 256.0);
        wl->queries[i].operators[0].cpu_cost_ms = scan_cost.total_cost;
        wl->queries[i].operators[0].io_cost_ms = scan_cost.startup_cost;
        
        /* Filter */
        wl->queries[i].operators[1].type = OP_FILTER;
        wl->queries[i].operators[1].estimated_rows_in = 10000000;
        wl->queries[i].operators[1].estimated_rows_out = 1000000;
        wl->queries[i].operators[1].operator_id = 1;
        wl->queries[i].operators[1].parent_id = 0;
        wl->queries[i].operators[1].cpu_cost_ms = 10.0;
        
        /* Aggregation */
        wl->queries[i].operators[2].type = OP_HASH_AGG;
        wl->queries[i].operators[2].estimated_rows_in = 1000000;
        wl->queries[i].operators[2].estimated_rows_out = 100;
        wl->queries[i].operators[2].operator_id = 2;
        wl->queries[i].operators[2].parent_id = 1;
        
        CostEstimate agg_cost = cost_estimate_hash_agg(&cost_params,
            (uint64_t)wl->queries[i].operators[2].estimated_rows_in, 10,
            wl->queries[i].operators[2].estimated_bytes_in);
        wl->queries[i].operators[2].cpu_cost_ms = agg_cost.total_cost;
        wl->queries[i].operators[2].memory_bytes = agg_cost.plan_width;
        
        /* Aggregate costs */
        wl->queries[i].total_cpu_cost_ms = scan_cost.total_cost + 10.0 + agg_cost.total_cost;
        wl->queries[i].total_io_cost_ms = scan_cost.startup_cost;
        wl->queries[i].total_memory_bytes = agg_cost.plan_width;
        wl->queries[i].peak_memory_bytes = wl->queries[i].total_memory_bytes * 1.5;
        
        wl->total_estimated_cpu_ms += wl->queries[i].total_cpu_cost_ms;
        wl->total_estimated_io_gb += wl->queries[i].total_io_cost_ms * 0.1; /* sim */
        wl->total_bytes_processed += (uint64_t)wl->queries[i].operators[0].estimated_bytes_in;
        wl->total_rows_processed += (uint64_t)wl->queries[i].operators[0].estimated_rows_in;
    }
    
    return wl;
}

void workload_destroy(GeneratedWorkload *wl) {
    if (!wl) return;
    if (wl->queries) {
        for (size_t i = 0; i < wl->num_queries; i++) {
            free(wl->queries[i].operators);
        }
        free(wl->queries);
    }
    if (wl->sequences) {
        for (size_t i = 0; i < wl->num_sequences; i++) {
            free(wl->sequences[i].keys);
        }
        free(wl->sequences);
    }
    free(wl);
}

void workload_print_summary(const GeneratedWorkload *wl) {
    if (!wl) return;
    
    printf("\n========== Generated Workload Summary ==========\n");
    printf("Workload:     %s\n", wl->config.workload_name);
    printf("Tables:       %lu\n", (unsigned long)wl->config.num_tables);
    printf("Data Size:    %lu GB\n", (unsigned long)wl->config.total_data_gb);
    printf("Queries:      %lu\n", (unsigned long)wl->num_queries);
    printf("Duration:     %.0f sec\n", wl->config.duration_sec);
    printf("Total CPU:    %.1f sec (estimated)\n", wl->total_estimated_cpu_ms / 1000.0);
    printf("Total I/O:    %.1f GB (estimated)\n", wl->total_estimated_io_gb);
    printf("Total Bytes:  %lu\n", (unsigned long)wl->total_bytes_processed);
    printf("Total Rows:   %lu\n", (unsigned long)wl->total_rows_processed);
    printf("=================================================\n");
}

/* ============================================================================
 * Cost Model Implementation (Volcano-style)
 * ============================================================================ */

void cost_model_init_default(CostModelParams *params) {
    if (!params) return;
    memset(params, 0, sizeof(CostModelParams));
    
    /* PostgreSQL-style defaults */
    params->tuple_size_bytes = 256.0;
    params->cpu_tuple_cost = 0.01;
    params->cpu_operator_cost = 0.0025;
    params->cpu_index_tuple_cost = 0.005;
    params->seq_page_cost = 1.0;
    params->random_page_cost = 4.0;
    params->page_size_bytes = 8192.0;
    params->hash_join_cost_per_tuple = 0.05;
    params->sort_cost_per_tuple = 0.1;
    params->effective_cache_size = 4ULL * 1024 * 1024 * 1024; /* 4 GB */
    params->memory_granularity_bytes = 1024.0;
}

CostEstimate cost_estimate_table_scan(const CostModelParams *params,
                                       uint64_t num_rows, double row_width) {
    CostEstimate est;
    memset(&est, 0, sizeof(est));
    
    if (!params) return est;
    
    double total_bytes = (double)num_rows * row_width;
    double pages = ceil(total_bytes / params->page_size_bytes);
    
    est.startup_cost = 0.0;
    est.total_cost = params->seq_page_cost * pages +
                     params->cpu_tuple_cost * (double)num_rows;
    est.plan_rows = (double)num_rows;
    est.plan_width = row_width;
    
    return est;
}

CostEstimate cost_estimate_hash_join(const CostModelParams *params,
                                      uint64_t outer_rows, uint64_t inner_rows,
                                      double outer_width, double inner_width) {
    CostEstimate est;
    memset(&est, 0, sizeof(est));
    
    if (!params) return est;
    
    /* Build cost: scan inner + build hash table */
    double build_cost = params->cpu_operator_cost * (double)inner_rows;
    build_cost += params->cpu_tuple_cost * (double)inner_rows * 2.0;
    
    /* Probe cost: scan outer + probe hash table */
    double probe_cost = params->cpu_operator_cost * (double)outer_rows;
    probe_cost += params->cpu_tuple_cost * (double)outer_rows;
    
    est.startup_cost = build_cost;
    est.total_cost = build_cost + probe_cost;
    est.plan_rows = (double)(outer_rows > inner_rows ? outer_rows : inner_rows);
    est.plan_width = outer_width + inner_width;
    
    return est;
}

CostEstimate cost_estimate_sort(const CostModelParams *params,
                                 uint64_t num_rows, double row_width) {
    CostEstimate est;
    memset(&est, 0, sizeof(est));
    
    if (!params) return est;
    
    /* O(N log N) comparison cost + I/O for external sort */
    double log_n = log2((double)num_rows);
    double comparison_cost = params->cpu_operator_cost * (double)num_rows * log_n;
    
    est.startup_cost = comparison_cost * 0.1;
    est.total_cost = comparison_cost;
    est.plan_rows = (double)num_rows;
    est.plan_width = row_width;
    
    return est;
}

CostEstimate cost_estimate_hash_agg(const CostModelParams *params,
                                     uint64_t num_rows, uint32_t num_groups,
                                     double row_width) {
    CostEstimate est;
    memset(&est, 0, sizeof(est));
    
    if (!params) return est;
    
    /* Hash aggregate: hash each input row, update aggregate state */
    double hash_cost = params->cpu_operator_cost * (double)num_rows;
    double agg_cost = params->cpu_tuple_cost * (double)num_rows;
    
    est.startup_cost = 0.0;
    est.total_cost = hash_cost + agg_cost;
    est.plan_rows = (double)num_groups;
    est.plan_width = row_width * 0.5; /* Aggregates typically narrower */
    
    return est;
}

QueryPlan *query_plan_build(const QueryOperator *operators, size_t num_ops,
                             const CostModelParams *cost_params) {
    (void)cost_params; /* Cost estimation is done during construction */
    if (!operators || num_ops == 0) return NULL;
    
    QueryPlan *plan = (QueryPlan *)calloc(1, sizeof(QueryPlan));
    if (!plan) return NULL;
    
    plan->num_operators = num_ops;
    plan->operators = (QueryOperator *)calloc(num_ops, sizeof(QueryOperator));
    if (!plan->operators) {
        free(plan);
        return NULL;
    }
    
    memcpy(plan->operators, operators, num_ops * sizeof(QueryOperator));
    
    /* Sum costs */
    for (size_t i = 0; i < num_ops; i++) {
        plan->total_cpu_cost_ms += plan->operators[i].cpu_cost_ms;
        plan->total_io_cost_ms += plan->operators[i].io_cost_ms;
        plan->total_memory_bytes += plan->operators[i].memory_bytes;
        if (plan->operators[i].memory_bytes > plan->peak_memory_bytes) {
            plan->peak_memory_bytes = plan->operators[i].memory_bytes;
        }
    }
    
    return plan;
}

void query_plan_print(const QueryPlan *plan) {
    if (!plan) return;
    
    printf("\n========== Query Plan: %s ==========\n", plan->query_name);
    printf("Operators: %lu\n", (unsigned long)plan->num_operators);
    printf("%-4s %-20s %-14s %-14s %-12s %-12s\n",
           "ID", "Type", "RowsIn", "RowsOut", "CPU(ms)", "IO(ms)");
    printf("---------------------------------------------------------------\n");
    
    for (size_t i = 0; i < plan->num_operators; i++) {
        const char *type_names[] = {
            "TABLE_SCAN", "INDEX_SCAN", "FILTER", "PROJECT",
            "HASH_JOIN", "MERGE_JOIN", "NESTED_LOOP_JOIN",
            "HASH_AGG", "SORT", "LIMIT", "UNION"
        };
        printf("%-4u %-20s %-12.0f   %-12.0f   %-10.2f   %-10.2f\n",
               plan->operators[i].operator_id,
               type_names[plan->operators[i].type],
               plan->operators[i].estimated_rows_in,
               plan->operators[i].estimated_rows_out,
               plan->operators[i].cpu_cost_ms,
               plan->operators[i].io_cost_ms);
    }
    
    printf("---------------------------------------------------------------\n");
    printf("Total CPU: %.1f ms | Total IO: %.1f ms | Peak Mem: %.1f MB\n",
           plan->total_cpu_cost_ms, plan->total_io_cost_ms,
           plan->peak_memory_bytes / (1024.0 * 1024.0));
    printf("======================================\n");
}

void query_plan_destroy(QueryPlan *plan) {
    if (!plan) return;
    free(plan->operators);
    free(plan);
}

/* ============================================================================
 * L4: Queueing Theory Applied to Query Execution
 * ============================================================================ */

uint32_t workload_littles_law_concurrency(double target_qps, double avg_query_time_ms) {
    /* L = lambda * W
     * L = target_qps * (avg_query_time_ms / 1000.0)
     * Returns concurrency needed, rounded up */
    if (avg_query_time_ms <= 0.0) return 1;
    double L = target_qps * (avg_query_time_ms / 1000.0);
    return (uint32_t)ceil(L);
}

double workload_amdahl_speedup(double parallel_fraction, uint32_t num_processors) {
    /* Speedup = 1 / ((1-P) + P/N)
     * where P = parallel_fraction, N = num_processors */
    if (num_processors == 0) return 1.0;
    double serial = 1.0 - parallel_fraction;
    double denominator = serial + parallel_fraction / (double)num_processors;
    return (denominator > 0.0) ? (1.0 / denominator) : (double)num_processors;
}