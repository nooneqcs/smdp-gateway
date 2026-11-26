
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
