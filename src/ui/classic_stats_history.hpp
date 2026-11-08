// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

#include <array>

namespace em::ui {

// Statistiken -> Frueher (ovr028:4176 = ovl04_4176): the "Ewige Tabelle" of
// all 42 clubs, sorted by the u32 all-time points (table row +0x4C), and the
// "Ewige Bilanz" of the active manager (manager block +0x13B..). The list is
// drawn into a 200x314 buffer; scroll 0..30 shows rows from scroll*6+2.
std::array<int, 42> classic_stats_history_order(const HokFile&);
void draw_classic_stats_history(ClassicCanvas&, const HokFile&, int scroll = 0);

// Native scroll step of a left press at (x,y) (4495:4950..49D2): +1, -1 or 0.
// The arrow buttons at (165,30) and (165,39) and the list margins both scroll.
int classic_stats_history_scroll(int x, int y);

}  // namespace em::ui
