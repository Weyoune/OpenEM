// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {

// Main-header fever curve, ovl10_1490 (5cb4:1490..18f8). The caller prepares
// its native beveled panel; coordinates are the original header coordinates.
// Opponent 254 means season end and 255 means no opponent, as in EM.EXE.
void draw_classic_ranking_graph(ClassicCanvas& canvas, const HokFile& h,
                                int ownTeam, int opponent, bool leagueEvent);

} // namespace em::ui
