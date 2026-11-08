// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Match display of the original (overlay ovr038 = ovl14), shown while a day is played minute by minute (ovl06_2522):
//   - one scoreboard per watched match (ovl14_16ac / ovl14_0d46), 45 lines apart from y = 4: matches of clubs whose
//     reporting mode is 2 ("Tore", team byte +0x34 bit 7, ovl14_2630), at most four;
//   - the minute panel bottom left (ovl14_0000: three period bars and the minute in KB38 digits), the reporter and the
//     message panel bottom right, where FUN_162c_23c4 (seg003:2AF4..2CB1) flashes every goal of a reported match without
//     a board (classic_goal_flashes);
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

// A match without a board whose goals the original flashes in the message panel (FUN_162c_23c4: a club with reporting
// mode 1 or 2, team byte +0x34 & 0xC0; mode 2 clubs beyond the four boards included).
struct ClassicReportedMatch {
    int home = -1, away = -1;
    std::string names[2];  // UTF-8
    int goals[2] = {};     // goals from chances (without shoot-out goals)
};

// The goal flash of FUN_162c_23c4 over the message panel: home name centred on x 0xF5 (y 0xC0), "-" (DS:495C) centred on
// 0x127, away name centred on 0x10D (y 0xC7), all font 3 colour 1 with black shadow, a colon of two short diagonals; after
// delay(50) the score in KB38 digits at (0xEF, 0xCC) (ovl14_0300; the home goals reduced below 30), then delay(70) and the
// panel is restored from the background page (2115:4675 from 0x4B00).
struct ClassicGoalFlash {
    std::string home, away;  // UTF-8
    int goals[2] = {};       // score after the goal
    bool score = false;      // second stage: the digits are drawn
};

struct ClassicMatchFrame {
    int minute = 1;                   // shown minute (1..70); before minute 1 the original shows 1
    std::vector<ClassicBoard> boards; // empty: "Spiele laufen"
    std::vector<ClassicReportedMatch> reported;  // reported matches without a board (goal flashes)
    std::optional<ClassicGoalFlash> flash;       // a goal flash drawn over this frame
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
// How long a frame stays: a minute (classic_match_minute_ms), or a goal flash stage independent of the speed:
// delay(50) = 16 BIOS ticks with the team names, delay(70) = 23 ticks with the score.
int classic_match_frame_ms(const ClassicMatchFrame& frame, int speed);

// Inserts the goal flashes of FUN_162c_23c4 into the observed minutes: every goal of a reported match without a board that
// falls in minute m becomes two frames (names, then names and score) shown before the frame of minute m. They are drawn over
// the previous minute's screen with the minute display already at m (ovl14_0000 runs before the chances, ovl14_0d46 after
// them). Home goals of a minute come first (the minute loop asks the home side first). The original only flashes while a
// board is shown (DS:5A56 stays 0 with "Spiele laufen"; DOSBox probe 08.11.2026). Cup table frames are left as they are.
std::vector<ClassicMatchFrame> classic_goal_flashes(std::vector<ClassicMatchFrame> frames);

// Builds the frame of one observed minute (season.cpp / competitions.cpp observer) from the day's matches. Boards follow
// ovl14_2630: every match in pair order where a club has reporting mode 2, at most four; the attendance of the human home
// game is passed in (Game::match_attendance). The other matches with a reporting club are listed in `reported`.
ClassicMatchFrame classic_match_frame(const Game& game, int minute, const std::vector<LiveMatch>& matches,
                                      long attendance, bool sold_out);

// Whether the original shows the minute display for this day (ovl06_161d): always when the human club plays, otherwise only
// with the display switch of the competition (Ergebnisse of any league / DEB-Pokal).
bool classic_match_display_wanted(const Game& game, bool human_plays, bool cup);

}  // namespace em::ui
