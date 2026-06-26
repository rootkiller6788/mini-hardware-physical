#ifndef PCIE_H
#define PCIE_H

#include <stdbool.h>
#include <stdint.h>

#define PCIE_MAX_LANES    16
#define PCIE_BAR_COUNT    6
#define PCIE_TLP_MAX_DATA 1024

typedef enum {
    PCIE_GEN1 = 1,
    PCIE_GEN2 = 2,
    PCIE_GEN3 = 3,
    PCIE_GEN4 = 4,
    PCIE_GEN5 = 5
} PCIEGen;

typedef enum {
    PCIE_DIR_TX = 0,
    PCIE_DIR_RX = 1
} PCIEDirection;

typedef enum {
    TLP_MEM_READ     = 0,
    TLP_MEM_WRITE    = 1,
    TLP_CFG_READ     = 2,
    TLP_CFG_WRITE    = 3,
    TLP_MSG          = 4,
    TLP_COMPLETION   = 5
} TLPPacketType;

typedef struct {
    int            gen;
    double         speed_gbps;
    PCIEDirection  direction;
} PCIELane;

typedef struct {
    int        num_lanes;
    PCIEGen    gen;
    double     speed;
    int        link_width;
    int        max_payload_size;
    PCIELane   lanes[PCIE_MAX_LANES];
} PCIELink;

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t bars[PCIE_BAR_COUNT];
    bool     msi_enabled;
    uint32_t msi_vector;
} PCIEConfig;

typedef struct {
    TLPPacketType type;
    uint16_t      requester_id;
    uint8_t       tag;
    uint64_t      address;
    uint8_t       data[PCIE_TLP_MAX_DATA];
    uint32_t      length;
    bool          has_data;
} TLPPacket;

PCIELink *pcie_link_init(int gen, int lanes);
void      pcie_link_destroy(PCIELink *link);
double    pcie_link_speed_gbps(const PCIELink *link);
double    pcie_link_bandwidth_gbps(const PCIELink *link);
TLPPacket pcie_tlp_create_read(uint64_t addr, int len);
TLPPacket pcie_tlp_create_write(uint64_t addr, const void *data, int len);
uint32_t  pcie_config_space_read(const PCIEConfig *cfg, int offset, int len);
void      pcie_config_space_write(PCIEConfig *cfg, int offset, uint32_t value);
void      pcie_print_link_info(const PCIELink *link);
void      pcie_print_tlp(const TLPPacket *tlp);
void      pcie_init_config(PCIEConfig *cfg, uint16_t vid, uint16_t did);

#endif
