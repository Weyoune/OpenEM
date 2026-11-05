// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {

enum class ClassicRosterView { Transfer, Contracts, Lineup };

// Native 59B5:1DED roster, with the contracts or left-hand transfer frame.
// Draws over the prepared background; no footer, hover state or input handling.
// Reads the selected manager's original 52-byte records from h.state.
void draw_classic_roster(ClassicCanvas& canvas, const HokFile& h,
                         bool contracts, int manager = 0);

// The lineup variant adds NR and penalties and uses native field maskF0C7.
// Draws its left frame only; the full page is draw_classic_lineup().
void draw_classic_roster(ClassicCanvas& canvas, const HokFile& h,
                         ClassicRosterView view, int manager = 0);

} // namespace em::ui
