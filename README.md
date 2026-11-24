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

## P7 · DPDK 端到端联调与跨核分发

### 链路拓扑

```
Windows 笔记本 (Realtek USB GbE, 192.168.99.2)
   └─ 二层直连，dst MAC = 广播 ff:ff:ff:ff:ff:ff
      └─ i210 (vfio-pci, 0000:01:00.0)
         └─ dpdk_mirp_v8
            ├─ lcore1：轮询收包 → 严格校验 → MIRP 解码 → book 合并
            └─ SPSC 无锁环 → lcore2：消费并打时间戳
```

### 验证结果

- 20000 条 40B MIRP 报单：`rx_total=20267`，解出 20000，入队/消费各 20000，**丢弃 0**
- `topic=1001、Length=16、mlen=40` 与样例报文逐字节一致；`Bid1 / Volume` 正确更新

### 延迟（i5-7400 单机、纯软件、不含网卡收包与 DMA）

| 口径 | 测量内容 | p50 | p99 | p999 |
|---|---|---:|---:|---:|
| 冷缓存单包 | rx_burst 返回 → 处理完（~570 pps） | 74.1 ns | 144.4 ns | 442.8 ns |
| 热缓存单条 | 同一 40B buffer 循环 1e7 次 | 27.3 ns | — | — |
| 跨核入队 | 生产核 push 至 SPSC | 50.2 ns | 54.8 ns | — |
| 跨核端到端 | t_prod → 消费核 pop | 177.2 ns | 198.2 ns | 13304.8 ns |

### 口径说明（重要）

- `rdtsc` 成对开销本机实测：单核 25.6 ns、双核活跃 43.8 ns，上表数值均已扣减
- 热缓存 27.3 ns 与冷缓存 74.1 ns 之差 ≈ 47 ns，即**内存延迟**（mbuf 数据冷 miss + book 结构写 miss），不代表 ALU 解码成本
- 入队 50.2 ns 是**跨核**口径，含 cache line 转移；不等同于同核入队（2.96 ns）
- p999 13.3 µs 来自**未隔离核的 OS 调度抖动**，非代码缺陷

### 关键实现

- `ValidMirp`：TypeID / Flag / TopicID / Length 四项严格校验，消除混杂模式下的杂包误判
- `MarketVisitor final` + `vector<ContractState>` 数组直接索引，无哈希查找
- SPSC 环 head/tail 各自 64 字节对齐，acquire/release 语义
- 每 burst 处理完统一 `rte_pktmbuf_free`

### 复现

```bash
# 编译
g++ -O2 -march=native -Isrc $(pkg-config --cflags libdpdk) \
    dpdk_mirp_v8.cpp $(ls src/*.cpp | grep -v 'src/market\.cpp$') \
    -o dpdk_mirp_v8 $(pkg-config --libs libdpdk)

# 运行（需要两个 lcore）
sudo ./dpdk_mirp_v8 -l 1,2 -a 0000:01:00.0 -- 20000
```

### 已知限制 / TODO

- 发送端受 Windows Scapy 限制，仅 ~570 pps，**非系统吞吐上限**
- 消费核未隔离（`isolcpus` / `nohz_full`），尾延迟 13 µs
- 入队可批量优化（每 burst 仅更新一次 head），预期降到十几 ns
- 目前用自造报文验证，尚未接入真实 SMDP 组播行情

## P7 · DPDK 端到端联调与跨核分发

### 目标

验证 SMDP2.0 (MIRP) 解码器在**真实二层链路**上的完整链路：
收包 → 严格校验 → 协议解码 → 订单簿合并 → 跨核无锁分发。
不依赖交易所行情源，用 Windows 主机发送自造 MIRP 报文做闭环测试。

### 架构

```
Windows 主机 (Realtek USB GbE, 192.168.99.2)
   │  二层直连，dst MAC = 广播 ff:ff:ff:ff:ff:ff
   ▼
i210 (vfio-pci, 0000:01:00.0)
   ▼
dpdk_mirp_v10  ── lcore 1 ──┐
   轮询 rte_eth_rx_burst      │  校验 + 解码 + book 合并
   收包预取（burst 内）        │
                              ▼
                        SPSC 无锁环 (65536, head/tail 各 64B 对齐)
                              ▼
                        lcore 2 空转消费，打时间戳
```

- 单核轮询收包，无中断、无系统调用
- `nb >= 8` 走批量路径（bulk free + 一次 head 更新的批量入队），`nb < 8` 走逐包路径
- 生产者/消费者分核，SPSC 使用 acquire/release 语义

### 快速开始

