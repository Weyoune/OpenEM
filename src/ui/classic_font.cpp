// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_font.hpp"

#include <algorithm>
#include <limits>

namespace em::ui {
namespace {
constexpr size_t kResidentFileBase = 0x1e00 + 0xbbf0;
constexpr size_t kFontTable = 0x6cd;
constexpr size_t kMapping = 0x6ba;
}

ClassicFont::ClassicFont(const Bytes& exe) {
    // Source layout and drawing algorithm: EM.EXE 1bbf:2214, 28f6, 2a31.
    if (exe.size() < kResidentFileBase + 0x2121 ||
        le16(exe, 0) != 0x5a4d || le16(exe, 8) * 16 != 0x1e00)
        throw FormatError("classic font: incompatible EM.EXE layout");
    const auto word = [&](size_t offset) { return le16(exe, kResidentFileBase + offset); };
    for (size_t c = 0; c < mapping_.size(); ++c) {
        uint8_t mapped = c < 32 || c >= 128 ? 32 : uint8_t(c);
        for (size_t n = 0; n < exe.at(kResidentFileBase + kMapping); ++n) {
            const auto p = kResidentFileBase + kMapping + 1 + n * 2;
            if (c >= 32 && c == exe.at(p)) { mapped = exe.at(p + 1); break; }
        }
        if (mapped < 32 || mapped >= 128) throw FormatError("classic font: invalid character mapping");
        mapping_[c] = uint8_t(mapped - 32);
    }
    for (size_t id = 0; id < faces_.size(); ++id) {
        auto& f = faces_[id];
        const size_t header = word(kFontTable + id * 2);
        f.metrics = {int(word(header + 2)), int(word(header + 4))};
        if (f.metrics.height < 1 || f.metrics.height > 32 || f.metrics.descender >= f.metrics.height)
            throw FormatError("classic font: invalid metrics");
        if (id == 0) continue; // Only a six-byte normal.font header is present.
        for (size_t c = 0; c < 96; ++c) {
            const size_t offset = word(header + 6 + c * 2);
            auto& g = f.glyphs[c];
            g.width = exe.at(kResidentFileBase + offset);
            if (g.width < 1 || g.width > 32) throw FormatError("classic font: invalid glyph width");
            const int stride = (g.width + 7) / 8;
            g.pixels.resize(size_t(g.width) * f.metrics.height);
            for (int y = 0; y < f.metrics.height; ++y)
                for (int x = 0; x < g.width; ++x)
                    g.pixels[size_t(y) * g.width + x] =
                        (exe.at(kResidentFileBase + offset + 1 + size_t(y) * stride + x / 8) &
                         (0x80 >> (x & 7))) != 0;
        }
        f.available = true;
    }
}

const ClassicFont::Face& ClassicFont::face(ClassicFontId id, bool require_bitmap) const {
    const auto index = static_cast<size_t>(id);
    if (index >= faces_.size()) throw FormatError("classic font: invalid font ID");
    const auto& f = faces_[index];
    if (require_bitmap && !f.available) throw FormatError("classic font: normal.font has no embedded glyph table");
    return f;
}

bool ClassicFont::available(ClassicFontId id) const { return face(id, false).available; }
ClassicFontMetrics ClassicFont::metrics(ClassicFontId id) const { return face(id, false).metrics; }

std::string ClassicFont::utf8_to_native(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        const auto c = uint8_t(text[i++]);
        if (c < 0x80) { out.push_back(char(c)); continue; }
        if (c == 0xc3 && i < text.size()) {
            const auto next = uint8_t(text[i]);
            uint8_t native = 0;
            switch (next) {
                case 0x84: native = 0x8e; break; // Ä
                case 0x96: native = 0x99; break; // Ö (raw backslash is remapped separately)
                case 0x9c: native = 0x9a; break; // Ü
                case 0xa4: native = 0x84; break;
                case 0xb6: native = 0x94; break;
                case 0xbc: native = 0x81; break;
                case 0x9f: native = 0xe1; break;
            }
            if (native) { out.push_back(char(native)); ++i; continue; }
        }
        // Unsupported code points occupy one original space, matching 2214's
        // unsupported-byte behavior; consume their UTF-8 continuation bytes.
        while (i < text.size() && (uint8_t(text[i]) & 0xc0) == 0x80) ++i;
        out.push_back(' ');
    }
    return out;
}

int ClassicFont::measure_native(ClassicFontId id, std::string_view native) const {
    const auto& f = face(id, true);
    int width = 0;
    for (const unsigned char c : native) {
        const int advance = f.glyphs[mapping_[c]].width;
        if (width > std::numeric_limits<int>::max() - advance) throw FormatError("classic font: text too wide");
        width += advance;
    }
    return width;
}
int ClassicFont::measure(ClassicFontId id, std::string_view utf8) const {
    return measure_native(id, utf8_to_native(utf8));
}

int ClassicFont::draw_native(Image& image, ClassicFontId id, int x, int baseline_y,
                             std::string_view native, uint8_t foreground,
                             bool opaque, uint8_t background) const {
    const auto& f = face(id, true);
    const int advance = measure_native(id, native);
    const int64_t top = int64_t(baseline_y) - f.metrics.baseline_offset();
    int64_t pen = x;
    for (const unsigned char c : native) {
        const auto& g = f.glyphs[mapping_[c]];
        for (int gy = 0; gy < f.metrics.height; ++gy) {
            const auto py = top + gy;
            if (py < 0 || py >= image.height) continue;
            for (int gx = 0; gx < g.width; ++gx) {
                const auto px = pen + gx;
                if (px < 0 || px >= image.width) continue;
                const uint8_t pixel = g.pixels[size_t(gy) * g.width + gx] ? foreground : background;
                // Native transparent blit 3893 skips palette index zero.
                if (opaque || (g.pixels[size_t(gy) * g.width + gx] && pixel != 0))
                    image.pixels.at(size_t(py) * image.width + size_t(px)) = pixel;
            }
        }
        pen += g.width; // Exact original advance: no added inter-glyph spacing.
    }
    return advance;
}

int ClassicFont::draw(Image& image, ClassicFontId id, int x, int baseline_y,
                      std::string_view utf8, uint8_t foreground, bool opaque, uint8_t background) const {
    return draw_native(image, id, x, baseline_y, utf8_to_native(utf8), foreground, opaque, background);
}
} // namespace em::ui
