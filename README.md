# smdp-gateway

上期所 SMDP2.0 行情协议（MIRP 增量 + MDQP 查询）的解码与快照合并实现，个人学习/技术作品。纯软件实现，不接入生产组播。

## 范围与边界

- **做**：报文解析、域分派、分价表增量合并、快照合并、解码性能测量
- **不做**：生产组播接入（无接入账号/组播配置），无实盘延迟数据
- **数据**：公开规范中的编解码示例报文 + 自造边界数据 + 按协议语义构造的真实感负载

## 协议依据（公开文档）

- 《上海期货交易所 第二代行情发布平台 接口规范》v1.22
- 《上海期货交易所 第二代行情发布平台 编解码示例》v1.12
- 《交易 API 和行情 API 接口规范》v2.00

详见 `docs/SOURCES.md`（仅列文档名与版本，不含原文）。

## 技术要点

- MIRP 报文 = 24 字节固定头 + 域序列；域由 `FieldID(Int16)` + `FieldSize(Int16)` 头切分
- VInt = Varint + ZigZag（同 protobuf `sint64`），64 位有符号，最多 10 字节
- MDQP 与 MIRP 同一 FieldID 含义不同（0x0003/0x1001/0x1002），解码按「协议 + 消息类型」分派
- 快照 + 增量合并：分价表变动事件序列按事务处理，超深度档位丢弃

## 现状

- P0–P6 完成：MDQP/MIRP 全字段解码、book 合并、报文级总装、性能基准、边界测试
- 性能见 `docs/RESULTS-SMDP.md`：理想 35.87 ns / 真实感 255.8 ns

## 目录

```
src/        解码(mirp/vint/decode/delta)、合并(book/market2)、编码(encode)
tests/      单测
benchmark/  性能基准（理想 + 真实感负载）
docs/       协议/资料/结果
```

## 构建 / 测试

依赖：g++ (C++17)、x86-64（SSE4.1/BMI2，0x1002 SIMD 用）。无第三方库。
必须带 `-march=native`（或至少 `-msse4.1`），否则 SIMD 编译报 target mismatch。

源码：
- `src/mirp.{h,cpp}`   24B 头 + 域遍历
- `src/vint.h`         VInt(zigzag) 解码（header-inline）
- `src/decode.{h,cpp}` 0x0003 / 0x1001 / 快照 0x0101 / 0x0102
- `src/delta.{h,cpp}`  0x1002 / 0x1011~0x1018
- `src/book.{h,cpp}`   快照装载 + 分价表增量
- `src/market2.{h,cpp}` MarketStore + 域分派
- `src/encode.{h,cpp}` MIRP 编码器

单元测试示例：
```
g++ -std=c++17 -O2 -march=native -Isrc src/mirp.cpp src/decode.cpp tests/mirp_test.cpp -o mirp_test
```
其余：snapshot_test / delta_test / book_test / market_test2 / boundary_test / encode_test。

真实感负载基准：
```
g++ -std=c++17 -O3 -march=native -flto -Isrc src/encode.cpp src/mirp.cpp src/decode.cpp src/delta.cpp src/book.cpp src/market2.cpp benchmark/dec_bench_real.cpp -o dec_real
```

## 约定

- 仓库为唯一事实源：每完成一块 → 更新 PROGRESS → commit → push
- 对外只引用有测试/日志支撑的数字，并写明口径
- 不含生产组播地址、内网 IP、账号密码、第三方文档原文

## 声明

个人学习用途。协议规范著作权归上海期货信息技术有限公司，本仓库不包含其文档原文。

