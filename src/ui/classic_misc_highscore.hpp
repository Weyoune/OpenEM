// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/highscore.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {
// Native Highscore page (ovr040:30ED): KB195 with its palette, "Highscore" in NewBold colour 14, then the eight
// records with a score: name (cut to at most 57 pixels) at x 56, club at 115, score (five places) at 207 and,
// outside the WM-only mode, "(championships/cups/European cups)" at 240; baselines 154 + 10 * row. Colour 10, shadow 4;
// colour 7 marks records whose identity equals DS:A9A3, the identity field of the native work record (DS:A96D), which
// the original never writes: so only records with identity 0 are marked (the merge stores the game identity).
void draw_classic_highscore(ClassicCanvas& canvas, const HighscoreTable& table, bool wm_only);
}  // namespace em::ui
