// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"
#include <array>

namespace em::ui {

struct ClassicStrengthsRow {
    int team = 0;
    std::array<int, 4> values{}; // Kondition, Technik, Form, unclamped overall
    uint8_t color = 30;
    std::array<uint8_t, 8> faces{}; // 0 gap, 1 loss, 2 draw, 3 win
    uint8_t summary_face = 2;
    int morale = 0;
};

// Native6668:3F16 sorting and cached values; league0/1/2/4, mode0/1/2.
// Does not perform the native146C entry refresh or modify any HOK bytes.
std::array<ClassicStrengthsRow, 12> classic_strengths_rows(
    const HokFile&, int league = 0, int mode = 0);
void draw_classic_strengths(ClassicCanvas&, const HokFile&, int league = 0, int mode = 0);

} // namespace em::ui
