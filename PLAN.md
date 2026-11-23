# 计划

目标：完成 SMDP2.0 的 MIRP 增量 + MDQP 快照解码与合并，离线可验证、有性能数据、可复现。

| 阶段 | 内容 | 人天 | 产出 |
|---|---|---|---|
| P0 | 资料收集、协议要点整理 | 1 | docs/SOURCES.md, docs/PROTOCOL.md |
| P1 | 24B头 + 域遍历 + VInt + 0x0003/0x1001 + 40B 单测 | 5-7 | mirp.h, vint.*, decode.*, tests |
| P2 | 增量域 0x1002、0x1011~0x1018 | 2-3 | decode 扩展 + 单测 |
| P3 | MDQP 快照域 0x0101/0x0102（定长） | 2 | decode |
| P4 | book 快照合并（事件序列/超深度/切换） | 4-6 | book.* + 单测 |
| P5 | 边界/鲁棒测试（多字节 VInt、超长域、切换） | 2-3 | tests/boundary |
| P6 | 解码性能（ns/条、Mpps，写口径） | 2-3 | benchmark + docs/RESULTS-SMDP.md |
| P7 | 接 DPDK 回放端到端（可选） | 5-8 | 整链延迟 |
| P8 | CTP adapter 真实流对照（可选） | 5-7 | 框架验证 |

## 不做
- 与官方 libftdmdapi.so 性能对比（个人无前置/账号）

## 里程碑
- Q1：P1-P6 跑通，产出 docs/RESULTS-SMDP.md
- Q2：P7 完整网关 demo
