
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
