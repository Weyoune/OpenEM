// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"
#include <array>

namespace em::ui {

// Native transient diagram:0 empty,otherwise original squad slot+1.
// Rows0..3 are outfield blocks; row4/cell4 is TW. Cells0..2 Sturm,3..5 Abwehr.
using ClassicLineupCells = std::array<std::array<int, 6>, 5>;

// Reconstruct the original menu-entry order; keep the returned map in the
// frontend across clicks instead of reconstructing after each mutation.
ClassicLineupCells classic_lineup_cells(const HokFile& h, int manager = 0);

// Original Mannschaft -> Aufst., 59B5:0384, ordinary menu mode0.
// selectedLine:0 none (native initial state),1..4 outfield blocks,5 goalkeeper.
// Paints the complete page on wallpaper22; never changes the saved lineup.
void draw_classic_lineup(ClassicCanvas& canvas, const HokFile& h,
                         int manager = 0, int selectedLine = 0,
                         const ClassicLineupCells* override = nullptr,const bool* six_override = nullptr);

} // namespace em::ui
