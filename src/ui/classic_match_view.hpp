// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Match display of the original (overlay ovr038 = ovl14), shown while a day is played minute by minute (ovl06_2522):
//   - one scoreboard per watched match (ovl14_16ac / ovl14_0d46), 45 lines apart from y = 4: matches of clubs whose
//     reporting mode is 2 ("Tore", team byte +0x34 bit 7, ovl14_2630), at most four;
//   - the minute panel bottom left (ovl14_0000: three period bars and the minute in KB38 digits), the reporter and the
//     message panel bottom right (FUN_162c_23c4 flashes goals of reported matches without a board there; not built yet);
//   - without any board but with a display switch on: "Spiele laufen" and the minute panel (ovr030:2642).
// The frames are snapshots of the day computed by Session::advance_day; drawing never changes the game.
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "ui/classic_canvas.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace em {
class Game;
struct LiveMatch;
}

namespace em::ui {

struct ClassicBoardTeam {
    std::string name;   // UTF-8
    int crest = 143;    // KB80 + (team +0x34 & 63), 143 for the national teams
    int line[2] = {};   // +0x2E / +0x30: lines on the ice (defence, attack)
    int energy[2] = {}; // +0x1E: energy of the line on the ice per kind
    int skaters = 5;    // +0x32
    int penalty_code = 0;  // +0x33: low nibble first penalty, high nibble second (minutes left & 7)
    int effort = 0;     // +0x26
    int offence = 0;    // +0x2C bits 4..6
    int chances[3] = {};   // per period, shown up to 9
    int penalty_minutes = 0;
};

struct ClassicBoard {
    std::array<ClassicBoardTeam, 2> team;
    int goals[2] = {};          // shown modulo 30 (the original marks decisions with +30)
    int period_goals[3][2] = {};
    long attendance = 0;        // home game of a manager, 0 = "(ausw.)"
    bool sold_out = false;      // attendance equals the capacity: colour 0x10
};

// Live result of one pair of a cup round for the table after minute 60 / 65 / 70 (ovl07_0eba).
struct ClassicCupLive {
    int home = -1, away = -1;
    int goals[2] = {};
    int markers = 0;  // 1 after the regular time of a drawn game (ovl05_3573 +30), 2 after a shoot-out
};

struct ClassicMatchFrame {
    int minute = 1;                   // shown minute (1..70); before minute 1 the original shows 1
    std::vector<ClassicBoard> boards; // empty: "Spiele laufen"
    bool settings_lamp = false;       // DS:878A, the switch bottom left (opens the Einstellungen)
    bool blink = false;               // ovl14_045c: reporter eyes closed (KB38 0x80) instead of open (KB38 0x50)
    int cup_table = 0;                // 60 / 65 / 70: the DEB-Pokal table of the round, waits for a click
    std::vector<ClassicCupLive> cup;  // live results for cup_table
};

void draw_classic_match_view(ClassicCanvas& canvas, const ClassicMatchFrame& frame);

// ovl07_0eba with the DEB-Pokal (after ovl06_2522 minutes 41-60, 61-65, 66-70): the page of Tabellen -> DEB-Pokal
// (draw_classic_cup, 4996:38F3) with the caption "<round>   <minute>.Minute" and the running game as the newest score column.
// before_day is the state before the day (draw, game index, earlier games of the series).
void draw_classic_cup_minute(ClassicCanvas& canvas, const HokFile& before_day, const SeasonCalendar& calendar, int minute,
                             const std::vector<ClassicCupLive>& live);
// The live results of all pairs of a cup round after a minute (observer of play_cup_round_live).
std::vector<ClassicCupLive> classic_cup_live(int minute, const std::vector<LiveMatch>& matches);

// ovl06_2522 delay per minute while the display runs: 30 (no button held) or 45 ticks of FUN_2115_019e, minus speed / 2;
// none from speed 70. One FUN_2115_019e unit is a third of a BIOS tick.
int classic_match_minute_ms(int speed);

// Builds the frame of one observed minute (season.cpp / competitions.cpp observer) from the day's matches. Boards follow
// ovl14_2630: every match in pair order where a club has reporting mode 2, at most four; the attendance of the human home
// game is passed in (Game::match_attendance).
ClassicMatchFrame classic_match_frame(const Game& game, int minute, const std::vector<LiveMatch>& matches,
                                      long attendance, bool sold_out);

// Whether the original shows the minute display for this day (ovl06_161d): always when the human club plays, otherwise only
// with the display switch of the competition (Ergebnisse of any league / DEB-Pokal).
bool classic_match_display_wanted(const Game& game, bool human_plays, bool cup);

}  // namespace em::ui
