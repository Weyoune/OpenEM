// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {

// Statistiken -> Verein: EuroCup, DEB-Pokal and league history of the active manager as bar graphs, one
// column per career year (manager block +0x18, four bytes per year: place, league, cup round, European cup
// phase; at most 50 years), and the details of the selected year. The career year is the u16 at save state
// 0xB951; 0 shows the "Im ersten Jahr ..." notice instead.

// One manager history row as text: "10. Platz der Bundesliga", "Deutscher Meister" or "Play-Off HALBFINALE";
// also the first line of the season review (classic_season_review.cpp). place: bits 0..6 the table position
// (team index, below 0x32) or a play-off code 0x33..0x36, bit 7 champion. width pads the number with '%'.
std::string classic_history_place(int place, int league, int width = 1);
int classic_stats_club_years(const HokFile&);  // min(career year, 50)
// selected: 0..years-1; -1 selects the original's start value (the last year).
void draw_classic_stats_club(ClassicCanvas&, const HokFile&, int selected = -1);
// Arrow buttons at (242,208)/(242,217): 1 upper, -1 lower, 0 none.
int classic_stats_club_arrow(int x, int y);

}  // namespace em::ui
