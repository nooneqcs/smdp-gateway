
## 待补测：硬件时间戳（2026-11-26 立项）
- T1 单包 TX 戳 / T2 单包 RX 戳 / T3 单口往返 RTT（同一 PHC 做差，免 PTP）
- T4 PHC 漂移（粗测 +44 ppm）/ T5 PHC vs rdtsc 对账
- 口径：T3 是"含对端 echo 开销的往返 RTT"，不对外称"端到端延迟"
- 依赖：笔记本接线 + echo 脚本；已验证 enable=0、read_time 可用

## 2026-11-26 P2 MIRP 编码基准（定案）
- 正确性：roundtrip 15 用例 ALL PASS（含 INT64_MIN/MAX、VInt 127/128/16383/16384）
- 快速版与 vector 版逐字节一致（fast_test ALL PASS）
- vector 接口：180 ns/pkt，其中 140 ns 是 push_back 容器开销（1.82 ns/字节，占 78%）
- 快速版（caller-provided buffer）：body 45.3 ns/pkt、full 45.9 ns/pkt、22.1 Mpps、77 B、7 域
- 按字节：编码 0.59 ns/B vs MIRP 解码 0.64 ns/B（35.87/56B/4域），同量级
- 24B 头成本约 0.6 ns
- 口径：L1/L2 热、-O3 -march=native、governor=performance、单线程
- 可选优化：PutVInt 由逐字节循环改 8 字节单写，预计 45→20 ns（未做）

## 2026-11-26 P2 编码优化：四种实现对比（定案）
最终采用 fast1（逐字节 while 循环 + caller buffer）：
- body 45.3 ns / 22.1 Mpps / 77 B / 7 域 / 0.59 ns/字节
- full 45.9 ns（24B 头约 0.6 ns）
对比（同数据、governor=performance、3000 MHz）：
- vector push_back 接口 180 ns（140 ns 为容器开销）
- 无分支 8 字节整写 266 ns
- 两级分支 2/3 字节精确写 198 ns
- 单分支 4 字节整写 229 ns
结论：宽存储重叠 + 不可预测长度分支是主要惩罚；最朴素的字节循环最快（4~6×）。
按字节：编码 0.59 ns/B vs MIRP 解码 0.64 ns/B（35.87/56B/4域），同量级。
正确性：15 用例 byte-identical（含 INT64_MIN/MAX、VInt 127/128/16383/16384）。

## 2026-11-26 P2 编码优化终结：6 种实现 + 机制结论
最终采用 fast1（逐字节 while 循环 + caller buffer）：
- 编码器本体（L1 热，64 tick）：33.9 ns/pkt，29.5 Mpps
- 端到端（4096 tick 真实多变）：45.3 ns/pkt，22.1 Mpps（对外口径）
- 理想下限（同一 tick）：28.5 ns
六种实现 body 耗时：push_back 180 / fast1 45.3 / fast2(8B无分支) 266 /
fast3(两级精确) 198 / fast4(单分支4B) 229 / fast5(无分支直写) 137
机制：fast1 的 p++ 与数据无关，分支预测允许投机执行，19 个 VInt 在 OoO 中重叠；
无分支版 p += 1+d1+d2 依赖数据，形成 19 级串行依赖链（19×~220cyc≈73ns，与实测吻合）。
结论：本机（Kaby Lake 4-wide）上 branch 是并行度来源，去掉分支反而慢 3–6 倍。
附加结论：结构体 176B→64B 无收益（44.7 vs 45.3），排除取数带宽假设。

## 2026-11-26 P2 编码：物理极限定位（定案）
- 标定：定长 32 位编码 12.57 ns / 122 B / 79.6 Mpps = 38 cycles / 31 stores
  => 本机 store 吞吐 = 1.2 cycles/store（Kaby Lake 单 store 端口），据此标定一切
- fast1（VInt）：L1 热 33.9 ns，约 70 次 store，理论下限 23-26 ns，达成 70-75%
- 差额分解：33.9 − 12.6 = 21.3 ns = 39 次额外 store(~13ns) + 19 次 zigzag(~8ns)
- 结论：编码瓶颈是 store 次数（1/cycle），非算力；VInt 每字节一次 store 与高速编码结构性冲突
- 三口径：VInt 真实 45.3 / VInt L1 33.9 / 定长 12.6（12.6 需放弃 MIRP 兼容，仅自有格式可用）
- 教训（DCE 三连）：1) 无消费点被删 2) asm 无 buf 操作数仍被删 3) 填充循环被 GCC 向量化
  铁律：先把 store 次数算出来，任何低于 store 下限（cycles = store 次数）的编码数字一律是假的

