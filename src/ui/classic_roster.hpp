// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

// Squad list ("Kader") of the classic frontend, shared by the transfer page (left frame), the contracts page
// and the line-up page.

namespace em::ui {

// Which page the list is drawn for; the columns and row colours differ.
enum class ClassicRosterView { Transfer, Contracts, Lineup };

// Draws the squad list in the contracts frame or the left-hand transfer frame, over the prepared background;
// no footer, hover state or input handling. Reads the selected manager's 52-byte squad records from h.state.
// Throws FormatError on an invalid manager or player id.
void draw_classic_roster(ClassicCanvas& canvas, const HokFile& h,
                         bool contracts, int manager = 0);

// General form. The line-up variant adds the NR column and the penalty minutes and keeps the plain role
// colours. It draws its left frame only; the full page is draw_classic_lineup().
void draw_classic_roster(ClassicCanvas& canvas, const HokFile& h,
                         ClassicRosterView view, int manager = 0);

} // namespace em::ui
