// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

#include <array>
#include <string>
#include <vector>

namespace em::ui {

// Statistiken -> Spieler (ovr025:0000 = ovl01_0000(0), stub 2480:0000).
// mode: 0 TORSCH., 1 VORLEGER, 2 SCORER (native start value 2).
// managers_only: the LIGA/MANAGER switch (native start value LIGA).
struct ClassicStatsPlayerRow {
    int player = 0, team = 0, rank = 0, baseline = 0;
    uint8_t color = 30;
    std::string native_name, native_score, native_rate;
};
struct ClassicStatsPlayersView {
    int mode = 2;
    bool managers_only = false;
    std::vector<ClassicStatsPlayerRow> skaters, keepers;  // at most 12 / 3
    std::string native_title, native_keeper_title;
    // 4080:0B9C draws "NOCH KEINE EINTR[GE..." only while the reused local row
    // variable is still 0x36: the last eligible candidate was a skater and no
    // skater row was shown. It starts with the last human squad's player count.
    bool empty_notice = false;
};

// Read-only: the native display first copies the human squads' league goals
// (squad +0x03) into the player records; that is applied to a local copy.
ClassicStatsPlayersView classic_stats_players_view(const HokFile&, int mode = 2, bool managers_only = false);
void draw_classic_stats_players(ClassicCanvas&, const HokFile&, int mode = 2, bool managers_only = false);

// Button hit test of the page: -1 none, 0..2 mode buttons, 3 LIGA, 4 MANAGER.
int classic_stats_players_button(int x, int y);
// Player row under (x,y): native player index (1..199) or -1.
int classic_stats_players_row(const ClassicStatsPlayersView&, int x, int y);

}  // namespace em::ui
