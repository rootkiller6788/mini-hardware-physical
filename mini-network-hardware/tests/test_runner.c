#include "nic_arch.h"
#include "mac.h"
#include "rdma.h"
#include "offload.h"
#include "switch_fabric.h"
#include "pcie.h"
#include "serdes.h"
#include "flow.h"
#include "timestamp.h"
#include "qos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

static int passed = 0, failed = 0;

#define T(name, expr) do { \
    if (expr) passed++; else { failed++; printf("  FAIL: %s\n", name); } \
} while(0)

#define TEQ(name, a, b) do { \
    long long _a=(long long)(a),_b=(long long)(b); \
    if (_a==_b) passed++; else { failed++; printf("  FAIL: %s (%lld!=%lld)\n",name,_a,_b); } \
} while(0)

int main(void) {
    printf("=== mini-network-hardware: Test Suite ===\n\n");

    printf("[MAC Tests]\n");
    {
        uint8_t mac[6];
        T("parse valid", mac_addr_parse("aa:bb:cc:dd:ee:ff", mac) == 0);
        T("parse null", mac_addr_parse(NULL, mac) == -1);
        char buf[32]; mac_addr_to_str(mac, buf);
        T("round-trip", strcmp(buf, "aa:bb:cc:dd:ee:ff") == 0);
        uint8_t d[6]={0x11,0x22,0x33,0x44,0x55,0x66};
        uint8_t s[6]={0xaa,0xbb,0xcc,0xdd,0xee,0xff};
        MACFrame f; mac_frame_build(&f,d,s,0x0800,(uint8_t*)"test",4);
        T("fcs check", mac_frame_check(&f));
        T("crc32", mac_crc32((uint8_t*)"12345678",8)!=0);
        MACStats st; mac_stats_init(&st);
        mac_stats_record_tx(&st); T("stats tx", st.frames_tx==1);
    }

    printf("[NIC Tests]\n");
    {
        NIC nic; uint8_t m[6]={0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
        nic_init(&nic,m,0xC0A80101);
        T("nic mac", memcmp(nic.mac_addr,m,6)==0);
        char d[64]="test";
        T("tx enq", nic_tx_enqueue(&nic,d,10)==0);
        T("tx null", nic_tx_enqueue(&nic,NULL,10)==-1);
        nic.dma_engine_active=true; nic_process(&nic);
        T("dma bytes", nic.dma_total_bytes>0);
        nic_interrupt_handler(&nic,NIC_IRQ_TX_COMPLETE);
        T("irq clear", nic.interrupts[NIC_IRQ_TX_COMPLETE]==false);
    }

    printf("[RDMA Tests]\n");
    {
        RDMAContext ctx; rdma_init(&ctx);
        T("rdma pd", ctx.protection_domain==1);
        uint8_t b[1024]; memset(b,0xAB,1024);
        T("reg mr", rdma_reg_mr(&ctx,b,1024)==0);
        int qp=rdma_create_qp(&ctx,RDMA_QP_RESET);
        T("create qp", qp>=0);
        rdma_modify_qp(&ctx,qp,RDMA_QP_RTS);
        uint8_t dest[64]; memset(dest,0,64);
        const char *msg="RDMAtest";
        int w=rdma_remote_write(&ctx,qp,dest,msg,(int)strlen(msg)+1);
        T("rdma write", w>0);
        uint32_t r; T("poll cq", rdma_poll_cq(&ctx,&r)==0);
    }

    printf("[Offload Tests]\n");
    {
        OffloadEngine *e=offload_engine_create(OFFLOAD_CSUM);
        T("create", e!=NULL);
        uint8_t d[]={0xDE,0xAD,0xBE,0xEF};
        uint16_t cs=offload_csum_compute(d,4);
        T("csum ok", offload_checksum_verify(e,d,4,cs));
        uint8_t pkt[200]; for(int i=0;i<200;i++)pkt[i]=(uint8_t)i;
        uint8_t *segs=NULL; int ns;
        OffloadEngine *ts=offload_engine_create(OFFLOAD_TSO);
        T("tso seg", offload_tso_segment(ts,pkt,200,50,&segs,&ns)==0);
        T("tso 4", ns==4); free(segs);
        offload_engine_destroy(ts); offload_engine_destroy(e);
    }

    printf("[Switch Tests]\n");
    {
        SwitchFabric *sw=switch_init(4);
        T("init", sw!=NULL);
        switch_add_port(sw,0,10);
        T("port up", sw->ports[0].state==PORT_UP);
        uint8_t mac[6]={0,1,2,3,4,5};
        switch_learn_mac(sw,mac,0);
        T("learned", sw->ports[0].mac_table_size==1);
        uint8_t fr[64]; memset(fr,0xAA,64);
        T("forward", switch_forward(sw,mac,fr,64)==0);
        switch_destroy(sw);
    }

    printf("[PCIe Tests]\n");
    {
        PCIELink *l=pcie_link_init(3,4);
        T("init", l!=NULL);
        TEQ("gen", l->gen, PCIE_GEN3);
        T("bw", pcie_link_bandwidth_gbps(l)>0);
        PCIEConfig c; pcie_init_config(&c,0x8086,0x1000);
        TEQ("vid", c.vendor_id, 0x8086);
        TLPPacket t=pcie_tlp_create_read(0x1000,64);
        TEQ("tlp type", t.type, TLP_MEM_READ);
        pcie_link_destroy(l);
    }

    printf("[SerDes Tests]\n");
    {
        serdes_8b10b_init_tables();
        RunningDisparity rd=RD_NEGATIVE; uint16_t sym; uint8_t dec; bool ctrl;
        T("8b10b enc", serdes_8b10b_encode(0xBC,false,&rd,&sym)==0);
        T("8b10b dec", serdes_8b10b_decode(sym,&rd,&dec,&ctrl)==0);
        TEQ("8b10b rt", dec, 0xBC);
        T("valid sym", serdes_8b10b_is_valid_symbol(sym));
        uint64_t d=0xABCD1234DEADBEEFULL,enc,decd; SyncHeaderType h=SYNC_DATA, rh;
        serdes_64b66b_encode(d,h,&enc); serdes_64b66b_decode(enc,&decd,&rh);
        T("64b66b rt", decd==d);
        Scrambler scr; scrambler_init(&scr,(1ULL<<7)|(1ULL<<6)|1,7,0x5A);
        uint8_t tb[16]="test scrambler"; scrambler_process(&scr,tb,14);
        T("scrambled", memcmp(tb,"test scrambler",14)!=0);
        scrambler_reset(&scr); scrambler_process(&scr,tb,14);
        T("descrambled", memcmp(tb,"test scrambler",14)==0);
        int8_t so; int ns; pam4_encode_bits(2,&so,&ns);
        T("pam4", pam4_decode_to_bits(&so)==2);
        T("shannon", shannon_capacity(10e9,100.0)>0);
        SerDesConfig *sc=serdes_config_create(LINE_CODE_PAM4,1,50.0);
        T("cfg", sc!=NULL); serdes_config_destroy(sc);
    }

    printf("[Flow Tests]\n");
    {
        uint8_t src[6]={0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
        PauseFrame pf; pause_frame_build(&pf,src,500);
        TEQ("pause q", pf.pause_quanta, 500);
        uint8_t raw[64]; memset(raw,0,64);
        raw[0]=1;raw[1]=0x80;raw[2]=0xC2;raw[3]=0;raw[4]=0;raw[5]=1;
        memcpy(raw+6,src,6); raw[12]=0x88;raw[13]=0x08;raw[14]=0;raw[15]=1;
        raw[16]=0x01;raw[17]=0xF4;
        PauseFrame pr; T("parse", pause_frame_parse(raw,64,&pr)==0);
        PFCState pfc; pfc_init(&pfc);
        pfc_set_pause(&pfc,3,100); T("pfc set", pfc_is_paused(&pfc,3));
        pfc_clear_pause(&pfc,3); T("pfc clr", !pfc_is_paused(&pfc,3));
        TokenBucket tb; token_bucket_init(&tb,1000,5000);
        uint64_t now=1000000000ULL;
        T("tb ok", token_bucket_consume(&tb,100,now));
        T("tb overflow", !token_bucket_consume(&tb,10000,now));
        LeakyBucket lb; leaky_bucket_init(&lb,1000,5000);
        T("lb enq", leaky_bucket_enqueue(&lb,1000,now));
        CongestionControl cc; congestion_init(&cc);
        uint64_t act; congestion_check(&cc,0.3,&act); T("cc pass", act==0);
        congestion_check(&cc,0.6,&act); T("cc mark", act==1);
        T("littles law", fabs(littles_law_queue_length(1000,0.01)-10.0)<0.1);
    }

    printf("[Timestamp Tests]\n");
    {
        PTPTimestamp a,b; ptp_timestamp_set(&a,100,500000000UL);
        ptp_timestamp_set(&b,100,200000000UL);
        TEQ("diff", ptp_timestamp_diff_ns(&a,&b), 300000000LL);
        PTPClock clk; uint8_t cid[8]={0,1,2,3,4,5,6,7};
        ptp_clock_init(&clk,cid);
        ptp_clock_set_time(&clk,0,0); ptp_clock_step(&clk,1000000000ULL);
        TEQ("step", (long long)clk.current_time.seconds, 1);
        PTPDelayMeasurement dm; ptp_delay_measurement_init(&dm);
        ptp_timestamp_set(&dm.t1,0,0); ptp_timestamp_set(&dm.t2,0,100000000UL);
        ptp_timestamp_set(&dm.t3,0,200000000UL); ptp_timestamp_set(&dm.t4,0,280000000UL);
        dm.valid=true; double off,del;
        ptp_calculate_offset(&dm,&off,&del);
        T("ptp offset", fabs(off-10000000.0)<1000.0);
        TSNGateControl gate; tsn_gate_init(&gate,1000000ULL);
        T("tsn open", tsn_gate_is_open(&gate,0));
    }

    printf("[QoS Tests]\n");
    {
        QoSScheduler sch; qos_scheduler_init(&sch,QOS_SCHED_DRR);
        qos_scheduler_add_class(&sch,QOS_PCP_VO,10,1500);
        qos_scheduler_add_class(&sch,QOS_PCP_BE,1,1500);
        qos_class_add_dscp(&sch.classes[0],DSCP_EF);
        TEQ("classify ef", qos_scheduler_classify(&sch,DSCP_EF),0);
        TEQ("classify df", qos_scheduler_classify(&sch,DSCP_DF),sch.default_class);
        int ql[2]={100,0}; drr_init_counters(&sch);
        T("drr sched", drr_schedule_next(&sch,ql)>=0);
        DCBState dcb; dcb_init(&dcb); dcb_enable_pfc(&dcb,0xFF);
        T("dcb pfc", dcb.pfc_enabled);
        ETSClassConfig ets; ets_init_class(&ets,0,30,false);
        T("ets bw", fabs(ets_available_bw(&ets,10000.0)-3000.0)<1.0);
    }

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
