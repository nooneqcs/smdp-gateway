#!/usr/bin/env python3
# ctp_wire_map.py —— 用 ticks_simnow.bin 的官方值在 wire 里定位字段偏移
import sys, struct, time

TICK='<16sq4d3q2d'          # RecTick 96B
TSZ=struct.calcsize(TICK)

def read_pcap(p):
    d=open(p,'rb').read(); m=d[:4]
    e='<' if m in (b'\xd4\xc3\xb2\xa1',b'\x4d\x3c\xb2\xa1') else '>'
    o=24; out=[]
    while o+16<=len(d):
        ts,tu,incl,org=struct.unpack(e+'IIII',d[o:o+16]); o+=16
        if o+incl>len(d): break
        out.append(d[o:o+incl]); o+=incl
    return out

def l4(pk):
    if len(pk)<14: return None
    et=struct.unpack('!H',pk[12:14])[0]; o=14
    if et==0x8100: et=struct.unpack('!H',pk[16:18])[0]; o=18
    if et!=0x0800: return None
    ip=pk[o:]; ihl=(ip[0]&0xf)*4
    if ip[9]!=6: return None
    tot=struct.unpack('!H',ip[2:4])[0]; tcp=ip[ihl:tot]
    sp,dp=struct.unpack('!HH',tcp[:4]); seq=struct.unpack('!I',tcp[4:8])[0]
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

def doubles(b):
    r=[]
    for o in range(len(b)-7):
        v=struct.unpack('>d',b[o:o+8])[0]
        if v==v and 0.01<abs(v)<1e7: r.append((o,v))
    return r

def ticks(path,sym):
    d=open(path,'rb').read(); out=[]
    for i in range(0,len(d)-TSZ+1,TSZ):
        f=struct.unpack(TICK,d[i:i+TSZ])
        inst=f[0].split(b'\0')[0].decode('ascii','ignore')
        if inst!=sym: continue
        tm=time.gmtime(f[1]//1000000+8*3600)
        out.append(dict(i=i//TSZ,time=f'{tm.tm_hour:02d}:{tm.tm_min:02d}:{tm.tm_sec:02d}',
                        last=f[2],bid1=f[3],ask1=f[4],pre=f[5],turnover=f[9],oi=f[10]))
    return out

def main():
    pcap,tbin=sys.argv[1],sys.argv[2]; sym=sys.argv[3] if len(sys.argv)>3 else 'au2702'
    fr=frames(pcap); big=max(fr,key=len)
    print(f'frames={len(fr)} biggest={len(big)}')
    ds=doubles(big)
    print('--- wire doubles ---')
    for o,v in ds: print(f'  @{o:4d} {v:g}')
    tk=ticks(tbin,sym)
    print(f'--- {sym} ticks={len(tk)} (前20) ---')
    for t in tk[:20]:
        print(f"  [{t['i']}] {t['time']} last={t['last']:g} bid1={t['bid1']:g} ask1={t['ask1']:g} pre={t['pre']:g} to={t['turnover']:g} oi={t['oi']:g}")
    print('--- auto-match ---')
    for t in tk:
        for o,v in ds:
            for k in ('last','bid1','ask1','pre','turnover','oi'):
                fv=t[k]
                if abs(v-fv)<=1e-9*max(1.0,abs(fv)):
                    print(f"  @{o:4d} {v:g} == {sym}[{t['i']}] {t['time']}.{k}")
main()
