# SMDP 行情网关 —— 结果

> 唯一对外口径来源。所有数字同机同口径；不同段不可相加/相减。

## 环境
i5-7400 4C4T 3.0GHz（no_turbo）、Ubuntu、DPDK 25.11；governor=performance；计时 rdtsc，25.9 ns 开销已扣。

## 一、行情解码（MIRP，内存内/批量，L2 热）
benchmark/parse_sw_only2.cpp：
- 帧解析(eth/ip/udp)：5.70 ns/包
- 域遍历(noop)：7.02 ns/包（1.76 ns/域 ×4）
- 域遍历+Decode*：37.26 ns/包（9.315 ns/域，0.959 ns/byte，4 域/38.8B）
- 纯 decode（遍历+解码−遍历）：30.24 ns/包（7.56 ns/域）

参照（不同工作量，勿混比）：定长 20 字段/80B 的 parse20 净 8.89 ns（绝对 10.57）。

## 二、编码（ticks->MIRP，真实 4 域）
- 27.0 ns/包，39.9 B（≈37.0 Mpps）；合成 7 域版 45.3 ns（数据量不同，勿与 4 域比）
- 编码/解码对称，均几十 ns 量级。

## 三、net_pcap 自洽闭环（可复现，无需网卡）
`make demo-pcap`：合成 908 ticks -> wire_tx 编码注入 pcap -> DPDK net_pcap 收 -> ParseUdpFrame -> IterateDomainsT -> Decode
- 18160 帧全部解出（0 失败），4 域/帧（72640 域），合约头 InstrumentNo/ChangeNo 与源全帧一致（head_mismatch=0）

## 四、交易引擎核心（纯软件）
`make trade-core`：报单帧 + 风控(手数) + mock 撮合(价时优先) + 持仓/PNL（无行情/DPDK）
- orders=199800 fills=199800 rejects=200（每千笔超大单被风控拒）
- 各合约净持仓 0，合计 realized=-999000（买 ask/卖 bid 的价差成本，负值符合预期），unrealized=0

## 五、tick->成交 延迟（本地 mock 闭环）
`make demo-trade`（限速 5000 tick/s + SCHED_FIFO，绑核 2/3）

| run | p50 | p90 | p99 | max |
|---|---|---|---|---|
| 1 | 177 ns | 200 ns | 216 ns | 127815 ns |
| 2 | 165 | 187 | 204 | 112130 |
| 3 | 161 | 183 | 568 | 140207 |
| 4 | 179 | 197 | 214 | 157591 |

定稿：**p50 ≈ 170–180 ns，p90 ≈ 190–200 ns，p99 ≈ 200–600 ns**
- 口径：T1=解码完成(tick 入 ring)，T2=成交落账；mock 撮合；含 1 次跨核 SPSC；不含 NIC PHY/公网。
- 不限速洪水回放时 p99 可达几十 µs（ring 排队假象，已用限速排除）。
- max 有偶发 ~100–160 µs 离群（未 per-core 隔离的调度抖动），报 p50/p99，不报 max。

## 六、口径纪律
- 分段不可加/减；只信同 harness、同条件的相对差。
- 每个数带口径：测什么、含不含、什么硬件/负载。
- 延迟报 p50/p90/p99，max 仅参考。
- CTP 内部反序列化不可测，只给上界（见 bench_notes）。

## 七、复现
~~bash
sudo bash -c 'echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages'
make demo-pcap && make trade-core && make demo-trade
~~

## 八、作废 / 踩坑
- 帧解析一度测出 0.03 ns：acc 未消费被 DCE，已修（现 5.70）。
- 洪水回放 p99 几十 µs：排队假象，限速后 ~200 ns，以此为准。