```bash
# 依赖：DPDK、libdpdk pkg-config、i210 已绑定 vfio-pci
g++ -O2 -march=native -Isrc $(pkg-config --cflags libdpdk) \
    dpdk_mirp_v10.cpp $(ls src/*.cpp | grep -v 'src/market\.cpp$') \
    -o dpdk_mirp_v10 $(pkg-config --libs libdpdk)

# 收包（两个 lcore：1=生产，2=消费），-- 后为解出条数目标
sudo ./dpdk_mirp_v10 -l 1,2 -a 0000:01:00.0 -- 20000

# 合成流水线吞吐（不经网卡）
sudo SYNTH=1 ./dpdk_mirp_v10 -l 1,2 -a 0000:01:00.0

# 热缓存解码微基准（含头校验/域解码/合并三层拆解）
g++ -O2 -march=native -Isrc hot_bench.cpp \
    $(ls src/*.cpp | grep -v 'src/market\.cpp$') -o hot_bench && ./hot_bench
```

发送端（Windows，目录需管理员权限 + Npcap）：

```python
from scapy.all import *
dev = next(v for v in conf.ifaces.values()
           if "Realtek USB GbE" in str(getattr(v,"name","")) + str(getattr(v,"description","")))
payload = bytes.fromhex("0101100002000000E903000002000000778B0D4FB42D00"
                        "0003000200000401100600313002000200")   # 40B 报单样例
pkt = Ether(dst="ff:ff:ff:ff:ff:ff") / IP(src="192.168.99.2", dst="192.168.99.1") \
      / UDP(sport=40000, dport=9999) / Raw(payload)
sendp(pkt, iface=dev, count=20000, inter=0, verbose=True)
```

### 实测结果

环境：i5-7400（4 核，无 HT）、i210、单线程轮询、纯软件，**不含网卡收包/DMA/队列等待**。

**功能正确性**：40B 报单 20000 条 → `rx_total=20267`，解出 20000，入队/消费各 20000，**丢弃 0**；
`topic=1001`、`Length=16`、`mlen=40` 与样例逐一对应，`Bid1`/`Volume` 正确更新。

**延迟**

| 口径 | 测量内容 | p50 | p99 |
|---|---|---:|---:|
| 冷缓存单包 | `rte_eth_rx_burst` 返回 → 处理完（~570 pps） | 74.1 ns | 144.4 ns |
| 热缓存单条 | 同一 40B buffer 循环 1e7 次（校验+解码+合并） | 27.3 ns | — |
| 跨核端到端 | `t_prod` → 消费核 `pop` | 135.1 ns | 197.5 ns |

热缓存三层拆解（`hot_bench`）：头校验 **2.54 ns**、域解码+合并 **23.3 ns**、完整单条 **27.3 ns**。

**吞吐（SYNTH，合成负载，进程内）**

| 路径 | ns/msg | 吞吐 |
|---|---:|---:|
| 单条入队 | 92–105 | 9.5–10.8 Mpps |
| 批量入队（每 32 条一次 head） | 33 | 30 Mpps（3.2×） |

### 口径说明（重要，引用数据必读）

1. **冷 vs 热是两种口径**。冷缓存 74.1 ns 与热缓存 27.3 ns 之差约 47 ns，即内存延迟
   （mbuf 数据冷 miss + book 结构写 miss），**不是 ALU 解码成本**。
2. **热缓存 27.3 ns 折合 36 Mpps**，远超真实行情速率，这已是该实现的有效上限，不再优化
   （`MarketVisitor` 去虚化 + `-O3` 仅再省 9%）。
3. **批量入队只在 `nb >= 8` 的突发场景有效**。570 pps（`nb≈1`）下 bulk free + 批量是**负优化**
   （处理段 176 → 324 ns），因此 v10 按 `nb` 自适应。
4. **`rdtsc` 成对开销**：单核 25.5 ns、双核活跃 43.8 ns（本机实测 20 万次）；
   所有延迟数值均已扣除。跨 run 比较只能看量级，不能当精确值。
5. **不要拿 SYNTH 的 30 Mpps 暗示线上吞吐**。它只证明“解码+分发流水线不是瓶颈”，
   真实瓶颈在收包与 OS。

### 实现要点

- `ValidMirp`：`TypeID / Flag / TopicID / Length` 四项严格校验，消除混杂模式下的杂包误判
  （未加之前，5 个杂包里有 3 个被误判为 MIRP，`topic=256`）
- `MarketVisitor final` + `vector<ContractState>` 数组直接索引，无哈希表
- SPSC 环：`head`/`tail` 分处不同 cache line，避免伪共享
- burst 内 `rte_prefetch0` 预取下一包，隐藏部分冷 miss
- 发送端用广播目的 MAC，避免依赖接收端 MAC 与 IGMP

### 已知限制与后续

- [ ] 当前为**单播自造报文**，未接真实 SMDP 组播行情；组播 MAC 过滤 / 多主题分发待实现
- [ ] 快照恢复（0x0101/0x0103 重建 → 对增量）流程未做端到端验证
- [ ] 序号缺口检测与 MDQP 补包请求未实现（行情网关可靠性关键路径）
- [ ] 消费核未做 CPU 隔离，p999 尾延迟受 OS 调度抖动影响（需 `isolcpus` / `nohz_full`）
- [ ] 可观测性：延迟直方图、丢包计数、序号跳变告警
- [ ] 多合约规模测试（当前 1 合约 + 1000 预置槽位）

