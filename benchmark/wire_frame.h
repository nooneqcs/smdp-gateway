#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace wire {

static inline uint16_t Csum16(const void* d, size_t n)
{
    const uint8_t* p = (const uint8_t*)d;
    uint32_t s = 0;
    while (n >= 2) { s += (uint32_t)((p[0] << 8) | p[1]); p += 2; n -= 2; }
    if (n) s += (uint32_t)(p[0] << 8);
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return (uint16_t)~s;
}

// vlanId = 0 表示不打 VLAN tag
inline size_t BuildUdpFrame(uint8_t* out, const uint8_t* payload, size_t len,
                            const uint8_t dstMac[6], const uint8_t srcMac[6],
                            const uint8_t srcIp[4], const uint8_t dstIp[4],
                            uint16_t srcPort, uint16_t dstPort,
                            uint16_t vlanId = 0)
{
    uint8_t* p = out;
    std::memcpy(p, dstMac, 6); p += 6;
    std::memcpy(p, srcMac, 6); p += 6;
    if (vlanId) {
        *p++ = 0x81; *p++ = 0x00;
        *p++ = (uint8_t)(vlanId >> 8); *p++ = (uint8_t)(vlanId & 0xFF);
    }
    *p++ = 0x08; *p++ = 0x00;

    uint8_t* ip = p;
    size_t ipTot = 20 + 8 + len;
    std::memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[2] = (uint8_t)(ipTot >> 8); ip[3] = (uint8_t)(ipTot & 0xFF);
    ip[8] = 64; ip[9] = 17;
    std::memcpy(ip + 12, srcIp, 4);
    std::memcpy(ip + 16, dstIp, 4);
    uint16_t c = Csum16(ip, 20);
    ip[10] = (uint8_t)(c >> 8); ip[11] = (uint8_t)(c & 0xFF);
    p += 20;

    uint8_t* udp = p;
    udp[0] = (uint8_t)(srcPort >> 8); udp[1] = (uint8_t)(srcPort & 0xFF);
    udp[2] = (uint8_t)(dstPort >> 8); udp[3] = (uint8_t)(dstPort & 0xFF);
    uint16_t ulen = (uint16_t)(8 + len);
    udp[4] = (uint8_t)(ulen >> 8);   udp[5] = (uint8_t)(ulen & 0xFF);
    udp[6] = 0; udp[7] = 0;          // UDP checksum = 0（IPv4 合法）
    p += 8;

    std::memcpy(p, payload, len);
    p += len;
    return (size_t)(p - out);
}

struct View { const uint8_t* payload; size_t len; };

inline bool ParseUdpFrame(const uint8_t* f, size_t flen, View& v)
{
    if (flen < 14) return false;
    size_t off = 12;
    uint16_t et = (uint16_t)((f[off] << 8) | f[off + 1]);
    off += 2;
    if (et == 0x8100) {
        if (flen < off + 4) return false;
        et = (uint16_t)((f[off + 2] << 8) | f[off + 3]);
        off += 4;
    }
    if (et != 0x0800) return false;
    if (flen < off + 20) return false;

    const uint8_t* ip = f + off;
    if ((ip[0] >> 4) != 4) return false;
    size_t ihl = (size_t)(ip[0] & 0x0f) * 4;
    if (ihl < 20 || flen < off + ihl) return false;
    if (ip[9] != 17) return false;

    size_t tot = (size_t)((ip[2] << 8) | ip[3]);
    if (tot < ihl + 8 || off + tot > flen) return false;

    const uint8_t* udp = ip + ihl;
    size_t ulen = (size_t)((udp[4] << 8) | udp[5]);
    if (ulen < 8 || ulen > tot - ihl) return false;

    v.payload = udp + 8;
    v.len = ulen - 8;
    return true;
}

} // namespace wire
