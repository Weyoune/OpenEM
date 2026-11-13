// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

// Taktik page of the classic frontend (drawing only; input in classic_tactics_input.cpp).

namespace em::ui {
// Draws the static tactics page as the original does: the four mode controls (line-up AUTOM./MANUELL, line
// change mode, effort and offence mode), the per-line minute quotas, the effort and offence values for the
// six game situations, and the four preset buttons; preset (0..3) selects the highlighted one. Values come
// from the snapshot's edited team fields, not from a preset copy. Throws FormatError on invalid input.
void draw_classic_tactics(ClassicCanvas&, const HokFile&, int preset = 0, int manager = 0);
}
