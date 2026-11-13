// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

#include <array>
#include <string>
#include <vector>

namespace em::ui {

// Statistiken -> Spieler: the best scorers (twelve skaters) and goalkeepers (three) of the active manager's
// league, or only of the human managers' clubs.
// mode: 0 TORSCH. (goals), 1 VORLEGER (assists), 2 SCORER (both; the original's start value).
// managers_only: the LIGA/MANAGER switch (the original starts with LIGA).

// One list row: player index, team, rank, text baseline, colour (24 for clubs of human managers) and texts.
struct ClassicStatsPlayerRow {
    int player = 0, team = 0, rank = 0, baseline = 0;
    uint8_t color = 30;
    std::string native_name, native_score, native_rate;
};
// The whole page.
struct ClassicStatsPlayersView {
    int mode = 2;
    bool managers_only = false;
    std::vector<ClassicStatsPlayerRow> skaters, keepers;  // at most 12 / 3
    std::string native_title, native_keeper_title;
    // The original draws "NOCH KEINE EINTR[GE..." only while the row variable it reuses is still 0x36: the
    // last eligible candidate was a skater and no skater row was shown. That variable starts with the last
    // human squad's player count. This quirk is reproduced.
    bool empty_notice = false;
};

// Read-only: the original first copies the human squads' league goals (squad +0x03) into the player records
// before it builds the list; here that is applied to a local copy. Throws FormatError on invalid input.
ClassicStatsPlayersView classic_stats_players_view(const HokFile&, int mode = 2, bool managers_only = false);
// Paints the page with the mode and LIGA/MANAGER buttons.
void draw_classic_stats_players(ClassicCanvas&, const HokFile&, int mode = 2, bool managers_only = false);

// Button hit test of the page: -1 none, 0..2 mode buttons, 3 LIGA, 4 MANAGER.
int classic_stats_players_button(int x, int y);
// Player row under (x,y): player index (1..199) or -1.
int classic_stats_players_row(const ClassicStatsPlayersView&, int x, int y);

}  // namespace em::ui
