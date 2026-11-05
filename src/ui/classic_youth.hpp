// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/classic_canvas.hpp"
#include "game/finances.hpp"
#include <array>
#include <string>

namespace em::ui {
struct ClassicYouthTeam {
    uint8_t rank = 0, league = 0; // original zero-based A078/A07B bytes
    int32_t raw_funding = 0, display_funding = 0, bar_width = 0;
    int graph_x = 0, graph_y = 0, caption_baseline = 0;
    std::string native_caption, native_amount;
};
struct ClassicYouthView {
    std::array<ClassicYouthTeam, 3> teams{};
};

// Pure native5396:087B/01ED view. The six standings bytes and three funding
// amounts are read from HOK; opening does not alter standings or draw RNG.
ClassicYouthView classic_youth_view(const HokFile& h, const FinanceTables& finance, int manager = 0);
void draw_classic_youth(ClassicCanvas& canvas, const HokFile& h, const FinanceTables& finance, int manager = 0);
} // namespace em::ui
