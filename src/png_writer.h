#pragma once
// Minimal dependency-free RGBA8 PNG writer (stored/uncompressed DEFLATE blocks).
// Used by ProxyLayer to persist downsampled proxy images that the host can load
// through the official "image:<path>" VRAM-cached resource.
// PNG is straight-alpha by specification; the caller must un-premultiply.
// No external dependencies (no zlib / no WIC).

#include <cstdint>
#include <vector>
#include <cstring>

namespace png {

inline uint32_t crc_table[256];
inline bool crc_ready = false;

inline void init_crc() {
    if (crc_ready) return;
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[n] = c;
    }
    crc_ready = true;
}

inline uint32_t crc32(const uint8_t* buf, size_t len) {
    init_crc();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

inline void push_be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}

inline void chunk(std::vector<uint8_t>& out, const char type[4],
                  const uint8_t* data, size_t len) {
    push_be32(out, (uint32_t)len);
    const size_t type_off = out.size();
    out.insert(out.end(), type, type + 4);
    if (len) out.insert(out.end(), data, data + len);
    push_be32(out, crc32(out.data() + type_off, 4 + len));
}

// raw: width*height*4 bytes RGBA8 straight alpha, row-major, no padding.
inline std::vector<uint8_t> encode_rgba8(const uint8_t* raw, int w, int h) {
    std::vector<uint8_t> out;
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    out.insert(out.end(), sig, sig + 8);

    uint8_t ihdr[13] = {};
    ihdr[0] = (uint8_t)(w >> 24); ihdr[1] = (uint8_t)(w >> 16);
    ihdr[2] = (uint8_t)(w >> 8);  ihdr[3] = (uint8_t)w;
    ihdr[4] = (uint8_t)(h >> 24); ihdr[5] = (uint8_t)(h >> 16);
    ihdr[6] = (uint8_t)(h >> 8);  ihdr[7] = (uint8_t)h;
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 6;   // colour type RGBA
    ihdr[10] = 0;  // deflate
    ihdr[11] = 0;  // filter
    ihdr[12] = 0;  // no interlace
    chunk(out, "IHDR", ihdr, 13);

    // Uncompressed payload: one filter-type byte (0) per scanline.
    const size_t row_bytes = (size_t)w * 4u + 1u;
    std::vector<uint8_t> payload(row_bytes * (size_t)h);
    for (int y = 0; y < h; ++y) {
        uint8_t* d = payload.data() + (size_t)y * row_bytes;
        d[0] = 0;
        std::memcpy(d + 1, raw + (size_t)y * (size_t)w * 4u, (size_t)w * 4u);
    }

    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < payload.size(); ++i) {
        a = (a + payload[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    const uint32_t adler = (b << 16) | a;

    std::vector<uint8_t> z;
    z.reserve(payload.size() + payload.size() / 65535u * 5u + 16u);
    z.push_back(0x78); z.push_back(0x01); // zlib header, FCHECK valid
    size_t pos = 0;
    while (pos < payload.size()) {
        const size_t n = (payload.size() - pos > 65535u) ? 65535u : (payload.size() - pos);
        const uint8_t bfinal = (pos + n == payload.size()) ? 1 : 0;
        z.push_back(bfinal);                        // BFINAL, BTYPE=00 stored
        z.push_back((uint8_t)(n & 0xFF));
        z.push_back((uint8_t)((n >> 8) & 0xFF));
        z.push_back((uint8_t)((~n) & 0xFF));
        z.push_back((uint8_t)(((~n) >> 8) & 0xFF));
        z.insert(z.end(), payload.begin() + (std::ptrdiff_t)pos,
                 payload.begin() + (std::ptrdiff_t)(pos + n));
        pos += n;
    }
    push_be32(z, adler);
    chunk(out, "IDAT", z.data(), z.size());
    chunk(out, "IEND", nullptr, 0);
    return out;
}

} // namespace png
