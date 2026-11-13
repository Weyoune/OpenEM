// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/vga.hpp"
#include <array>
#include <string_view>

// The original game's bitmap fonts, read from the user's EM.EXE.
namespace em::ui {

// Font numbers as used by the game's font table in EM.EXE. Normal has no
// bitmap table in the shipped executable and is deliberately unavailable
// instead of being substituted.
enum class ClassicFontId { Normal = 0, Mini = 1, Flow = 2, NewBold = 3 };

// Vertical metrics of a font, in pixels.
struct ClassicFontMetrics {
    int height = 0;     // glyph cell height (1..32)
    int descender = 0;  // rows below the baseline
    // Distance from the top of the cell to the baseline row.
    int baseline_offset() const { return height - descender - 1; }
};

// The four fonts plus the game's character substitution table.
class ClassicFont {
public:
    // Parses the fonts from the bytes of EM.EXE; throws FormatError if the
    // executable has an unexpected layout.
    explicit ClassicFont(const Bytes& original_exe);
    // False only for Normal, which has no bitmaps.
    bool available(ClassicFontId id) const;
    ClassicFontMetrics metrics(ClassicFontId id) const;

    // "Native" means the game's own byte encoding (code page 437 umlauts
    // and the game's ASCII aliases such as '{' for ä). UTF-8 strings from
    // the database/save decoders use the unqualified methods.
    // measure*() return the advance width in pixels.
    int measure(ClassicFontId id, std::string_view utf8) const;
    int measure_native(ClassicFontId id, std::string_view native) const;
    // Draws text with its baseline at baseline_y, clipped to the image.
    // Glyph pixels get foreground; with opaque=true the rest of each glyph
    // cell gets background, otherwise it is left untouched (and foreground
    // 0 draws nothing, since 0 is the transparent index). Returns the
    // advance width. Throws for the bitmap-less Normal font.
    int draw(Image& image, ClassicFontId id, int x, int baseline_y,
             std::string_view utf8, uint8_t foreground,
             bool opaque = false, uint8_t background = 0) const;
    int draw_native(Image& image, ClassicFontId id, int x, int baseline_y,
                    std::string_view native, uint8_t foreground,
                    bool opaque = false, uint8_t background = 0) const;

    // Converts UTF-8 to the game's encoding (see the .cpp).
    static std::string utf8_to_native(std::string_view utf8);

private:
    // One character: width in pixels and width*height coverage flags.
    struct Glyph { int width = 0; std::vector<uint8_t> pixels; };
    // One font: metrics and glyphs for characters 32..127.
    struct Face { ClassicFontMetrics metrics; std::array<Glyph, 96> glyphs; bool available = false; };
    std::array<Face, 4> faces_;
    std::array<uint8_t, 256> mapping_{};  // input byte -> glyph index (0..95)
    const Face& face(ClassicFontId id, bool require_bitmap) const;
};

} // namespace em::ui
