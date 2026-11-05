// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/classic_canvas.hpp"
#include "game/finances.hpp"

namespace em::ui {

// Original Totale page (4495:33CE), drawing only. The budget/tables must
// describe the supplied exported snapshot and human manager (0..3).
void draw_classic_totals(ClassicCanvas& canvas, const HokFile& snapshot,
                         const ManagerBudget& budget, const FinanceTables& tables, int manager = 0);

}  // namespace em::ui
