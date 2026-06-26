#include "ftl.h"
#include "gc.h"
#include "wear_leveling.h"
#include "nvme.h"
#include "ecc.h"
#include "ldpc.h"
#include "nand_model.h"
#include "endurance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define T(name) do { printf("  [TEST] %s ... ", name); } while(0)
#define P() do { printf("PASS\n"); tests_passed++; } while(0)
#define F(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define EQ(a,b,m) do { if((a)!=(b)){F(m);return;} } while(0)
#define OK(c,m) do { if(!(c)){F(m);return;} } while(0)

static void test_ftl(void) {
    FTL ftl;
    uint8_t wb[4096], rb[4096];
    T("FTL init");
    ftl_init(&ftl, FTL_MAPPING_PAGE_LEVEL);
    EQ(ftl.free_pages, FTL_MAX_PHYSICAL_PAGES, "free");
    P();
    T("FTL write/read");
    memset(wb, 0xAB, 4096);
    EQ(ftl_write(&ftl, 0, wb), 0, "wr");
    EQ(ftl_read(&ftl, 0, rb), 0, "rd");
    EQ(memcmp(wb, rb, 4096), 0, "cmp");
    P();
    T("FTL overwrite");
    memset(wb, 0xCD, 4096);
    ftl_write(&ftl, 0, wb);
    EQ(ftl_read(&ftl, 0, rb), 0, "rd2");
    EQ(rb[0], 0xCD, "ovw");
    P();
    T("FTL trim");
    EQ(ftl_trim(&ftl, 0), 0, "trim");
    P();
    T("FTL invalid LBA");
    EQ(ftl_read(&ftl, FTL_MAX_LBAS, rb), -1, "bad rd");
    EQ(ftl_write(&ftl, FTL_MAX_LBAS, wb), -1, "bad wr");
    P();
}

static void test_ecc(void) {
    int d, bit;
    T("Hamming encode/decode");
    for(d=0;d<16;d++){
        uint8_t enc=ecc_hamming_encode((uint8_t)d);
        uint8_t dec;
        ecc_hamming_decode(enc,&dec);
        assert(dec==(uint8_t)d);
    }
    P();
    T("Hamming 1-bit correction");
    for(d=0;d<16;d++){
        uint8_t enc=ecc_hamming_encode((uint8_t)d);
        for(bit=0;bit<7;bit++){
            uint8_t corr=ecc_hamming_introduce_error(enc,bit);
            uint8_t fix;
            ecc_hamming_decode(corr,&fix);
            assert(fix==(uint8_t)d);
        }
    }
    P();
}

static void test_ldpc(void) {
    LDPCCode code;
    int i;
    T("LDPC construction");
    ldpc_gallager_construct(&code, 24, 3, 6);
    OK(code.N>0 && code.M>0, "construct");
    P();
    T("LDPC min-sum decode");
    {
        uint8_t info[48]={0};
        uint8_t cw[96]={0};
        LDPCDecoder dec;
        memset(&dec,0,sizeof(dec));
        for(i=0;i<code.K;i++)info[i]=(uint8_t)(i%2);
        ldpc_encode(&code,info,cw);
        for(i=0;i<code.N;i++)dec.llr_in[i]=cw[i]?-10.0:10.0;
        ldpc_min_sum_decode(&dec,&code,0.75);
        OK(dec.iterations > 0, "ran");
    }
    P();
    T("Shannon limit");
    {
        double lim=ldpc_shannon_limit(0.5);
        OK(lim<1.0 && lim>-10.0,"limit");
    }
    P();
}

static void test_nand(void) {
    NANDCell cell;
    NANDDevice dev;
    T("NAND cell types");
    nand_cell_init(&cell,NAND_SLC);
    EQ(cell.num_levels,2,"SLC");
    nand_cell_init(&cell,NAND_MLC);
    EQ(cell.num_levels,4,"MLC");
    nand_cell_init(&cell,NAND_TLC);
    EQ(cell.num_levels,8,"TLC");
    P();
    T("NAND ISPP");
    nand_cell_init(&cell,NAND_MLC);
    {double v=nand_ispp_program(&cell,1,4);OK(v>-2.0,"vth");}
    P();
    T("NAND erase PE");
    nand_cell_init(&cell,NAND_MLC);
    nand_erase_cell(&cell);
    EQ(cell.pe_cycles,1,"PE");
    P();
    T("NAND RBER aging");
    nand_cell_init(&cell,NAND_MLC);
    nand_endurance_degrade(&cell,50000);
    {double r=nand_compute_rber(&cell);
     OK(r>=0.0,"rber nonnegative");}
    P();
    T("NAND retention");
    nand_cell_init(&cell,NAND_MLC);
    {double a=nand_retention_time(&cell,300.0);
     nand_endurance_degrade(&cell,100000);
     double b=nand_retention_time(&cell,300.0);
     OK(b<a,"ret");}
    P();
    T("NAND device init");
    nand_device_init(&dev,NAND_TLC,4,2);
    EQ(dev.num_dies,4,"dies");
    P();
}

static void test_endur(void) {
    EnduranceSpec spec;
    EnduranceTracker trk;
    T("Endurance spec");
    endurance_spec_init(&spec,1000.0,3.0,5.0,1.5);
    OK(spec.tbw>0.0,"tbw");
    P();
    T("Endurance tracker");
    endurance_tracker_init(&trk,&spec,7.0);
    endurance_record_write(&trk,1e12,1.5);
    OK(trk.pe_cycles_used>0.0,"pe");
    P();
    T("DWPD");
    {double d=endurance_compute_dwpd(3e12,1000.0);
     OK(d>2.9&&d<3.1,"dwpd");}
    P();
    T("Weibull CDF");
    {double c=weibull_cdf(8760.0,1.0,87600.0);
     OK(c>0.0&&c<1.0,"cdf");}
    P();
    T("AFR");
    {double a=endurance_afr(2e6);
     OK(a>0.0&&a<0.01,"afr");}
    P();
}

static void test_gc_module(void) {
    FTL ftl;
    GarbageCollector gc;
    uint8_t data[4096];
    int i;
    T("GC trigger");
    ftl_init(&ftl,FTL_MAPPING_PAGE_LEVEL);
    gc_init(&gc,&ftl,GC_GREEDY,GC_DEFAULT_OP_PCT,GC_DEFAULT_THRESHOLD);
    memset(data,0x55,4096);
    for(i=0;i<100;i++)ftl_write(&ftl,(uint32_t)i,data);
    (void)gc_trigger(&gc);
    P();
    T("WA formula");
    {double wa=gc_write_amplification_formula(0.9,7.0);
     OK(wa>1.0,"wa");}
    P();
}

int main(void) {
    printf("=== Integration Test ===\n\n");
    test_ftl();
    printf("\n");
    test_ecc();
    printf("\n");
    test_ldpc();
    printf("\n");
    test_nand();
    printf("\n");
    test_endur();
    printf("\n");
    test_gc_module();
    printf("\n=== %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
