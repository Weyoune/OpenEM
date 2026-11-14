// SPDX-License-Identifier: GPL-3.0-only
// Picture import/export of the portable editor front end; see sdl_image_io.hpp. The stb implementations are compiled
// in sdl_stb.cpp.
#include "editor/sdl_image_io.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "stb_image.h"

// The zlib compressor of stb_image_write (public, but not declared in its header).
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace em::editor {

namespace fs = std::filesystem;

bool load_argb(const fs::path& from, std::vector<uint32_t>& out, int& w, int& h) {
    std::ifstream f(from, std::ios::binary);
    if (!f) return false;
    const std::vector<unsigned char> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (file.empty() || file.size() > 0x7FFFFFFF) return false;
    int iw = 0, ih = 0, comp = 0;
    if (!stbi_info_from_memory(file.data(), int(file.size()), &iw, &ih, &comp) || iw <= 0 || ih <= 0 || iw > 4096 || ih > 4096)
        return false;
    unsigned char* rgba = stbi_load_from_memory(file.data(), int(file.size()), &iw, &ih, &comp, 4);
    if (!rgba) return false;
    out.resize(size_t(iw) * size_t(ih));
    for (size_t i = 0; i < out.size(); i++) {
        const unsigned char* p = rgba + i * 4;
        out[i] = uint32_t(p[3]) << 24 | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }
    stbi_image_free(rgba);
    w = iw;
    h = ih;
    return true;
}

namespace {

// CRC-32 (ISO 3309, as PNG uses it) over `n` bytes, continuing from `crc` (start with 0).
uint32_t crc32(const unsigned char* p, size_t n, uint32_t crc = 0) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put_u32(std::string& s, uint32_t v) {  // big endian, as everything in PNG
    for (int sh = 24; sh >= 0; sh -= 8) s.push_back(char((v >> sh) & 0xFF));
}

// Appends one PNG chunk: length, type, data, CRC over type and data.
void put_chunk(std::string& out, const char* type, const std::string& data) {
    put_u32(out, uint32_t(data.size()));
    std::string body(type, 4);
    body += data;
    out += body;
    put_u32(out, crc32(reinterpret_cast<const unsigned char*>(body.data()), body.size()));
}

}  // namespace

bool export_png(const fs::path& to, const Image& img, const Palette& pal) {
    if (img.width <= 0 || img.height <= 0 || img.pixels.size() != size_t(img.width) * size_t(img.height)) return false;
    std::string ihdr;
    put_u32(ihdr, uint32_t(img.width));
    put_u32(ihdr, uint32_t(img.height));
    ihdr += std::string("\x08\x03\x00\x00\x00", 5);  // 8 bit, indexed colour, deflate, adaptive filtering, no interlace
    std::string plte;
    for (const auto& c : pal.rgb) plte.append({char(c[0]), char(c[1]), char(c[2])});
    // scan lines with filter type 0 (none) in front of each row
    std::vector<unsigned char> raw;
    raw.reserve((size_t(img.width) + 1) * size_t(img.height));
    for (int y = 0; y < img.height; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), img.pixels.begin() + ptrdiff_t(y) * img.width, img.pixels.begin() + ptrdiff_t(y + 1) * img.width);
    }
    int zlen = 0;
    unsigned char* z = stbi_zlib_compress(raw.data(), int(raw.size()), &zlen, 8);
    if (!z) return false;
    const std::string idat(reinterpret_cast<const char*>(z), size_t(zlen));
    std::free(z);  // stb_image_write allocates with malloc (STBIW_MALLOC default)
    std::string png("\x89PNG\r\n\x1a\n", 8);
    put_chunk(png, "IHDR", ihdr);
    put_chunk(png, "PLTE", plte);
    put_chunk(png, "IDAT", idat);
    put_chunk(png, "IEND", {});
    std::ofstream f(to, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(png.data(), std::streamsize(png.size()));
    return bool(f);
}

}  // namespace em::editor
