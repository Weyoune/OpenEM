// SPDX-License-Identifier: GPL-3.0-only
// Classic-mode Bank page (loans, deposits, transfers between human managers
// and share trading). This header only exposes the renderer; the input
// handling lives in the classic UI controller.
#pragma once

#include "ui/classic_canvas.hpp"
#include "game/finances.hpp"

namespace em::ui {

// Draws the Bank page exactly as the original game lays it out; it does not
// modify any state.
//   snapshot      current save state (balances, share holdings, prices)
//   budget        the manager's budget figures shown on the page
//   selectedParty 0..3 = a human manager, 4 = the bank, 5..8 = a share
//   bankSelected  true while a party is actively selected for an operation
//   amount        money (raw save-state units) for a bank/human party, or a
//                 number of shares for a stock party
//   deposit       true for the right-hand operation (deposit/repay, or sell
//                 shares), false for the left-hand one (withdraw/borrow, buy)
//   manager       index of the manager whose page is shown
//   showPrompt    whether the amount-entry prompt is drawn
void draw_classic_bank(ClassicCanvas& canvas, const HokFile& snapshot, const ManagerBudget& budget,
                       int selectedParty, bool bankSelected, int64_t amount, bool deposit, int manager = 0,bool showPrompt=true);

}  // namespace em::ui
