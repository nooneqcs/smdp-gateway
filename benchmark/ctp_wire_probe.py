#!/usr/bin/env python3
# ctp_wire_probe.py —— 摊开 CTP 私有 wire：切帧 + hex + ASCII + BE-double 扫描
import sys, struct, re

def read_pcap(path):
    data = open(path,'rb').read()
    m = data[:4]
    if m in (b'\xd4\xc3\xb2\xa1', b'\x4d\x3c\xb2\xa1'): e='<'
    elif m in (b'\xa1\xb2\xc3\xd4', b'\xa1\xb2\x3c\x4d'): e='>'
    else: raise SystemExit('bad pcap magic '+m.hex())
    off=24; out=[]
    while off+16<=len(data):
        ts,tu,incl,orig=struct.unpack(e+'IIII', data[off:off+16]); off+=16
        if off+incl>len(data): break
        out.append((ts+tu/1e6, data[off:off+incl])); off+=incl
    return out

def l4(pkt):
    if len(pkt)<14: return None
    et=struct.unpack('!H',pkt[12:14])[0]; o=14
    if et==0x8100:
        et=struct.unpack('!H',pkt[16:18])[0]; o=18
    if et!=0x0800: return None
    ip=pkt[o:]
    if len(ip)<20: return None
    ihl=(ip[0]&0xf)*4
    if ip[9]!=6: return None
    tot=struct.unpack('!H',ip[2:4])[0]
    tcp=ip[ihl:tot]
    if len(tcp)<20: return None
    sport,dport=struct.unpack('!HH',tcp[0:4])
    seq=struct.unpack('!I',tcp[4:8])[0]
    doff=(tcp[12]>>4)*4
    return ip[12:16],sport,ip[16:20],dport,seq,tcp[doff:]

def main():
    path=sys.argv[1]; K=int(sys.argv[2]) if len(sys.argv)>2 else 5
    pkts=read_pcap(path)
    print(f'pcap packets = {len(pkts)}')
    segs=[]
    for ts,p in pkts:
        r=l4(p)
        if not r: continue
        src,sport,dst,dport,seq,pl=r
        if sport==30012 and pl: segs.append((seq,pl))   # 服务器->客户端
    segs.sort()
    stream=bytearray(); nxt=None
    for seq,pl in segs:
        if nxt is None or seq==nxt: stream+=pl; nxt=seq+len(pl)
        elif seq>nxt:
            skip=nxt-seq
            if skip<len(pl): stream+=pl[skip:]; nxt=seq+len(pl)
    print(f'stream bytes = {len(stream)}')

    i=0; frames=[]
    while i+4<=len(stream):
        if stream[i]==2 and stream[i+1]==0:
            ln=struct.unpack('!H',stream[i+2:i+4])[0]
            if 0<ln<=4000 and i+4+ln<=len(stream):
                frames.append(bytes(stream[i+4:i+4+ln])); i+=4+ln; continue
        i+=1
    print(f'messages = {len(frames)}  (len hist: ', end='')
    from collections import Counter
    print(dict(Counter(len(f) for f in frames)), ')')

    for idx,f in enumerate(frames[:K]):
        print(f'\n===== msg[{idx}] bodylen={len(f)} =====')
        for o in range(0,len(f),16):
            chunk=f[o:o+16]
            hx=' '.join(f'{b:02x}' for b in chunk)
            asc=''.join(chr(b) if 32<=b<127 else '.' for b in chunk)
            print(f'{o:04x}  {hx:<47}  {asc}')
        print('-- ASCII runs (off,len):')
        for m in re.finditer(rb'[ -~]{4,}', f):
            print(f'   @{m.start():3d} len={len(m.group())}  {m.group()!r}')
        print('-- BE-double in (0.01,1e7):')
        for o in range(0,len(f)-7):
            v=struct.unpack('>d',f[o:o+8])[0]
            if 0.01 < abs(v) < 1e7 and v==v:
                print(f'   @{o:3d}  {v:g}')
    return frames

if __name__=='__main__':
    main()
