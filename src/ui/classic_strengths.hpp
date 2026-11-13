// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"
#include <array>

// Stärken page of the classic frontend: the twelve teams of a league ranked by their strength values, with
// the form of their last eight games and their morale.

namespace em::ui {

// One team line of the page.
struct ClassicStrengthsRow {
    int team = 0;
    std::array<int, 4> values{}; // the three strength values (each clamped to 0..99) and the unclamped overall
    uint8_t color = 30;          // 24 for teams of human managers (including the national coach)
    std::array<uint8_t, 8> faces{}; // last eight results: 0 gap, 1 loss, 2 draw, 3 win
    uint8_t summary_face = 2;    // 2 + wins - losses, clamped to 1..3
    int morale = 0;              // team byte +0x17; 50 is neutral
};

// The rows in the original's order, with its cached values: league 0/1/2 or 4 (national teams), mode 0
// Gesamt, 1 Abwehr, 2 Sturm (which of the two stored values per strength are averaged; team bytes
// +0x18..+0x1D). Does not perform the strength refresh the original runs when the page is entered (the
// application does that through Game) and modifies no save bytes. Throws FormatError on an invalid league or mode.
std::array<ClassicStrengthsRow, 12> classic_strengths_rows(
    const HokFile&, int league = 0, int mode = 0);
// Paints the page.
void draw_classic_strengths(ClassicCanvas&, const HokFile&, int league = 0, int mode = 0);

} // namespace em::ui
