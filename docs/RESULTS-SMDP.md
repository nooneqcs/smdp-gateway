# SMDP 解码性能结果

## 环境
- CPU: Intel(R) Core(TM) i5-7400 @ 3.00GHz（单线程）
- 编译器: g++ -O2 -flto -std=c++17
- 数据: 同进程反复解析同一报文，数据在 L1/L2，无网络

## 口径
- 报文 56B = 24B MIRP 头 + 32B 体
- 体: 0x0003(合约头) + 0x1001×2(分价表变动) + 0x1002(成交概要)，共 4 域
- frame only: ParseMirpHeader + 域遍历，不解域体
- full + book: 上述 + 四域 VInt 解码 + 分价表/成交概要更新入 Book
- 每轮 5,000,000 报文，预热 100,000；单线程

## 结果（三次，-O2 -flto）
| 口径 | 1 | 2 | 3 |
|---|---|---|---|
| frame only (ns/pkt) | 5.28 | 5.32 | 5.31 |
| frame only (Mpps) | 189.3 | 187.9 | 188.3 |
| full+book (ns/pkt) | 40.26 | 40.57 | 40.12 |
| full+book (Mpps) | 24.8 | 24.6 | 24.9 |

## 优化链
| 版本 | full ns/pkt | Mpps | 手段 |
|---|---|---|---|
| 基线 | 114.98 | 8.7 | 每域查 unordered_map |
| v2 | 85.24 | 11.7 | 缓存 ContractState* |
| v3 | 57.60 | 17.4 | DecodeSVInt inline 快路径 + 模板遍历去虚 |
| v4 | 40.3 | 24.8 | -flto 跨 TU 内联 |

## 说明
- 纯软件解码基准，不含收包/组播/系统调用，非端到端延迟。
- 数字与编译器强相关，引用须带 `-O2 -flto` 与 CPU 型号（i5-7400）。
