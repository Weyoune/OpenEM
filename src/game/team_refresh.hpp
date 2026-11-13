// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Refresh of a manager's team record when a menu shows team strengths: the number of players on
// the ice per line and the team's skill/form averages are recomputed from the current lineup.
#include "game/match.hpp"

namespace em {
// Skaters of the current defence and attack line (defaults for a team without manager).
struct TeamRefreshCounts { int defence=2,attack=3; };
// Recomputes the team record of `manager`'s club (0..5) as the original does before showing team
// strengths: for a manager's club it counts the goalkeeper and the players of the current lines
// (reduced by running penalties so that at most 6 - or 5 without goalkeeper - are on the ice),
// updates skaters and the 6-skater flag (+0x2C bit 3), and resets the defence line when there is no
// goalkeeper; then skill 1, skill 2 and form of both kinds become the mean of lines 0..2. The new
// values are written to team record +0x18..+0x1D (and +0x2C, +0x2E, +0x32 for a manager's club).
// Like the original, rows with player ID 0 inside the occupied prefix are not skipped; they use
// the dummy record before player 1. No automatic selection, fatigue booking or random draws.
TeamRefreshCounts refresh_menu_team(HokFile&,int manager);
} // namespace em
