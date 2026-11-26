#!/usr/bin/env python3
import struct, os, sys, random, shutil
OUT = sys.argv[1] if len(sys.argv)>1 else "benchmark/ticks_simnow.bin"
if os.path.exists(OUT):
    shutil.copy(OUT, OUT+".real.bak")   # 真数据先备份
insts=[b"au2702",b"ag2702",b"cu2701",b"rb2701"]
base={b"au2702":767.0,b"ag2702":9700.0,b"cu2701":89700.0,b"rb2701":3070.0}
vol={i:100000 for i in insts}; to={i:1e9 for i in insts}; oi={i:150000 for i in insts}
rng=random.Random(42); ts0=1700000000000000; recs=[]
for t in range(908):
    inst=insts[t%4]; b=base[inst]; last=b+(t%50)*0.02
    vol[inst]+=rng.randint(0,50); to[inst]+=rng.randint(1000,100000); oi[inst]+=rng.randint(-5,5)
    recs.append(struct.pack('<16s q d d d d q q q d d', inst, ts0+t*500000,
        last, last-0.02, last+0.02, b-10.0,
        rng.randint(1,500), rng.randint(1,500), vol[inst], to[inst], float(oi[inst])))
os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
open(OUT,"wb").write(b"".join(recs))
print(f"wrote {OUT} {len(recs)} records")
