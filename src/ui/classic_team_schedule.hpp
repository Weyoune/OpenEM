// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "game/schedule.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <optional>

// Team schedule ("Spielplan") of the club info box: the 22 league rounds of one half of the season for a
// domestic club, with opponent, its place and strength, and the result or date of both meetings.

namespace em::ui {

// One meeting: league round, calendar index of the day (unplayed only), whether it is played and at home,
// colour (30 home, 1 away) and the text "own:other" or "?:dd.mm.".
struct ClassicTeamScheduleCell {
    int round = 0, calendar_index = -1;
    bool played = false, home = false;
    uint8_t color = 30;
    std::string native;
};
// One opponent line: opponent team, baseline, colour, name, "place. (strength)" and the two meetings.
struct ClassicTeamScheduleRow {
    int opponent = -1, baseline = 113;
    uint8_t color = 30;
    std::string native_name, native_place_strength;
    std::array<ClassicTeamScheduleCell,2> cells;
};
// The whole box.
struct ClassicTeamSchedule {
    bool available = false;          // false when the league has not started (next round 0)
    int league = 0, half = 0;        // league 0..2 and the half shown (0 rounds 1..22, 1 rounds 23..44)
    std::string native_round_button; // label of the button that switches to the other half
    std::array<ClassicTeamScheduleRow,11> rows;
    // Pairings of the last round looked up and the date of the last unplayed meeting: the original leaves
    // both behind in its working state (see prepare_classic_team_schedule_working).
    std::array<HokPairing,6> last_pairings{};
    std::optional<GameDate> last_preview_date;
};

// The team schedule of team (domestic team index 0..51). half -1 selects the original's initial page from
// the next-round byte (save state 0x8F80 + league); explicit halves are 0/1. These two functions are pure:
// no change to the save, its date or pairings, and no random numbers.
ClassicTeamSchedule classic_team_schedule(const HokFile& h, const ScheduleTables& tables,
    const SeasonCalendar& calendar, int team, int half = -1);
void draw_classic_team_schedule(ClassicCanvas& canvas, const HokFile& h,
    const ScheduleTables& tables, const SeasonCalendar& calendar, int team, int half = -1);

// Explicit changes to the UI's transient working copy of the save, reproducing what the original leaves in
// its state; never run on the game itself. Prepare after opening or changing page: writes the last looked-up
// pairings (0x8DFD + league * 12) and the last preview date (0x8E21..). Finish restores only the current
// pairings, keeping the preview date until the return to the main menu. False means the league's next round
// is 0: the original returns immediately without changes.
bool prepare_classic_team_schedule_working(HokFile& working, const ScheduleTables& tables,
    const SeasonCalendar& calendar, int team, int half = -1);
bool finish_classic_team_schedule_working(HokFile& working, const ScheduleTables& tables, int team);
// On the return to the main menu the original recomputes the displayed date from the current season day;
// this does the same on the working copy.
void normalize_classic_team_schedule_main_return(HokFile& working);

} // namespace em::ui
