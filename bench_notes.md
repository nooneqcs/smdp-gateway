
## 2026-11-27 tick 快照 96B vs 64B（seqlock / atomic words，同代码同机同次）
- 96B = 12 word 跨 2 cache line；64B = 8 word 单 line
- publish: 12.06 -> 4.85 ns（-60%；扣 1.86 baseline 净 10.2 -> 3.0，-71%）
- latest : 6.50 -> 4.70 ns（-28%）
- 两侧 tears=0
- 结论：跨 cache line 的 split store 是主因（非单纯少 4 个 word），压 64B 确证值得做
