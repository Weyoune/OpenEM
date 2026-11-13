// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

// Hover help of the transfer list: highlights the pool row under the mouse and names the player's owner
// ("from <club>" or the listing manager) in the help line.

namespace em::ui {

// The hovered pool row (-1 none) and its help text in the game's byte encoding.
struct ClassicTransferHover {
    int row = -1;
    std::string native_help;
};

// The two help-text prefixes, read from the user's EM.EXE.
class ClassicTransferHelp {
public:
    // Reads the prefixes through two far pointers in the EM.EXE data segment; throws FormatError if the file
    // does not have the expected layout.
    explicit ClassicTransferHelp(const Bytes& exe);

private:
    std::string from_, country_;  // prefix for club players, prefix for national players (id > 179)
    friend ClassicTransferHover classic_transfer_hover(const HokFile&, int,
        int, int, const ClassicTransferHelp&);
};

// The pool row under (x, y) and its owner help, the way the original transfer list looks it up (its own pool
// lookup, not the left roster's shared input helper). Uses the transfer pool (squad 4) and the original
// one-based global player ids.
ClassicTransferHover classic_transfer_hover(const HokFile& h, int manager,
    int x, int y, const ClassicTransferHelp& help);

// Valid row only: optional highlight rules (colour 16) and owner-help clear/text. An invalid hover is a
// no-op: when the pointer leaves, the original restores the old rules (colour 31) but retains the owner help.
// The application restores the previous row; a pool click keeps the highlight rules.
void draw_classic_transfer_hover(ClassicCanvas& canvas,
    const ClassicTransferHover& hover, bool drawRules = true, bool drawHelp = true);

} // namespace em::ui
