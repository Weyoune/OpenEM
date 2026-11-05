// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {
// Native5F7C:1F2B static tactics page; preset selects the highlighted slot.
// Values come from the snapshot's edited team fields, not from a preset copy.
void draw_classic_tactics(ClassicCanvas&, const HokFile&, int preset = 0, int manager = 0);
}
