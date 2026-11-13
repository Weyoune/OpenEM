// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/classic_canvas.hpp"
#include "game/finances.hpp"
#include <array>
#include <string>

// Jugendteams page of the classic frontend: standings of the three youth teams (Schüler, Jugend, Junioren)
// as a graph and their funding as bars. Input in classic_youth_input.cpp.

namespace em::ui {
// One youth team.
struct ClassicYouthTeam {
    uint8_t rank = 0, league = 0; // zero-based place and league (save state 0x06BB + manager * 0x15, +0..2 / +3..5)
    int32_t raw_funding = 0, display_funding = 0, bar_width = 0;  // funding in internal units and in DM; bar length
    int graph_x = 0, graph_y = 0, caption_baseline = 0;           // marker position in the standings graph
    std::string native_caption, native_amount;                    // "n.Platz m.Liga" and the DM amount
};
// The three youth teams.
struct ClassicYouthView {
    std::array<ClassicYouthTeam, 3> teams{};
};

// Pure view of the original page. The six standings bytes and three funding amounts are read from the save;
// opening does not alter standings or use random numbers. Throws FormatError on an invalid manager or currency.
ClassicYouthView classic_youth_view(const HokFile& h, const FinanceTables& finance, int manager = 0);
// Paints the page.
void draw_classic_youth(ClassicCanvas& canvas, const HokFile& h, const FinanceTables& finance, int manager = 0);
} // namespace em::ui
