#include "nand_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * NAND flash memory cell physics model.
 * L2: Floating gate transistor. Fowler-Nordheim tunneling.
 * L4: ISPP model, Arrhenius law for data retention.
 */

void nand_cell_init(NANDCell *cell, NANDCellType type) {
    double base_mu = -2.5, base_sigma = 0.08, spacing;
    int i;
    memset(cell, 0, sizeof(NANDCell));
    cell->type = type;
    cell->bits_per_cell = (int)type;
    cell->num_levels = 1 << ((int)type);
    cell->pe_cycles = 0;
    cell->retention_time_sec = 0.0;
    cell->temperature_kelvin = 300.0;
    cell->rber = 0.0;
    cell->read_disturb_accum = 0.0;
    switch (type) {
    case NAND_SLC: spacing = 3.0; break;
    case NAND_MLC: spacing = 1.0; break;
    case NAND_TLC: spacing = 0.4; break;
    case NAND_QLC: spacing = 0.15; break;
    default: spacing = 1.0;
    }
    for (i = 0; i < cell->num_levels && i < VTH_NUM_LEVELS; i++) {
        cell->vth[i].mu = base_mu + i * spacing;
        cell->vth[i].sigma = base_sigma * (1.0 + 0.02 * i);
    }
}

double nand_ispp_program(NANDCell *cell, int target_level, int num_pulses) {
    double vth_erase = cell->vth[VTH_LEVEL_ER].mu;
    double vstep;
    int nlev = cell->num_levels;
    if (target_level < 1 || target_level >= nlev) return vth_erase;
    switch (cell->type) {
    case NAND_SLC: vstep = 0.40; break;
    case NAND_MLC: vstep = 0.25; break;
    case NAND_TLC: vstep = 0.12; break;
    case NAND_QLC: vstep = 0.06; break;
    default: vstep = 0.25;
    }
    vstep *= (1.0 + (double)cell->pe_cycles / 200000.0);
    double vth = vth_erase + num_pulses * vstep;
    double prog_noise = 0.05 * sqrt((double)num_pulses) *
                        ((double)rand() / RAND_MAX - 0.5) * 2.0;
    vth += prog_noise;
    double target_mu = cell->vth[target_level].mu;
    if (vth > target_mu + 0.5) vth = target_mu + 0.3;
    if (vth < target_mu - 0.3) vth = target_mu - 0.2;
    cell->vth[target_level].mu = vth;
    return vth;
}

double nand_erase_cell(NANDCell *cell) {
    double erase_target, noise;
    switch (cell->type) {
    case NAND_SLC: erase_target = -2.5; break;
    case NAND_MLC: erase_target = -2.5; break;
    case NAND_TLC: erase_target = -2.5; break;
    case NAND_QLC: erase_target = -2.0; break;
    default: erase_target = -2.5;
    }
    noise = (0.15 + 0.001 * (double)cell->pe_cycles) *
            ((double)rand() / RAND_MAX * 2.0 - 1.0);
    cell->vth[VTH_LEVEL_ER].mu = erase_target + noise;
    cell->vth[VTH_LEVEL_ER].sigma = 0.20 + 0.0005 * (double)cell->pe_cycles;
    cell->pe_cycles++;
    return cell->vth[VTH_LEVEL_ER].mu;
}

int nand_read_cell(const NANDCell *cell, int wordline, int bitline) {
    int i;
    double vth_measured, best_dist = 1e100;
    int best_level = 0;
    (void)wordline; (void)bitline;
    vth_measured = cell->vth[VTH_LEVEL_ER].mu +
                   (double)rand() / RAND_MAX * 4.0 - 2.0;
    for (i = 0; i < cell->num_levels && i < VTH_NUM_LEVELS; i++) {
        double dist = fabs(vth_measured - cell->vth[i].mu);
        if (dist < best_dist) { best_dist = dist; best_level = i; }
    }
    return best_level;
}

double nand_compute_rber(const NANDCell *cell) {
    int i;
    int nlev = cell->num_levels;
    double total_error = 0.0;
    if (nlev < 2) return 0.0;
    for (i = 0; i < nlev && i < VTH_NUM_LEVELS; i++) {
        double sigma = cell->vth[i].sigma;
        if (sigma < 1e-9) sigma = 1e-9;
        double mu = cell->vth[i].mu;
        if (i == 0) {
            double vref_low = (mu + cell->vth[1].mu) * 0.5;
            total_error += 0.5 * erfc((vref_low - mu) / (sigma * 1.414213562));
        } else if (i == nlev - 1) {
            double vref_high = (cell->vth[i-1].mu + mu) * 0.5;
            total_error += 0.5 * erfc((mu - vref_high) / (sigma * 1.414213562));
        } else {
            double vref_low  = (cell->vth[i-1].mu + mu) * 0.5;
            double vref_high = (mu + cell->vth[i+1].mu) * 0.5;
            total_error += 0.5 * erfc((vref_low - mu) / (sigma * 1.414213562));
            total_error += 0.5 * erfc((mu - vref_high) / (sigma * 1.414213562));
        }
    }
    double bits_per_sample = log2((double)nlev);
    if (bits_per_sample < 1.0) bits_per_sample = 1.0;
    return total_error / ((double)nlev * bits_per_sample);
}

