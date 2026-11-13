// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"
#include <array>

// Classic line-up page: diagram model and renderer.
namespace em::ui {

// The game's transient line-up diagram: 0 = empty, otherwise squad slot + 1.
// Rows 0..3 are the outfield blocks; row 4 / cell 4 is the goalkeeper (TW).
// Cells 0..2 are forwards (Sturm), 3..5 defenders (Abwehr); cell 5 of block 1
// is used only with six field players.
using ClassicLineupCells = std::array<std::array<int, 6>, 5>;

// Builds the diagram in the order the game fills it when the page is
// opened. The frontend keeps the returned map across clicks and updates it
// itself instead of rebuilding it after each change, as the game does with
// its own diagram.
ClassicLineupCells classic_lineup_cells(const HokFile& h, int manager = 0);

// Draws the line-up page ("Mannschaft" -> "Aufst.") in the ordinary menu.
// selectedLine: 0 none (the initial state), 1..4 outfield blocks,
// 5 goalkeeper. override replaces the diagram built from the save state,
// six_override the six-field-player mode. Paints the complete page on the
// ice wallpaper (KB/22); never changes the saved line-up.
void draw_classic_lineup(ClassicCanvas& canvas, const HokFile& h,
                         int manager = 0, int selectedLine = 0,
                         const ClassicLineupCells* override = nullptr,const bool* six_override = nullptr);

} // namespace em::ui
