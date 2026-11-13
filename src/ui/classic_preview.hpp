// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/binary.hpp"
#include "ui/classic_canvas.hpp"
#include <string_view>

// Savegame preview of the classic Laden/Speichern page: the small box that shows year, matchday and the
// managers of a .HOK file, decoded from the file's unencrypted header.

namespace em::ui {

// One manager entry of the preview header (40 bytes each from offset 0x10).
struct ClassicPreviewRecord {
    uint8_t league = 0;      // +0: league 0 Bundesliga, 1 Zweite Liga, 2 Oberliga (4 = national teams)
    bool world_cup = false;  // +1: non-zero for a national coach; then no league is shown
    // +2 / +21: the game's own bytes, bounded at the first NUL or the 19-byte field end.
    // text_native preserves the original CP437 aliases and ISO646 glyphs.
    std::string manager;
    std::string club;
};

// The decoded preview header.
struct ClassicPreview {
    std::string version = "KRON_0.99";  // 0x00: magic, 10 bytes including the NUL
    int32_t year = 0;                   // 0x0A: season year, i32
    uint8_t matchday = 0;               // 0x0F: matchday
    std::vector<ClassicPreviewRecord> managers;  // 0x0E: count (0..4), records from 0x10
};

// Decodes the 0xB0-byte preview header, as the original's load page does. Accepts that prefix or a whole HOK,
// validates the version and manager count and ignores all inactive records. No encrypted state, marker, nonce,
// random number or checksum is consulted. Throws FormatError on a truncated or invalid header.
ClassicPreview decode_classic_preview(const Bytes& headerOrFile);

// Paints the original modal over the caller's already rendered page. The
// path is a DOS-style byte string supplied by the caller; it is never read from the HOK.
// The caller restores the backing page when the left or right mouse button is released.
void draw_classic_preview(ClassicCanvas& canvas, const ClassicPreview& preview,
                          std::string_view virtualDOSpath);

} // namespace em::ui
