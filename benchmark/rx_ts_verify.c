// rx_ts_verify.c —— 手动注册 DPDK 动态时间戳字段，验证 i210 RX 硬件戳
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mbuf_dyn.h>
#include <rte_mempool.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define PORT 0
#define NRXD 256

int main(int argc, char** argv){
    if (rte_eal_init(argc, argv) < 0) return 1;
    if (rte_eth_dev_count_avail() < 1) { printf("no eth port\n"); return 1; }

    struct rte_eth_dev_info di;
    int rc = rte_eth_dev_info_get(PORT, &di); (void)rc;
    int ts_capa = (di.rx_offload_capa & RTE_ETH_RX_OFFLOAD_TIMESTAMP) != 0;
    printf("rx_offload_capa=0x%llx  TIMESTAMP_support=%d\n",
           (unsigned long long)di.rx_offload_capa, ts_capa);

    /* 手动注册动态字段 + flag（等价于便捷函数） */
    struct rte_mbuf_dynfield dynf;
    __builtin_memset(&dynf, 0, sizeof dynf);
    snprintf(dynf.name, sizeof dynf.name, "%s", RTE_MBUF_DYNFIELD_TIMESTAMP_NAME);
    dynf.size  = sizeof(uint64_t);
    dynf.align = __alignof__(uint64_t);
    int ts_off = rte_mbuf_dynfield_register(&dynf);

    struct rte_mbuf_dynflag dynflg;
    __builtin_memset(&dynflg, 0, sizeof dynflg);
    snprintf(dynflg.name, sizeof dynflg.name, "%s", RTE_MBUF_DYNFLAG_RX_TIMESTAMP_NAME);
    int ts_bit = rte_mbuf_dynflag_register(&dynflg);
    uint64_t ts_flag = (ts_bit >= 0) ? (1ULL << ts_bit) : 0;
    printf("dyn ts: offset=%d bit=%d flag=0x%llx\n", ts_off, ts_bit, (unsigned long long)ts_flag);

    struct rte_eth_conf conf; __builtin_memset(&conf,0,sizeof conf);
    if (ts_capa) conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_TIMESTAMP;

    if (rte_eth_dev_configure(PORT, 1, 0, &conf) < 0){ printf("configure fail\n"); return 1; }

    struct rte_mempool* mp = rte_pktmbuf_pool_create("mp", 8192, 0, 0, 2048, rte_socket_id());
    if (!mp){ printf("mempool fail\n"); return 1; }
    struct rte_eth_rxconf rxq; __builtin_memset(&rxq,0,sizeof rxq);
    if (rte_eth_rx_queue_setup(PORT,0,NRXD,rte_socket_id(),&rxq,mp) < 0){ printf("rxq fail\n"); return 1; }
    if (rte_eth_dev_start(PORT) < 0){ printf("start fail\n"); return 1; }

    struct rte_eth_link lk; __builtin_memset(&lk,0,sizeof lk);
    if (rte_eth_link_get_nowait(PORT,&lk)==0)
        printf("link: up=%d speed=%u duplex=%d\n", lk.link_status, lk.link_speed, lk.link_duplex);

    printf("timesync_enable=%d\n", rte_eth_timesync_enable(PORT));
    struct timespec ts; __builtin_memset(&ts,0,sizeof ts);
    printf("read_time=%d PHC=%ld.%09ld\n", rte_eth_timesync_read_time(PORT,&ts), ts.tv_sec, ts.tv_nsec);

    struct rte_mbuf* m[32];
    long got=0, flagged=0; uint64_t last=0;
    for (long it=0; it<20000000 && got<200; ++it){
        uint16_t n = rte_eth_rx_burst(PORT,0,m,32);
        for (uint16_t j=0;j<n;++j){
            ++got;
            int has = (ts_off>=0 && ts_flag && ((m[j]->ol_flags & ts_flag)!=0));
            uint64_t t = has ? *RTE_MBUF_DYNFIELD(m[j], ts_off, uint64_t*) : 0;
            if (has) ++flagged;
            if (got<=10 || has)
                printf("pkt#%ld flag=%d ts=%llu%s\n", got, has, (unsigned long long)t,
                       (has && last && t>last) ? " (inc)" : "");
            if (has) last = t;
            rte_pktmbuf_free(m[j]);
        }
    }
    printf("\nRESULT: got=%ld flagged=%ld -> %s\n", got, flagged,
           (got==0) ? "NO PACKETS (插网线/发送端)" :
           (flagged==0) ? "HW ts NOT set (走打点退路)" : "HW ts OK");
    return 0;
}
