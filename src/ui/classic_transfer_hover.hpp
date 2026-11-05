// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {

struct ClassicTransferHover {
    int row = -1;
    std::string native_help;
};

class ClassicTransferHelp {
public:
    explicit ClassicTransferHelp(const Bytes& exe);

private:
    std::string from_, country_;
    friend ClassicTransferHover classic_transfer_hover(const HokFile&, int,
        int, int, const ClassicTransferHelp&);
};

// Separate5F7C:0AB5 pool lookup, not the left roster's shared input helper.
// Uses raw manager4 records and the original one-based global player IDs.
ClassicTransferHover classic_transfer_hover(const HokFile& h, int manager,
    int x, int y, const ClassicTransferHelp& help);

// Valid row only: optional rules16 and owner-help clear/text. Invalid hover
// is a no-op: native leaving restores old rules31 but retains the owner help.
// App owns previous-row restoration; a pool click keeps the highlight rules.
void draw_classic_transfer_hover(ClassicCanvas& canvas,
    const ClassicTransferHover& hover, bool drawRules = true, bool drawHelp = true);

} // namespace em::ui
