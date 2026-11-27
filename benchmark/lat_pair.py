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
arr = []
for ts, data in rd(sys.argv[1]):
    r = payload(data)
    if r and r[0] == S and len(r[1]) > 40:
        arr.append(ts)

raw = open('tick_ts.bin', 'rb').read()
n = len(raw) // 16
tk = []
for i in range(n):
    tk.append(struct.unpack('<qq', raw[i*16:i*16+16])[1])

print('server->client data packets:', len(arr))
print('ticks:', len(tk))

m = min(len(arr), len(tk))
ds = sorted(tk[i] - arr[i] for i in range(m))
def pct(q): return ds[min(len(ds)-1, int(len(ds)*q))]
print('deltas(us): min=%.1f p50=%.1f p90=%.1f p99=%.1f max=%.1f' %
      (ds[0]/1e3, pct(.5)/1e3, pct(.9)/1e3, pct(.99)/1e3, ds[-1]/1e3))
print('negative:', sum(1 for x in ds if x < 0))
