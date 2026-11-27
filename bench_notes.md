
## 2026-11-27 tick 快照 96B vs 64B（seqlock / atomic words，同代码同机同次）
- 96B = 12 word 跨 2 cache line；64B = 8 word 单 line
- publish: 12.06 -> 4.85 ns（-60%；扣 1.86 baseline 净 10.2 -> 3.0，-71%）
- latest : 6.50 -> 4.70 ns（-28%）
- 两侧 tears=0
- 结论：跨 cache line 的 split store 是主因（非单纯少 4 个 word），压 64B 确证值得做

## 2026-11-27 RecTick 压 64B 快照 定稿
- 布局：自然对齐手动排布，无 padding。int64×2 + char[8] + int32×9 + int16 + uint8×2 = 64B
- 三条 static_assert（sizeof=64 + offsetof 24/56/60）编译期全过
- 价格按协议 tick 定点（demo tick=0.02，存 k=p/tick 的 int32）；turnover 保 int64
- 合成同代码对照：publish 12.06(96B) -> 4.85(64B)
- 真实 96B→64B（908 条 simnow）：publish 4.70 / latest 5.03 / compress 21.79 ns，TSC 3.000GHz
- 并发 order_violations=0
- 注：compress 是 demo 的 96B→64B 二次转换，真实解析直接产 64B，此数不对应任何真实环节，不外报

## 2026-11-27 行情核↔策略核 真并发（seqlock 快照）
- 限速 1.50M/s、策略 ~1µs/笔：published 3,001,616 / distinct 1,831,487 / skipped 1,170,103 / coverage 61.0% / bad=0
- 自洽校验：distinct+skipped=3,001,590 ≈ published 3,001,616（差 26 尾部边界）
- 覆盖率和理论吻合：667ns/1.05µs≈63.5% vs 实测 61%
- 对照全速 18.09M/s：coverage 0.8%（过载假象，证明写端不为读端让路，非缺陷）
- 结论：行情→策略用最新值快照，策略慢自动跳过、只取最新、不阻塞行情核；读到的 tick 零撕裂

## 2026-11-27 demo_trade5：seqlock 快照接交易链，与 demo_trade4 等价
- 结构：行情线程(CPU2) bin→compress→publish；策略线程(CPU3) latest→原strategy→match_mkt→on_fill→账本
- RATE=5000：decoded=18160 orders=18160 fills=18160 realized=-90800.0 unrealized=0.0，与 demo_trade4 一字不差
- 结论：SPSC队列→seqlock快照，交易语义与账本完全等价
- 必要条件：绑核(CPU2/CPU3)。不绑核 OS 抢占策略线程>tick间隔，快照覆盖中间tick导致漏单(实测18157~18159波动)
- 启动边界：ready握手(strat seed后置位, prod等待)才从seq=0读起；不握手会跳seq=0(快照启动不补历史)

## 2026-11-27 SMDP 快照合并三原语验证（官方 al1201 / 成交示例）
- apply_level: EventType '1'增/'2'改/'3'删, MDEntryType '0'买/'1'卖（ASCII 字符非数字）, 1-based 档位
  价格 Price = CodecPrice + PriceOffset*PriceTick（18000+20*5=18100 对上）
- apply_trade: Turnover += (VolumeChange*CodecPrice + TurnoverOffset*PriceTick)*VolumeMultiple
  官方示例 Volume 4->8、Turnover 360000->721000、OI 1000->1004->1008 全对上
- apply_head: ChangeNo 连号 gaps=0、跳号(4->7) gaps=1
- 三个原语均用《SMDP2.0 编解码示例 v1.12》逐行验证
