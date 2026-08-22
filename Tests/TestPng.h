// Tests/TestPng.h — write a tiny RGBA PNG without zlib (stored deflate block). For asset/render tests (ADR-0037).
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace akeirtest {

inline std::uint32_t crc32(const std::uint8_t* d, std::size_t n, std::uint32_t c = 0xFFFFFFFFu) {
    for (std::size_t i = 0; i < n; ++i) { c ^= d[i]; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return c;
}

/// pixels: w*h RGBA bytes, row-major top-down.
inline bool writePng(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgba) {
    std::vector<std::uint8_t> raw;
    for (int y = 0; y < h; ++y) { raw.push_back(0); raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(y) * w * 4, rgba.begin() + static_cast<std::ptrdiff_t>(y + 1) * w * 4); }
    // zlib stream: header + one stored block + adler32
    std::vector<std::uint8_t> z = {0x78, 0x01, 0x01};
    const std::uint16_t len = static_cast<std::uint16_t>(raw.size()), nlen = static_cast<std::uint16_t>(~len);
    z.push_back(len & 0xFF); z.push_back(len >> 8); z.push_back(nlen & 0xFF); z.push_back(nlen >> 8);
    z.insert(z.end(), raw.begin(), raw.end());
    std::uint32_t a = 1, b = 0;
    for (auto c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    std::uint32_t adler = (b << 16) | a;
    for (int i = 3; i >= 0; --i) z.push_back((adler >> (i * 8)) & 0xFF);
    auto be32 = [](std::vector<std::uint8_t>& v, std::uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back((x >> (i * 8)) & 0xFF); };
    auto chunk = [&](std::vector<std::uint8_t>& out, const char* tag, const std::vector<std::uint8_t>& data) {
        be32(out, static_cast<std::uint32_t>(data.size()));
        std::vector<std::uint8_t> td(tag, tag + 4); td.insert(td.end(), data.begin(), data.end());
        out.insert(out.end(), td.begin(), td.end());
        be32(out, crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
    };
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr; be32(ihdr, static_cast<std::uint32_t>(w)); be32(ihdr, static_cast<std::uint32_t>(h)); ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

} // namespace akeirtest
