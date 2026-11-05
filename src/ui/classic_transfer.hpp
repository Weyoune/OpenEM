// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/finances.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {
// Native5F7C:05F1 transfer page, with59B5:1DED own roster and03CC footer.
// Static page only; hover owner help, selections and deal dialogs are separate.
void draw_classic_transfer(ClassicCanvas&, const HokFile&, const ManagerBudget&, int manager = 0);
}
