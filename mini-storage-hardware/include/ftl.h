#ifndef FTL_H
#define FTL_H

#include <stdbool.h>
#include <stdint.h>

#define FTL_MAX_LBAS          16384
#define FTL_MAX_PHYSICAL_PAGES 65536
#define FTL_PAGES_PER_BLOCK    128
#define FTL_BLOCKS_PER_PLANE   16
#define FTL_PLANES             4
#define FTL_PAGE_DATA_SIZE     4096

typedef enum {
    PAGE_TYPE_LOWER,
    PAGE_TYPE_MIDDLE,
    PAGE_TYPE_UPPER
} PageType;

typedef enum {
    PAGE_STATE_FREE,
    PAGE_STATE_VALID,
    PAGE_STATE_INVALID,
    PAGE_STATE_BAD
} PageState;

typedef enum {
    BLOCK_STATE_FREE,
    BLOCK_STATE_ACTIVE,
    BLOCK_STATE_FULL,
    BLOCK_STATE_BAD
} BlockState;

typedef enum {
    FTL_MAPPING_PAGE_LEVEL,
    FTL_MAPPING_BLOCK_LEVEL,
    FTL_MAPPING_HYBRID
} FTLMappingMode;

typedef struct {
    uint32_t  id;
    PageType  type;
    PageState state;
    uint8_t   data[FTL_PAGE_DATA_SIZE];
} FlashPage;

typedef struct {
    FlashPage pages[FTL_PAGES_PER_BLOCK];
    uint32_t  erase_count;
    BlockState state;
} FlashBlock;

typedef struct {
    FlashBlock blocks[FTL_BLOCKS_PER_PLANE];
} FlashPlane;

typedef struct {
    uint64_t reads;
    uint64_t writes;
    uint64_t erases;
    uint64_t gc_moves;
    uint64_t host_writes;
} FTLStats;

typedef struct {
    FlashPlane     planes[FTL_PLANES];
    int32_t        mapping_table[FTL_MAX_LBAS];
    int32_t        reverse_map[FTL_MAX_PHYSICAL_PAGES];
    uint32_t       write_pointer;
    uint32_t       free_pages;
    FTLMappingMode mapping_mode;
    FTLStats       stats;
} FTL;

void ftl_init(FTL *ftl, FTLMappingMode mode);
int  ftl_read(const FTL *ftl, uint32_t lba, uint8_t *out_data);
int  ftl_write(FTL *ftl, uint32_t lba, const uint8_t *data);
int  ftl_trim(FTL *ftl, uint32_t lba);
int  ftl_allocate_block(FTL *ftl, uint32_t start_lba, uint32_t num_pages);

/* SLC write buffer (Turbo Write) — L8 */
void ftl_slc_buffer_init(void);
int  ftl_slc_buffer_write(FTL *ftl, uint32_t lba, const uint8_t *data);
int  ftl_slc_buffer_flush(FTL *ftl);

/* Hot/cold data separation — L5 */
void   ftl_hotcold_init(void);
bool   ftl_is_hot_lba(uint32_t lba);
void   ftl_track_update(uint32_t lba);
double ftl_hot_data_ratio(void);

/* Merge cost analysis for log-block FTL — L5 */
double ftl_merge_cost_estimate(uint32_t valid_pages_in_data,
                               uint32_t valid_pages_in_log,
                               uint32_t pages_per_block);

void ftl_print_stats(const FTL *ftl);

#endif
