# smdp-gateway

上期所 SMDP2.0 风格的行情网关 + 交易引擎（学习/演示项目）：从裸字节解码行情，到策略下单、撮合、成交、持仓/PNL 的完整链路，全部可在**单机、无外网、无交易所账号**下复现。

> 交易所真撮合、真组播、真报单通道需要会员/托管/官方 API，本仓库用 **net_pcap 注入**和**mock 撮合**在本地复现其"软件栈"；真实接入选型见文末 Roadmap。

## 覆盖范围

- **行情**：MIRP 报文解码（VInt + 域分派）、UDP 帧封解、DPDK/net_pcap 收包、跨核 SPSC 分发
- **交易**：二进制报单协议、预交易风控、mock 撮合（价时优先）、持仓/已实现/未实现 PNL
- **基准**：编码/解码、net_pcap 自洽闭环、tick→成交延迟（p50/p90/p99）

## 架构

~~
[行情]  net_pcap/DPDK 收 -> ParseUdpFrame -> IterateDomainsT -> Decode* -> SPSC
                                                                         |
[交易]  策略 -> 风控 -> 报单(二进制帧) -> mock 撮合 -> 成交 -> 持仓/PNL -> 延迟统计
~~

## 目录

- `src/`：核心库（`mirp` 域解析、`decode`/`delta` 解码、`encode` 编码、`book` 订单簿、`vint` 变长整数、`market` 访问者）
- `tests/`：8 个单测（`mirp_test`/`encode_test`/`delta_test`/`boundary_test`/`market_test`/`book_test`/`snapshot_test`）
- `benchmark/`：`wire_frame.h` 帧、`wire_tx` 注入、`pcap_rx_verify2` 解码闭环、`trade_core` 引擎核心、`demo_trade3/4` 行情->成交、`gen_ticks.py` 合成数据、`roundtrip_test`/`wire_test` 正确性
- `docs/`：`RESULTS-SMDP.md`、`RESULTS-VInt.md`、`bench_notes.md`

## 快速开始

**前置**：g++ / make / pkg-config / DPDK(libdpdk)；DPDK 需大页：
~~bash
sudo bash -c 'echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages'
~~

**三条命令看全部成果（都不需要网卡、不需要 SimNow）：**
~~bash
make demo-pcap   # 行情闭环：18160 帧全解出，合约头全一致
make trade-core  # 交易引擎核心：199800 单，realized=-999000(价差成本)
make demo-trade  # 行情->策略->撮合->成交->持仓；tick->成交 p50 177 / p90 200 / p99 216 ns
~~

## 可复现结果（i5-7400 3.0GHz，L2 热/批量摊薄，已扣 rdtsc 25.9ns）

| 项 | 结果 | 口径 |
|---|---|---|
| 行情闭环 | 18160 帧全解出，4 域/帧，`head_mismatch=0` | net_pcap 注入回放 |
| 交易引擎核心 | orders=199800 fills=199800 rejects=200，realized=-999000 | 纯软件，无行情/DPDK |
| **tick->成交** | **p50 177 / p90 200 / p99 216 ns** | mock 撮合；net_pcap + 限速 5000/s + SCHED_FIFO；含 1 次跨核 SPSC |
| MIRP 纯解码 | 30.24 ns/包（4 域/38.8B，7.56 ns/域） | 内存内、批量 |
| MIRP 编码 | 27.0 ns/包（4 域/39.9B） | 内存内、批量 |

## 延迟口径（重要）

- **tick->成交**：T1=解码完成(tick 入 ring)、T2=成交落账；含跨核 SPSC；**不含** NIC PHY、不含公网。
- **必须限速回放**：不限速洪水时 p99 会到几十 µs（ring 排队假象）；限速+SCHED_FIFO 后才反映真实尾部（~200ns）。
- **报 p50/p90/p99，不报 max**：max 有偶发 ~100–160 µs 离群（未 per-core 隔离的调度抖动）。
- 分段不可加：不同段/条件不可相减/相加，只信同 harness 相对差。

## 合规

- 交易接入只走**官方 CTP API / SimNow 模拟**；**不逆向**交易所/柜台私有协议；仓库不含账户/成交明细等敏感信息。
- 交易所真撮合/真组播需会员资格与托管，本仓库以本地 mock/net_pcap 复现其软件栈。

## Roadmap

- [ ] M3：接入真实 CTP 通道（SimNow）验证报单->回报->对账
- [ ] 真 i210/环回 的 DPDK 实收链路
- [ ] 可选：`isolcpus`/`nohz_full` 压 p99 尾部
