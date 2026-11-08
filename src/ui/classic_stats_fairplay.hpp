// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

#include <array>

namespace em::ui {

// Statistiken -> Fairplay (ovr028:0000 = ovl04_0000(1)): the twelve clubs of
// the active manager's league sorted by their assessed league penalty minutes
// (u16 at table row +0x4A, DS:82A1), with minutes per game. The page waits
// for one click (sub_19B08) and closes again.
std::array<int, 12> classic_stats_fairplay_order(const HokFile&);  // team indices
void draw_classic_stats_fairplay(ClassicCanvas&, const HokFile&);

}  // namespace em::ui
