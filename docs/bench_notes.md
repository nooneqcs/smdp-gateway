
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
