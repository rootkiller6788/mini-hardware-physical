#ifndef NAND_MODEL_H
#define NAND_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* NAND flash memory cell physics model.
 *
 * L2: Core concept — floating gate / charge trap transistor,
 *     Fowler-Nordheim tunneling, program/erase mechanisms.
 * L4: Arrhenius law for data retention, ISPP model for programming.
 *
 * References:
 *   - Cai et al., "Threshold Voltage Distribution in MLC NAND Flash",
 *     IEEE Trans. Electron Devices, 2013.
 *   - Micheloni et al., "Inside NAND Flash Memories", Springer 2010.
 */

#define NAND_MAX_PE_CYCLES      100000
#define NAND_PAGE_SIZE           18432
#define NAND_PAGES_PER_BLOCK     256
#define NAND_BLOCKS_PER_DIE      4
#define NAND_MAX_DIES            4

/* Cell types — each stores n bits via 2^n threshold voltage levels */
typedef enum {
    NAND_SLC = 1,   /* Single-Level Cell, 1 bit/cell */
    NAND_MLC = 2,   /* Multi-Level Cell,  2 bits/cell */
    NAND_TLC = 3,   /* Triple-Level Cell, 3 bits/cell */
    NAND_QLC = 4    /* Quad-Level Cell,   4 bits/cell */
} NANDCellType;

/* NAND cell threshold voltage (Vth) distribution parameters.
 * Each level follows a Gaussian N(μ, σ²) after ISPP.
 */
typedef enum {
    VTH_LEVEL_ER = 0,   /* erased state (negative Vth for SLC/MLC/TLC) */
    VTH_LEVEL_A  = 1,   /* first  programmed state */
    VTH_LEVEL_B  = 2,   /* second programmed state (MLC+) */
    VTH_LEVEL_C  = 3,   /* third  programmed state (MLC+) */
    VTH_LEVEL_D  = 4,
    VTH_LEVEL_E  = 5,
    VTH_LEVEL_F  = 6,
    VTH_LEVEL_G  = 7,   /* highest state (TLC) */
    VTH_NUM_LEVELS = 16
} VthLevel;

typedef struct {
    double mu;    /* mean threshold voltage */
    double sigma; /* standard deviation */
} VthDistribution;

/* Flash cell model per word-line */
typedef struct {
    NANDCellType type;
    int      bits_per_cell;
    int      num_levels;         /* 2^bits_per_cell */
    VthDistribution vth[VTH_NUM_LEVELS];
    int      pe_cycles;          /* program/erase cycle count */
    double   retention_time_sec;
    double   temperature_kelvin; /* operating temperature */
    double   rber;               /* raw bit error rate */
    double   read_disturb_accum;
} NANDCell;

/* Die-level model */
typedef struct {
    NANDCell  cells[NAND_BLOCKS_PER_DIE * NAND_PAGES_PER_BLOCK];
    int       blocks_per_die;
    int       pages_per_block;
    size_t    page_size_bytes;
    uint32_t  bad_blocks;
    uint32_t  total_blocks;
    uint64_t  bytes_read;
    uint64_t  bytes_written;
    uint64_t  total_pe_cycles;
} NANDDie;

/* NAND device (multiple dies, channels) */
typedef struct {
    NANDDie    dies[NAND_MAX_DIES];
    int        num_dies;
    int        channels;
    NANDCellType cell_type;
    double     endurance_rating;  /* rated P/E cycles */
    double     typical_rber_begin;
    double     typical_rber_end;
} NANDDevice;

/* Initialize cell with ISPP model.
 * ISPP: Incremental Step Pulse Programming.
 *   Vth shift per pulse ≈ constant (≈0.2V for MLC).
 *   Number of pulses determines final Vth level.
 */
void nand_cell_init(NANDCell *cell, NANDCellType type);

/* Simulate N program pulses; returns final threshold voltage.
 * Vth(n) = Vth_erase + n * Vstep + noise
 */
double nand_ispp_program(NANDCell *cell, int target_level, int num_pulses);

/* Simulate erase operation (Fowler-Nordheim tunneling)
 * Returns the threshold voltage after erase.
 */
double nand_erase_cell(NANDCell *cell);

/* Read cell: sense amplifier compares Vth against reference voltages.
 * Returns the detected level (0..num_levels-1).
 */
int nand_read_cell(const NANDCell *cell, int wordline, int bitline);

/* Compute RBER from Vth distribution overlap:
 * RBER ≈ sum of Q((Vref_i - μ_i)/σ_i) * P(level_i)
 */
double nand_compute_rber(const NANDCell *cell);

/* Data retention model: Arrhenius law.
 *   t_ret = A * exp(Ea / (k * T))
 * where Ea ≈ 1.1 eV for electron de-trapping.
 */
double nand_retention_time(const NANDCell *cell, double temperature_kelvin);

/* Read disturb: each read on same block slightly programs cells */
double nand_read_disturb_shift(int neighbor_reads);

/* Cell-to-cell interference (CCI): floating gate coupling */
double nand_cci_shift(double delta_vth_neighbor, double coupling_ratio);

/* Endurance degradation: σ grows with P/E cycles */
void nand_endurance_degrade(NANDCell *cell, int pe_cycles);

void nand_device_init(NANDDevice *dev, NANDCellType type,
                      int num_dies, int channels);
void nand_print_cell_state(const NANDCell *cell);
void nand_print_device_info(const NANDDevice *dev);

#endif
