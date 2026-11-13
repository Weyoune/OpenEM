// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/finances.hpp"

// Youth funding: the three monthly grants a manager pays to his youth teams (one per age group)
// and the slider logic of the youth screen that changes them. The grants live in the save state
// at 0x06C1 + manager * 0x15 + kind * 4 (i32 each, in internal currency units); the rest of that
// 0x15-byte youth block (quality, table rank, league per age group) is handled by
// youth_progress.hpp. Game::adjust_youth_funding (youth_actions.cpp) ties this to the game state.

namespace em {
// Slider scale of one youth grant, in display currency: each click moves it by `step`, and it
// can be raised up to `maximum`. Grant kind k (0..2) has step 500 << k and maximum 10000 << k.
struct YouthFundingScale {
    int32_t step;     // amount of one left/right click, display currency
    int32_t maximum;  // largest grant reachable by clicking/dragging, display currency
};
// The slider scale of grant `kind` (0..2). The currency tables are only validated (throws
// FormatError on an invalid kind or currency); the scale itself is currency independent.
YouthFundingScale youth_funding_scale(const FinanceTables&,int kind);
// The stored monthly grant `kind` (0..2) of `manager` (0..3), in internal currency units, read
// from state 0x06C1 + manager * 0x15 + kind * 4. Throws FormatError when out of range.
int32_t youth_funding_amount(const HokFile&,int kind,int manager=0);
// The new stored value of a grant after a click at screen column `x` on its slider row.
// `raw` is the current stored value (internal units); the computation runs in display currency:
//   x < 121    one step down (unchanged if the grant is already 0),
//   x > 190    one step up (unchanged if it has reached the maximum),
//   121..190   proportional: (x - 120) * maximum / 70, rounded down to a whole step.
// The result is converted back to internal units. All products wrap like the original's signed
// 32-bit arithmetic, so stored values that are not a multiple of the step behave as in the game.
// Returns `raw` itself when nothing changes.
int32_t youth_funding_target(int32_t raw,const FinanceTables&,int kind,int x);
// The grant row (0..2) hit by a click at screen row `y`, or -1 when the click is between or
// outside the rows. Row k covers y = 168 + 24k .. 177 + 24k.
int youth_funding_row(int y);
} // namespace em
