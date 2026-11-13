// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/finances.hpp"
#include "ui/classic_canvas.hpp"

// Transfer page of the classic frontend (drawing only; input in classic_transfer_input.cpp, hover help in
// classic_transfer_hover.cpp).

namespace em::ui {
// Draws the original transfer page: the manager's own squad on the left (classic_roster.hpp), the transfer
// list (up to twelve pool players with strengths, age, purchase price "(K)" and loan price "(L)") on the
// right, and the footer with balance, debts, assets and net worth plus the Talent button.
// Static page only; hover owner help, selections and deal dialogs are separate.
void draw_classic_transfer(ClassicCanvas&, const HokFile&, const ManagerBudget&, int manager = 0);
}
