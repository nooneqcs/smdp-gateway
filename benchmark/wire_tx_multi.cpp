#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include "wire_frame.h"
#include "mrp_encode_real.h"

static void put_mirp_header(uint8_t* h, uint16_t length, int32_t packetNo, int16_t topic) {
    memset(h, 0, 24);
    h[0] = 0x01;                                    // Flag, 低4位=1
    h[1] = 0x01;                                    // TypeID=0x01
    h[2] = (uint8_t)(length & 0xFF);
    h[3] = (uint8_t)((length >> 8) & 0xFF);
    memcpy(h + 4, &packetNo, 4);
    memcpy(h + 8, &topic, 2);
}

int main(int argc, char** argv) {
    if (rte_eal_init(argc, argv) < 0) { puts("EAL init failed"); return 1; }
    uint16_t port = 0;
    struct rte_eth_dev_info info;
    if (rte_eth_dev_info_get(port, &info) != 0) { puts("no port 0"); return 1; }
    printf("port0 driver = %s\n", info.driver_name);
    struct rte_eth_conf conf; memset(&conf, 0, sizeof conf);
    if (rte_eth_dev_configure(port, 0, 1, &conf) < 0) { puts("configure failed"); return 1; }
    struct rte_mempool* mp = rte_pktmbuf_pool_create("TX", 8191, 0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mp) { puts("mempool failed"); return 1; }
    if (rte_eth_tx_queue_setup(port, 0, 1024, rte_eth_dev_socket_id(port), NULL) < 0) { puts("txq failed"); return 1; }
    if (rte_eth_dev_start(port) < 0) { puts("start failed"); return 1; }

    std::vector<realdata::RecTick> raw;
    if (!realdata::Load("ticks_simnow.bin", raw)) { perror("ticks"); return 1; }
    std::vector<realdata::RealRec> recs;
    realdata::Build(raw, recs);

    static const uint8_t DST[6] = {0x01,0x00,0x5e,0x00,0x00,0x01};
    static const uint8_t SRC[6] = {0x02,0x00,0x00,0x00,0x00,0x01};
    static const uint8_t SIP[4] = {192,168,99,1};
    static const uint8_t DIP[4] = {239,0,0,1};

    const int M = 4;                               // 每帧合约数
    const int REPEAT = 20;
    uint8_t payload[2048], frame[2600];
    int32_t pktNo = 1;
    long sent = 0, failed = 0;

    for (int rep = 0; rep < REPEAT; ++rep) {
        for (size_t i = 0; i < recs.size(); i += M) {
            size_t cnt = std::min((size_t)M, recs.size() - i);
            size_t dlen = 0;
            for (size_t j = 0; j < cnt; ++j)                       // header 之后逐合约拼域串
                dlen += realdata::EncodeReal(payload + 24 + dlen, recs[i + j]);
            put_mirp_header(payload, (uint16_t)dlen, pktNo++, 1001); // Length=域串总长
            size_t total = 24 + dlen;
            size_t flen = wire::BuildUdpFrame(frame, payload, total, DST, SRC, SIP, DIP, 40000, 1001, 0);

            struct rte_mbuf* m = rte_pktmbuf_alloc(mp);
            if (!m) { ++failed; continue; }
            rte_pktmbuf_append(m, flen);
            memcpy(rte_pktmbuf_mtod(m, uint8_t*), frame, flen);
            uint16_t n = rte_eth_tx_burst(port, 0, &m, 1);
            if (n != 1) { rte_pktmbuf_free(m); ++failed; } else ++sent;
        }
    }
    printf("loaded=%zu sent=%ld failed=%ld  M=%d\n", recs.size(), sent, failed, M);
    rte_eth_dev_stop(port); rte_eth_dev_close(port);
    return failed ? 1 : 0;
}

