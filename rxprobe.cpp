#include <cstdio>
#include <cstdint>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include "mirp.h"
using namespace smdp;
int main(int c,char**v){
  setvbuf(stdout,NULL,_IONBF,0);
  rte_eal_init(c,v);
  auto mp=rte_pktmbuf_pool_create("p",8192,256,0,RTE_MBUF_DEFAULT_BUF_SIZE,0);
  if(!mp){puts("mp");return 1;}
  rte_eth_conf cf{};
  if(rte_eth_dev_configure(0,1,1,&cf)<0){puts("cfg");return 1;}
  if(rte_eth_rx_queue_setup(0,0,1024,0,0,mp)<0){puts("rxq");return 1;}
  if(rte_eth_dev_start(0)<0){puts("start");return 1;}
  rte_eth_promiscuous_enable(0);
  puts("ready");
  unsigned long long tot=0,ok=0,bad=0,last=0;
  for(;;){
    rte_mbuf* mb[32];
    uint16_t nb=rte_eth_rx_burst(0,0,mb,32);
    tot+=nb;
    for(uint16_t i=0;i<nb;i++){
      uint8_t* d=rte_pktmbuf_mtod(mb[i],uint8_t*);
      uint16_t len=mb[i]->data_len;
      if(len>42){
        const uint8_t* m=d+42; uint16_t ml=len-42;
        MirpHeader h; int r=ParseMirpHeader(m,ml,h);
        if(r==0&&h.TypeID==1&&(h.Flag&15)==1&&h.TopicID>=1000&&h.TopicID<=9999&&(uint32_t)h.Length+24==ml) ok++;
        else { bad++; if(bad<=3) printf("bad len=%u ml=%u r=%d T=%d F=%02x Topic=%d L=%u\n",len,ml,r,(int)h.TypeID,h.Flag,(int)h.TopicID,(unsigned)h.Length); }
      }
      rte_pktmbuf_free(mb[i]);
    }
    if(tot/16!=last){ last=tot/16; printf("收=%llu 合法=%llu 坏=%llu\n",tot,ok,bad); }
  }
}
