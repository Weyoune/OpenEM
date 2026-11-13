// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {

// The league-position "fever curve" of the main-menu header.
//
// Draws the curve of ownTeam (and of opponent, if it is a domestic club
// below 52) over the rounds played, the rank labels at the curve ends and,
// with leagueEvent during the regular season, the "<n>.Spieltag" caption.
// Before the first matchday it prints "Vor dem ersten Spieltag." instead
// (except at season end). The caller draws the bevelled panel; coordinates
// are those of the original header. Opponent 254 means season end and 255
// (or -1) no opponent, as in the game. Nothing is drawn for national teams.
// Throws FormatError if the position history is truncated.
void draw_classic_ranking_graph(ClassicCanvas& canvas, const HokFile& h,
                                int ownTeam, int opponent, bool leagueEvent);

} // namespace em::ui
