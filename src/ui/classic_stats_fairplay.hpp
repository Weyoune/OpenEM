// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

#include <array>

namespace em::ui {

// Statistiken -> Fairplay: the twelve clubs of the active manager's league sorted by their assessed league
// penalty minutes (u16 at league table row +0x4A; table at save state 0x3BA5, 0x50 bytes per team), with
// minutes per game. The page waits for one click and closes again.
// The twelve team indices in display order (fewest penalty minutes first).
std::array<int, 12> classic_stats_fairplay_order(const HokFile&);
// Paints the page.
void draw_classic_stats_fairplay(ClassicCanvas&, const HokFile&);

}  // namespace em::ui
