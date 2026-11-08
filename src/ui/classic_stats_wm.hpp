// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

#include <array>

namespace em::ui {

// Statistiken -> WM (ovr031:1411 = ovl07_1411): the last 16 world
// championship placings (table row +0x34.. of the twelve national teams
// 52..63, 0 ends the list) of places 1..8 as flag grid, the "eternal" WM
// ranking by 9 - place points, and the twelve national team boxes.
struct ClassicStatsWmView {
    std::array<int, 12> score{};
    std::array<int, 12> order{};  // national index 0..11, best first
};
ClassicStatsWmView classic_stats_wm_view(const HokFile&);
void draw_classic_stats_wm(ClassicCanvas&, const HokFile&);
// National team box under (x,y) (5396:1A87): team index 52..63 or -1.
int classic_stats_wm_team(int x, int y);

}  // namespace em::ui
