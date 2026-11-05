// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/classic_canvas.hpp"
#include "game/finances.hpp"

namespace em::ui {

// Native Bank page, drawing only. selectedParty:0..3 human,4 bank,5..8
// shares. bankSelected means a party is actively selected; deposit means
// the right-hand operation (deposit/repay, or selling shares). amount is
// raw money for a bank/human party, or a count for a stock party.
void draw_classic_bank(ClassicCanvas& canvas, const HokFile& snapshot, const ManagerBudget& budget,
                       int selectedParty, bool bankSelected, int64_t amount, bool deposit, int manager = 0,bool showPrompt=true);

}  // namespace em::ui
