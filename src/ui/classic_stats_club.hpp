// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {

// Statistiken -> Verein (ovr028:200E = ovl04_200e): EuroCup, DEB-Pokal and
// league history of the active manager as bar graphs, one column per career
// year (manager +0x18, four bytes per year, at most 50), and the details of
// the selected year. Career year DS:03BD = HOK 0xB951; 0 shows the
// "Im ersten Jahr ..." notice instead.
int classic_stats_club_years(const HokFile&);  // min(career year, 50)
// selected: 0..years-1; -1 selects the native start value (the last year).
void draw_classic_stats_club(ClassicCanvas&, const HokFile&, int selected = -1);
// Arrow buttons at (242,208)/(242,217) (4495:259E): 1 upper, -1 lower, 0 none.
int classic_stats_club_arrow(int x, int y);

}  // namespace em::ui
