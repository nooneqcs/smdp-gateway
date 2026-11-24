# SMDP 解码性能结果

## 环境
- CPU: Intel Core i5-7400 @ 3.00GHz（no_turbo，单线程）
- 编译: g++ -std=c++17（通用 -O2 -flto；本机 -O3 -march=native -flto）
- 数据在 L1/L2，同进程反复解析，无网络/系统调用

## 口径

### A. 理想报文
- 47B = 24B MIRP 头 + 23B 体，4 域（0x0003+0x1001+0x1002），全 1 字节 VInt，单合约
- frame only：头 + 域遍历，不解
- full：+ 四域解码 + book 更新

### B. 真实感负载（模拟分布，非真实抓包）
- 175B/包、4 合约、~14 域
- VInt 混合：PriceOffset ±100tick(1-2B)、成交量 0-5000(1-2B)、成交额 0-1e7(2-4B)
- 由 MIRP encoder 按协议语义构造（真实 SimNow 数值未接入）

## 结果

### 理想（-O3 -march=native）
| 口径 | ns/pkt | Mpps |
|---|---|---|
| frame only | 5.05 | 198 |
| full + book | 35.87 | 27.9 |

（通用 -O2 -flto：full 40.3 / 24.8）

### 真实感（-O3 -march=native）
| ns/pkt | Mpps | 规模 |
|---|---|---|
| 255.85 | 3.9 | 175B / 14 域 / 4 合约 |

## 优化链

### 理想
| 版本 | full ns | 手段 |
|---|---|---|
| 基线 | 114.98 | 每域查 unordered_map |
| v2 | 85.24 | 缓存 ContractState* |
| v3 | 57.60 | VInt inline + 模板遍历去虚 |
| v4 | 40.3 | -flto |
| v5 | 38.2 | -O3 -march=native + final |
| v6 | 35.87 | 0x1002 SIMD zigzag |

### 真实感
| 版本 | ns | 手段 |
|---|---|---|
| 基线 | 314.06 | unordered_map |
| v2 | 274.5 | map → vector 直接索引 |
| v3 | 255.85 | switch → 频率序 if-else |

## 试过但否决
| 尝试 | 效果 |
|---|---|
| zigzag 查表 | 理想 38→46（加 load 拖累） |
| 2 字节分层 | 真实 275→304 |
| pext 无分支 | 真实 275→309 |
| book 去分支 | ±1% 噪声 |
| 0x1001 SSE | 理想 35.9→50（3 个跑不满 4-lane） |
| 0x1001/0x0003 混合批量 | 理想 37→48 |

## 关键结论
- 理想 IPC 3.55（吞吐受限）；真实 IPC 2.29。
- branch-miss 2% 是现象、被乱序吸收；减分支手段（pext/分层/查表）多亏。
- 域分派 switch→if 赚，是因跳转表间接跳转开销，非 miss 率（优化前后 miss 率均 2.07%）。
- 指标高 ≠ 该降它：要区分“短循环可预测分支”（VInt，别动）与“间接跳转”（switch，可换）。

## 未做 / 边界
- 非端到端（不含收包/组播/分发）。
- 真实 MIRP 报文分布未知，B 为语义构造；整包 SIMD 未做（变长报文难向量化，投入产出低）。