double nand_retention_time(const NANDCell *cell, double temperature_kelvin) {
    double Ea = 1.1, k = 8.617333262e-5, A = 1e-12;
    double Ea_eff = Ea * (1.0 - (double)cell->pe_cycles / 500000.0);
    if (Ea_eff < 0.3) Ea_eff = 0.3;
    double t_ret = A * exp(Ea_eff / (k * temperature_kelvin));
    return t_ret / 3600.0;
}

double nand_read_disturb_shift(int neighbor_reads) {
    double disturb_per_read = 2e-7;
    double total_shift = disturb_per_read * (double)neighbor_reads;
    if (total_shift > 0.5) total_shift = 0.5 + 0.1 * log(1.0 + total_shift - 0.5);
    return total_shift;
}

double nand_cci_shift(double delta_vth_neighbor, double coupling_ratio) {
    return coupling_ratio * delta_vth_neighbor;
}

void nand_endurance_degrade(NANDCell *cell, int pe_cycles) {
    int i;
    cell->pe_cycles = pe_cycles;
    double pe_ratio = (double)pe_cycles / 10000.0;
    double log_factor = log(1.0 + pe_ratio);
    for (i = 0; i < cell->num_levels && i < VTH_NUM_LEVELS; i++) {
        double sigma0;
        switch (cell->type) {
        case NAND_SLC: sigma0 = 0.08; break;
        case NAND_MLC: sigma0 = 0.10; break;
        case NAND_TLC: sigma0 = 0.06; break;
        case NAND_QLC: sigma0 = 0.04; break;
        default: sigma0 = 0.10;
        }
        cell->vth[i].sigma = sigma0 * (1.0 + 0.6 * log_factor);
        cell->vth[i].mu += 0.05 * log_factor * i;
    }
    cell->rber = nand_compute_rber(cell);
}

void nand_device_init(NANDDevice *dev, NANDCellType type,
                      int num_dies, int channels) {
    int d;
    memset(dev, 0, sizeof(NANDDevice));
    dev->num_dies = num_dies;
    dev->channels = channels;
    dev->cell_type = type;
    dev->typical_rber_begin = 1e-5;
    dev->typical_rber_end = 0.01;
    for (d = 0; d < num_dies && d < NAND_MAX_DIES; d++) {
        dev->dies[d].blocks_per_die = NAND_BLOCKS_PER_DIE;
        dev->dies[d].pages_per_block = NAND_PAGES_PER_BLOCK;
        dev->dies[d].page_size_bytes = NAND_PAGE_SIZE;
        dev->dies[d].total_blocks = NAND_BLOCKS_PER_DIE;
        dev->dies[d].bad_blocks = 0;
    }
    switch (type) {
    case NAND_SLC: dev->endurance_rating = 100000.0; break;
    case NAND_MLC: dev->endurance_rating = 10000.0;  break;
    case NAND_TLC: dev->endurance_rating = 3000.0;   break;
    case NAND_QLC: dev->endurance_rating = 1000.0;   break;
    default: dev->endurance_rating = 10000.0;
    }
}

void nand_print_cell_state(const NANDCell *cell) {
    int i;
    printf("NAND Cell State:\n");
    printf("  Type: %s (%d bits/cell)\n",
           cell->type == NAND_SLC ? "SLC" :
           cell->type == NAND_MLC ? "MLC" :
           cell->type == NAND_TLC ? "TLC" : "QLC",
           cell->bits_per_cell);
    printf("  P/E Cycles: %d\n", cell->pe_cycles);
    printf("  Temperature: %.1f K\n", cell->temperature_kelvin);
    printf("  RBER: %.2e\n", cell->rber);
    printf("  Vth distributions:\n");
    for (i = 0; i < cell->num_levels && i < VTH_NUM_LEVELS; i++) {
        printf("    Level %d: mu=%.3fV sigma=%.3fV\n",
               i, cell->vth[i].mu, cell->vth[i].sigma);
    }
}

void nand_print_device_info(const NANDDevice *dev) {
    int d;
    printf("NAND Device Info:\n");
    printf("  Type: %d bits/cell, %d dies, %d channels\n",
           (int)dev->cell_type, dev->num_dies, dev->channels);
    printf("  Endurance rating: %.0f P/E cycles\n",
           dev->endurance_rating);
    printf("  RBER range: %.1e (begin) -> %.1e (end-of-life)\n",
           dev->typical_rber_begin, dev->typical_rber_end);
    for (d = 0; d < dev->num_dies && d < NAND_MAX_DIES; d++) {
        printf("  Die %d: %u blocks, %u pages/block, %zu bytes/page,"
               " %u bad blocks\n",
               d, dev->dies[d].total_blocks, dev->dies[d].pages_per_block,
               dev->dies[d].page_size_bytes, dev->dies[d].bad_blocks);
    }
}