## ADR-001 编码器采用 VInt（MIRP），不采用定长
状态：已定
背景：编码器用途是生成 MIRP 报文供解码器/DPDK 链路验证，属测试工装；
      生产分发不走编码，而是解码后直接传内部结构体。
决策：编码器保持 VInt（fast1 实现），不改为定长。
理由：
 1) 输出格式必须与被测解码器一致，否则 round-trip 自证无意义、MIRP 兼容性丢失；
 2) 性能上无必要：45.3 ns = 22.1 Mpps，是真实行情峰值的 20 倍以上；
 3) 定长 12.6 ns / struct-on-wire 1.86 ns 的数据已记录，作为架构选型依据备查。
后续：若做下游分发层（Q2），采用 64B 内部结构（SimTickC 草案）直传，不经过编码。

## 2026-11-26 P2 真实行情回放编码（SimNow 908 条，4 合约）
数据源：SimNow CTP 行情落盘 ticks_simnow.bin（au2702/ag2702/cu2701/rb2701，120s）
校验：ts_backwards=0、zeroBidVol=0、roundtrip ALL PASS
三版演进（body，4 域：0x0003 + 0x1001×2 + 0x1002）：
- v1 绝对价位×10000：48.7 B / 33.4 ns / 30.0 Mpps
- v2 基准价偏移（CodecPrice 本意）：43.5 B / 29.3 ns / 34.1 Mpps
- v3 语义修正：Turnover 不×10000、OpenInterest 传变化量：39.9 B / 27.0 ns / 37.0 Mpps
结论：
1) 合成数据会低估 VInt 长度（造的数据 offset 仅 2B，真实价位 8 位→4B），
   基准必须用真实分布，否则口径失真；
2) 字段语义（Change vs 绝对值）直接影响报文长度与 store 次数，属协议理解而非优化技巧。
对比合成 7 域 77B：45.3 ns。真实 4 域 39.9B：27.0 ns（域数少、字节少、但每字节略贵）。

## 2026-11-26 P3 帧封装与发送路径（软件级验证完成）
- wire_frame.h：BuildUdpFrame（Ethernet14 + IPv4 + UDP，可选 VLAN tag）+ ParseUdpFrame
- wire_test：真实行情 908 条，帧往返 ALL PASS、VLAN ALL PASS、截断被拒、
  接收端（解帧→遍历域→解码）head 校验 OK；开销 42 B
- wire_tx：DPDK 发送路径，用 --vdev=net_pcap0 验证，sent=18160 failed=0，
  pcap 1.78MB；帧头 01:00:5e:00:00:01、192.168.99.1:40000 -> 239.0.0.1:1001，
  UDP 载荷 34-36 B 随 VInt 长度变化
- 下一步（需网卡）：换 -a 0000:01:00.0 真发，收端 rx_burst → 解帧 → 解码

## 2026-11-26 CTP 官方 API vs 自写解析：成本拆解
测量方法：perf stat 全进程 + rdtsc 打点回调
1) 官方 API 常驻开销：2.60G cycles/60s（0 tick 空载）≈ 43M cycles/s ≈ 单核 1.4%；
   context-switches 108k/60s ≈ 1800/s；user 0.09s vs sys 1.60s（内核网络栈为主）
   —— 收到 353 ticks 时 cycles 与空载差异在 ±1% 噪声内，per-tick 增量不可测
2) 回调内处理成本（我们自己的代码）：
   - 含 sscanf("%d:%d:%d")：avg 2941 / min 681 / max 18788 cycles
   - 改手写 ASCII 解析后：avg 157.2 / min 37 / max 549 cycles（≈52 ns）
   => sscanf 单项占 19 倍，且是 max 异常的主要来源
3) 对比自写 MIRP：解码 108 cycles/pkt、编码 27 ns/pkt（真实行情）
结论：CTP 官方 API 的每 tick 处理成本（157 cycles）与自写解析同量级，
      "官方 API 慢" 在 per-tick 不成立；成本在常驻线程唤醒与内核交互，
      低延迟优化的重点是绕开内核/减少唤醒/避免慢库函数，而非重写解析。
