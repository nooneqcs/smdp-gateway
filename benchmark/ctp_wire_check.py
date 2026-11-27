#!/usr/bin/env python3
# ctp_wire_check.py —— 固定偏移(35/43/97/155/165)直接读，看是否合理，判定偏移稳不稳
import sys, struct
from collections import Counter

def read_pcap(p):
    d=open(p,'rb').read(); m=d[:4]
    e='<' if m in (b'\xd4\xc3\xb2\xa1',b'\x4d\x3c\xb2\xa1') else '>'
    o=24; out=[]
    while o+16<=len(d):
        a,b,c,org=struct.unpack(e+'IIII',d[o:o+16]); o+=16
        if o+c>len(d): break
        out.append(d[o:o+c]); o+=c
    return out

def l4(pk):
    if len(pk)<14: return None
    et=struct.unpack('!H',pk[12:14])[0]; o=14
    if et==0x8100: et=struct.unpack('!H',pk[16:18])[0]; o=18
    if et!=0x0800: return None
    ip=pk[o:]; ihl=(ip[0]&0xf)*4
    if ip[9]!=6: return None
    tot=struct.unpack('!H',ip[2:4])[0]; tcp=ip[ihl:tot]
    sp=struct.unpack('!H',tcp[:2])[0]; seq=struct.unpack('!I',tcp[4:8])[0]
    return sp,seq,tcp[(tcp[12]>>4)*4:]

def frames(p):
    segs=[]
    for pk in read_pcap(p):
        r=l4(pk)
        if r and r[0]==30012 and r[2]: segs.append((r[1],r[2]))
    segs.sort(); s=bytearray(); nx=None
    for seq,pl in segs:
        if nx is None or seq==nx: s+=pl; nx=seq+len(pl)
        elif seq>nx and nx-seq<len(pl): s+=pl[nx-seq:]; nx=seq+len(pl)
    out=[]; i=0
    while i+4<=len(s):
        if s[i]==2 and s[i+1]==0:
            ln=struct.unpack('!H',s[i+2:i+4])[0]
            if 0<ln<=4000 and i+4+ln<=len(s): out.append(bytes(s[i+4:i+4+ln])); i+=4+ln; continue
        i+=1
    return out

def dbl(f,off):
    if off+8>len(f): return None
    return struct.unpack('>d',f[off:off+8])[0]

def main():
    fr=frames(sys.argv[1])
    print('frames=',len(fr))
    print('size hist=',dict(Counter(len(f) for f in fr)))
    ok=0; fail=[]; ok_sizes=Counter(); fail_sizes=Counter()
    for f in fr:
        last=dbl(f,35); pre=dbl(f,43); oi=dbl(f,97); bid1=dbl(f,155); ask1=dbl(f,165)
        if None in (last,pre,oi,bid1,ask1):
            fail.append((len(f),'short')); fail_sizes[len(f)]+=1; continue
        good = (600<last<900 and abs(pre-756.32)<0.01 and 100000<oi<200000
                and 600<bid1<900 and 600<ask1<900 and abs(ask1-bid1)<5)
        if good: ok+=1; ok_sizes[len(f)]+=1
        else:
            fail.append((len(f),f'last={last:.3f} pre={pre:.3f} oi={oi:.0f} bid={bid1:.3f} ask={ask1:.3f}'))
            fail_sizes[len(f)]+=1
    print(f'fixed-offset plausible: {ok}/{len(fr)}')
    print('ok sizes  =',dict(ok_sizes))
    print('fail sizes=',dict(fail_sizes))
    print('--- 前 10 个 fail ---')
    for x in fail[:10]: print('  ',x)
main()
