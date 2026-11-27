#!/usr/bin/env python3
# ctp_wire_label.py —— 每帧自动标注 5 个锚定字段偏移，看是否稳定
import sys, struct, re, time
TICK='<16sq4d3q2d'; TSZ=struct.calcsize(TICK)

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

def ticks(path):
    d=open(path,'rb').read(); out={}
    for i in range(0,len(d)-TSZ+1,TSZ):
        f=struct.unpack(TICK,d[i:i+TSZ])
        if f[0].split(b'\0')[0]!=b'au2702': continue
        tm=time.gmtime(f[1]//1000000+8*3600)
        hh=f'{tm.tm_hour:02d}:{tm.tm_min:02d}:{tm.tm_sec:02d}'
        out.setdefault(hh,[]).append(dict(last=f[2],bid1=f[3],ask1=f[4],pre=f[5],oi=f[10]))
    return out

def find_off(b,v):
    p=struct.pack('>d',v); return b.find(p)

def main():
    pcap,tbin=sys.argv[1],sys.argv[2]
    fr=frames(pcap); tk=ticks(tbin); rows=[]
    for idx,f in enumerate(fr):
        m=re.search(rb'(\d\d:\d\d:\d\d)',f)
        if not m: continue
        for c in tk.get(m.group(0).decode(),[]):
            offs=[find_off(f,c[k]) for k in ('last','pre','bid1','ask1','oi')]
            if min(offs)>=0: rows.append((idx,len(f),m.group(0).decode(),offs)); break
    print(f'frames={len(fr)} labeled={len(rows)}')
    from collections import Counter
    cnt=Counter(tuple(r[3]) for r in rows)
    print('offset-tuple(last,pre,bid1,ask1,oi) -> count:')
    for k,v in cnt.most_common(10): print('  ',k,v)
    print('--- 前 20 帧 ---')
    for r in rows[:20]: print(f'  frame[{r[0]:3d}] size={r[1]:3d} {r[2]} offs={r[3]}')
main()
