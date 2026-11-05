// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/vga.hpp"
#include <array>
#include <string_view>

namespace em::ui {

// Original IDs in EM.EXE's 1bbf:06cd table. Normal has no bitmap table in
// the shipped executable and is deliberately unavailable instead of substituted.
enum class ClassicFontId { Normal = 0, Mini = 1, Flow = 2, NewBold = 3 };

struct ClassicFontMetrics {
    int height = 0;
    int descender = 0;
    int baseline_offset() const { return height - descender - 1; }
};

class ClassicFont {
public:
    explicit ClassicFont(const Bytes& original_exe);
    bool available(ClassicFontId id) const;
    ClassicFontMetrics metrics(ClassicFontId id) const;

    // Native means the original byte encoding, including CP437 aliases.
    // UTF-8 names from the database/save decoders use the unqualified methods.
    int measure(ClassicFontId id, std::string_view utf8) const;
    int measure_native(ClassicFontId id, std::string_view native) const;
    int draw(Image& image, ClassicFontId id, int x, int baseline_y,
             std::string_view utf8, uint8_t foreground,
             bool opaque = false, uint8_t background = 0) const;
    int draw_native(Image& image, ClassicFontId id, int x, int baseline_y,
                    std::string_view native, uint8_t foreground,
                    bool opaque = false, uint8_t background = 0) const;

    static std::string utf8_to_native(std::string_view utf8);

private:
    struct Glyph { int width = 0; std::vector<uint8_t> pixels; };
    struct Face { ClassicFontMetrics metrics; std::array<Glyph, 96> glyphs; bool available = false; };
    std::array<Face, 4> faces_;
    std::array<uint8_t, 256> mapping_{};
    const Face& face(ClassicFontId id, bool require_bitmap) const;
};

} // namespace em::ui
