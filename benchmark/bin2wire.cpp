// bin(96B realdata::RecTick) → 域序列 → 前置 24B MIRP头 → BuildUdpFrame → AF_PACKET
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "wire_frame.h"
#include "mrp_encode_real2.h"
#include "mirp.h"

int main(int argc, char** argv) {
    const char* bin = argc>1 ? argv[1] : "ticks_simnow.bin";
    const char* dev = "enp2s0";
    const char* pcapOut = nullptr;
    uint8_t dmac[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    uint8_t dip[4]  = {239,0,0,1};
    double rate = 0; long limit = 0; int repeat = 1;

    for (int i=2;i<argc;i++) {
        if      (!strncmp(argv[i],"--dev=",6))   dev = argv[i]+6;
        else if (!strncmp(argv[i],"--pcap=",7))  pcapOut = argv[i]+7;
        else if (!strncmp(argv[i],"--rate=",7))  rate = atof(argv[i]+7);
        else if (!strncmp(argv[i],"--limit=",8)) limit = atol(argv[i]+8);
        else if (!strncmp(argv[i],"--repeat=",9))repeat = atoi(argv[i]+9);
        else if (!strncmp(argv[i],"--dmac=",7))
            sscanf(argv[i]+7,"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &dmac[0],&dmac[1],&dmac[2],&dmac[3],&dmac[4],&dmac[5]);
    }

    std::vector<realdata::RecTick> raw;
    if (!realdata::Load(bin, raw)) { perror(bin); return 1; }
    std::vector<realdata::RealRec> recs;
    realdata::Build(raw, recs);
    printf("loaded=%zu bytes/rec=%zu hdr=%zu\n",
           recs.size(), sizeof(realdata::RecTick), sizeof(smdp::MirpHeader));

    uint8_t smac[6] = {0,0,0,0,0,0};
    {   char p[128]; snprintf(p,sizeof p,"/sys/class/net/%s/address",dev);
        FILE* f=fopen(p,"r"); if(!f){perror(p);return 1;}
        unsigned a,b,c,d,e,g;
        if (fscanf(f,"%x:%x:%x:%x:%x:%x",&a,&b,&c,&d,&e,&g) != 6) { fclose(f); return 1; }
        fclose(f);
        smac[0]=a;smac[1]=b;smac[2]=c;smac[3]=d;smac[4]=e;smac[5]=g;
    }
    static const uint8_t SIP[4] = {10,0,0,12};

    FILE* pf = nullptr; int fd = -1; sockaddr_ll sll{};
    if (pcapOut) {
        pf = fopen(pcapOut,"wb");
        struct H { uint32_t m; uint16_t a,b; int32_t tz; uint32_t s,snap,l; } h{};
        h.m=0xa1b2c3d4; h.a=2; h.b=4; h.snap=65535; h.l=1; fwrite(&h,sizeof h,1,pf);
    } else {
        fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (fd<0) { perror("AF_PACKET(root?)"); return 1; }
        unsigned idx = if_nametoindex(dev);
        if (!idx) { perror("if_nametoindex"); return 1; }
        sll.sll_family=AF_PACKET; sll.sll_ifindex=(int)idx; sll.sll_halen=6;
        memcpy(sll.sll_addr, dmac, 6);
    }

    uint64_t interval = rate>0 ? (uint64_t)(1e9/rate) : 0;
    timespec next{}; clock_gettime(CLOCK_MONOTONIC,&next);
    uint8_t body[sizeof(smdp::MirpHeader) + 512], frame[1024];
    int32_t pktno = 0;
    long sent=0, failed=0;

    for (int rep=0; rep<repeat; ++rep) {
        long c=0;
        for (auto& r : recs) {
            if (limit && c++>=limit) break;

            // 域序列直接写进 body 头之后
            size_t dlen = realdata::EncodeReal(body + sizeof(smdp::MirpHeader), r);
            if (!dlen) { ++failed; continue; }

            smdp::MirpHeader mh{};
            mh.Flag      = 0x01;
            mh.TypeID    = 0x01;
            mh.Length    = (uint16_t)dlen;
            mh.PacketNo  = ++pktno;
            mh.TopicID   = 1001;
            std::memcpy(body, &mh, sizeof mh);

            size_t blen = sizeof(smdp::MirpHeader) + dlen;
            size_t flen = wire::BuildUdpFrame(frame, body, blen, dmac, smac,
                                              SIP, dip, 40000, 1001, 0);
            if (!flen) { ++failed; continue; }

            if (pf) {
                struct P{uint32_t s,us,i,o;} ph{};
                timespec ts{}; clock_gettime(CLOCK_REALTIME,&ts);
                ph.s=(uint32_t)ts.tv_sec; ph.us=(uint32_t)(ts.tv_nsec/1000);
                ph.i=ph.o=(uint32_t)flen;
                fwrite(&ph,sizeof ph,1,pf); fwrite(frame,1,flen,pf);
            } else if (sendto(fd, frame, flen, 0, (sockaddr*)&sll, sizeof sll) < 0) { ++failed; continue; }
            ++sent;

            if (interval) {
                next.tv_nsec += (long)interval;
                while (next.tv_nsec>=1000000000){next.tv_nsec-=1000000000;++next.tv_sec;}
                clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&next,nullptr);
            }
        }
    }
    printf("sent=%ld failed=%ld\n", sent, failed);
    if (pf) fclose(pf);
    if (fd>=0) close(fd);
    return failed ? 1 : 0;
}
