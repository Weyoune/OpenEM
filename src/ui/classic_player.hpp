// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"
#include <optional>

// Player detail box of the classic frontend: the modal opened by a right click on a roster or transfer list.

namespace em::ui {

// Paints the player detail modal (name, age, caps, strength, face, goals/games per competition, prices and
// contract, and the four Unterzahl/Überzahl/Tendenz/Erschöpfung scales) over the existing page; the caller
// preserves/restores that page and handles the "Aha !" button. player is the decoded h.players index; slot < 0
// finds the player's roster slot (1-based player id in squad byte +15) in the chosen manager's squad. A supplied
// slot must refer to that player. rawMarketQuote is a freshly assessed market value in internal units (before
// the division by 3 into DM); it is required for players whose market value the game draws at random (squad
// flag 0x80 with byte +0x16 <= 63), because this renderer never uses the random generator. Throws FormatError on
// invalid indices or when such a quote is missing.
void draw_classic_player(ClassicCanvas& canvas, const HokFile& h, int player,
                         int manager = 0, int slot = -1,
                         std::optional<int64_t> rawMarketQuote = std::nullopt);

} // namespace em::ui
