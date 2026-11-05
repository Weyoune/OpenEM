// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/binary.hpp"
#include "ui/classic_canvas.hpp"
#include <string_view>

namespace em::ui {

struct ClassicPreviewRecord {
    uint8_t league = 0;
    bool world_cup = false;
    // Original DOS bytes, bounded at the first NUL or the 19-byte field end.
    // text_native preserves the original CP437 aliases and ISO646 glyphs.
    std::string manager;
    std::string club;
};

struct ClassicPreview {
    std::string version = "KRON_0.99";
    int32_t year = 0;
    uint8_t matchday = 0;
    std::vector<ClassicPreviewRecord> managers;
};

//7348:0E41 reads only the 0xB0-byte preview header. Accepts that prefix or a
// whole HOK, validates the version/count and ignores all inactive records.
// No encrypted state, marker, nonce, RNG or checksum is consulted.
ClassicPreview decode_classic_preview(const Bytes& headerOrFile);

// Paints the original modal over the caller's already rendered page. The
// external path is a DOS-native byte string; it is never read from the HOK.
// Caller restores the backing page on left/right mouse release (event2/4).
void draw_classic_preview(ClassicCanvas& canvas, const ClassicPreview& preview,
                          std::string_view virtualDOSpath);

} // namespace em::ui
