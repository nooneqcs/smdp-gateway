import struct, sys

def rd(path):
    f = open(path, 'rb'); g = f.read(24); m = g[:4]
    if   m == b'\xd4\xc3\xb2\xa1': e, ns = '<', 0
    elif m == b'\x4d\x3c\xb2\xa1': e, ns = '<', 1
    elif m == b'\xa1\xb2\xc3\xd4': e, ns = '>', 0
    elif m == b'\xa1\xb2\x3c\x4d': e, ns = '>', 1
    else: raise SystemExit('bad magic %r' % m)
    out = []
    while True:
        h = f.read(16)
        if len(h) < 16: break
        a, b, c, d = struct.unpack(e + 'IIII', h)
        data = f.read(c)
        if len(data) < c: break
        out.append((a * 1000000000 + (b if ns else b * 1000), data))
    return out

def payload(p):
    if len(p) < 34: return None
    et = struct.unpack('!H', p[12:14])[0]; o = 14
    if et == 0x8100:
        et = struct.unpack('!H', p[16:18])[0]; o = 18
    if et != 0x0800: return None
    ip = p[o:]
    if (ip[0] >> 4) != 4 or ip[9] != 6: return None
    ihl = (ip[0] & 15) * 4
    tot = struct.unpack('!H', ip[2:4])[0]
    t = ip[ihl:]
    if len(t) < 20: return None
    do = (t[12] >> 4) * 4
    return ip[12:16], t[do:tot-ihl]

S = bytes([182, 254, 243, 31])

# 1) 拼 TCP 流，记录每段对应的时间戳
stream = bytearray()
seg = []          # (offset_end, ts)
for ts, data in rd(sys.argv[1]):
    r = payload(data)
    if r and r[0] == S:
        stream += r[1]
        seg.append((len(stream), ts))

# 2) 按 CTP 帧头 02 00 len_be16 切消息
msgs = []         # (offset, ts)
pos = 0
bad = 0
while pos + 4 <= len(stream):
    if stream[pos] != 0x02 or stream[pos+1] != 0x00:
        pos += 1; bad += 1; continue
    ln = (stream[pos+2] << 8) | stream[pos+3]
    if pos + 4 + ln > len(stream): break
    ts = None
    for end, t in seg:
        if pos < end: ts = t; break
    msgs.append((pos, ts, ln))
    pos += 4 + ln

print('stream bytes:', len(stream), ' messages:', len(msgs), ' resync_bytes:', bad)
from collections import Counter
print('len hist:', Counter((l // 20) * 20 for _, _, l in msgs).most_common())

if len(sys.argv) > 2:
    lo = int(sys.argv[2])
    msgs = [m for m in msgs if m[2] >= lo]
    print('after filter >=', lo, ':', len(msgs))

raw = open('tick_ts.bin', 'rb').read()
n = len(raw) // 16
tk = [struct.unpack('<qq', raw[i*16:i*16+16])[1] for i in range(n)]
print('ticks:', n)
extra = len(msgs) - n
if extra > 0:
    pass  # no trim
    print('trimmed front:', extra)

from datetime import datetime
def _f(ns): return datetime.fromtimestamp(ns/1e9).strftime('%H:%M:%S.%f')
print('first tick', _f(tk[0]), 'last tick', _f(tk[-1]))
print('first msg ', _f(msgs[0][1]), 'last msg ', _f(msgs[-1][1]))
ds = []
i = j = 0
while i < len(tk) and j < len(msgs):
    d = tk[i] - msgs[j][1]
    if d < 0 or d > 20000000:
        j += 1
    else:
        ds.append(d); i += 1; j += 1
ds.sort()
print('matched:', len(ds), 'of', len(tk))
def pct(q): return ds[min(len(ds)-1, int(len(ds)*q))]
print('deltas(us): min=%.1f p50=%.1f p90=%.1f p99=%.1f max=%.1f' %
      (ds[0]/1e3, pct(.5)/1e3, pct(.9)/1e3, pct(.99)/1e3, ds[-1]/1e3))
print('negative:', sum(1 for x in ds if x < 0))
j = 0; last = None; ds2 = []
for t in tk:
    while j < len(msgs) and msgs[j][1] <= t:
        last = msgs[j][1]; j += 1
    if last is not None:
        ds2.append(t - last)
ds2.sort()
def q2(q): return ds2[min(len(ds2)-1, int(len(ds2)*q))]
print('arrive->callback(us): n=%d min=%.1f p50=%.1f p90=%.1f p99=%.1f max=%.1f' %
      (len(ds2), ds2[0]/1e3, q2(.5)/1e3, q2(.9)/1e3, q2(.99)/1e3, ds2[-1]/1e3))
print('  <1ms: %d   <10ms: %d   >=10ms: %d' %
      (sum(1 for x in ds2 if x < 1000000),
       sum(1 for x in ds2 if x < 10000000),
       sum(1 for x in ds2 if x >= 10000000)))

