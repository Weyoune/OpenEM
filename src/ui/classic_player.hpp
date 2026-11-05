// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"
#include <optional>

namespace em::ui {

// Original roster right-click detail,1238:002C. Paints the modal over the
// existing page; caller preserves/restores that page and handles Aha! input.
// player is the decoded h.players index; slot<0 finds its native roster ID
// in the chosen manager's records. A supplied slot must refer to that player.
// rawMarketQuote is native freshly assessed value (before DM conversion),
// required for flag80/open16<=63 players. This renderer never draws RNG.
void draw_classic_player(ClassicCanvas& canvas, const HokFile& h, int player,
                         int manager = 0, int slot = -1,
                         std::optional<int64_t> rawMarketQuote = std::nullopt);

} // namespace em::ui
