// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "ui/classic_canvas.hpp"
#include <optional>
#include <vector>

// "Play-Off Spiele" page of the classic frontend: the Bundesliga play-off bracket (quarterfinals, semifinals,
// final) with the results of every game, opened from the middle button of the Bundesliga fixtures.

namespace em::ui {
// What a click on the play-off page does: nothing, open the club info of a team, or go back to the league.
enum class ClassicPlayoffAction { None, ClubInfo, Liga };
// Result of classic_playoff_hit: the action, the team (for ClubInfo), mode 0 = home / 1 = away column, the
// pairing index, and the hit rectangle.
struct ClassicPlayoffHit {
    ClassicPlayoffAction action = ClassicPlayoffAction::None;
    int team = -1, mode = -1, pair = -1;
    int left = 0, top = 0, right = -1, bottom = -1; // inclusive
};
// One game result of a pairing: x position, number of marker dots (a stored score of 30.. carries one dot,
// 60.. two dots; the stored value minus 30/60 is the goal count), text colour and the "hh:aa" text with '%'
// as blank digit.
struct ClassicPlayoffScore {
    int x = 0, markers = 0;
    uint8_t color = 1;
    std::string native;
};
// One pairing line: pairing index, home and away team index, text baseline, name colours (24 for teams of
// human managers) and the names in the game's byte encoding, followed by the played games.
struct ClassicPlayoffRow {
    int pair = 0, home = -1, away = -1, baseline = 0;
    uint8_t home_color = 30, away_color = 1;
    std::string native_home, native_away;
    std::vector<ClassicPlayoffScore> scores;
};
// A stage heading (VIERTELFINALE, HALBFINALE, ENDSPIEL) at its baseline.
struct ClassicPlayoffHeading { int baseline = 0; std::string native; };
// Everything the page shows, computed from the save state.
struct ClassicPlayoffView {
    int stage = 0, game = 0, cursor = 255;  // play-off stage (0x8F87), game counter (0x0259), list cursor (0x03B6)
    bool available = false;                 // the pairings exist (cursor != 255)
    int frame_top = 70, frame_rows = 5, pair_count = 1;  // frame position/height in tiles and listed pairings
    std::string native_title = "Play-Off Spiele", native_caption;  // title and the "d.m." caption
    std::vector<ClassicPlayoffHeading> headings;
    std::vector<ClassicPlayoffRow> rows;
    int preview_calendar_index = -1;        // calendar index of the next play-off day, -1 if none
    std::optional<GameDate> preview_date;   // date of that day (shown in the caption)
};

// Builds the page from the complete bracket and result history in the save state, including earlier stages.
// Pure: no Game object, random numbers, bookings, strength refresh, date change or screen restoration.
// Throws FormatError on malformed state.
ClassicPlayoffView classic_playoff_view(const HokFile&,const SeasonCalendar&);
// Hit test at (x, y): the "Liga" button or a team name of a pairing.
ClassicPlayoffHit classic_playoff_hit(const ClassicPlayoffView&,int x,int y);
// Paints the full page.
void draw_classic_playoff(ClassicCanvas&,const HokFile&,const SeasonCalendar&);
// Entry action of the modal on the controller's transient working copy of the save: writes the date of the
// next play-off day into the current date fields (0x8E21 day, 0x8E25 month - 1, 0x8E29 year, u32 each) and
// into h.date, as the original does even when the bracket is not available. Returns false if there is no
// such date.
bool prepare_classic_playoff_working(HokFile&,const SeasonCalendar&);
} // namespace em::ui
