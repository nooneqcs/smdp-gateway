#include "mirp.h"
#include <cstring>

namespace smdp {

int ParseMirpHeader(const uint8_t* buf, size_t len, MirpHeader& h) {
    if (len < sizeof(MirpHeader)) return -1;
    std::memcpy(&h, buf, sizeof(MirpHeader));
    return 0;
}

int IterateDomains(const uint8_t* body, size_t bodyLen, DomainVisitor& v) {
    size_t off = 0;
    int n = 0;
    while (off + 4 <= bodyLen) {
        int16_t fid, fsz;
        std::memcpy(&fid, body + off, 2);
        std::memcpy(&fsz, body + off + 2, 2);
        off += 4;
        if (fsz < 0 || off + (size_t)fsz > bodyLen) return -1;
        v.OnDomain(DomainRef{fid, fsz, body + off});
        off += (size_t)fsz;
        ++n;
    }
    return (off == bodyLen) ? n : -1;
}

} // namespace smdp
