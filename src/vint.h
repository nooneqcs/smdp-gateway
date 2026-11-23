#pragma once
#include <cstdint>
#include <cstddef>

namespace smdp {

// 无符号 Varint（原始 LEB128），一般不用；VInt 字段请用 DecodeSVInt
bool DecodeVInt(const uint8_t*& p, const uint8_t* end, uint64_t& out);

// VInt = Varint + ZigZag（协议规定的字段编码），64 位有符号
bool DecodeSVInt(const uint8_t*& p, const uint8_t* end, int64_t& out);

// 批量解析无符号 Varint
size_t DecodeVIntBatch(const uint8_t* data, size_t len, uint64_t* out, size_t n);

} // namespace smdp
