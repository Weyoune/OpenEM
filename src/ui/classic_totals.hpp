// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/classic_canvas.hpp"
#include "game/finances.hpp"

// Totale page of the classic frontend: series, power-play figures, attendance, finances and monthly income
// and expenses of the manager's club.

namespace em::ui {

// Draws the original Totale page (drawing only). The budget/tables must describe the supplied exported
// snapshot and human manager (0..3). Throws FormatError on invalid input. Sets the 13 gradient colours of
// the palette (green to red) as the original does.
void draw_classic_totals(ClassicCanvas& canvas, const HokFile& snapshot,
                         const ManagerBudget& budget, const FinanceTables& tables, int manager = 0);

}  // namespace em::ui
