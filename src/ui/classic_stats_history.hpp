// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

#include <array>

namespace em::ui {

// Statistiken -> Frueher: the "Ewige Tabelle" of all 42 clubs, sorted by the u32 all-time points (league
// table row +0x4C), and the all-time record of the active manager (manager block +0x13B..). The list is
// drawn into a 200x314 buffer; scroll 0..30 shows rows from scroll*6+2.
// The 42 team indices in table order (most points first).
std::array<int, 42> classic_stats_history_order(const HokFile&);
// Paints the page with the list scrolled to scroll (0..30); throws FormatError on other values.
void draw_classic_stats_history(ClassicCanvas&, const HokFile&, int scroll = 0);

// Scroll step of a left press at (x,y), as the original page reacts: +1, -1 or 0.
// The arrow buttons at (165,30) and (165,39) and the list margins both scroll.
int classic_stats_history_scroll(int x, int y);

}  // namespace em::ui
