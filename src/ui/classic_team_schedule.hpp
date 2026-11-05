// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "game/schedule.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <optional>

namespace em::ui {

struct ClassicTeamScheduleCell {
    int round = 0, calendar_index = -1;
    bool played = false, home = false;
    uint8_t color = 30;
    std::string native;
};
struct ClassicTeamScheduleRow {
    int opponent = -1, baseline = 113;
    uint8_t color = 30;
    std::string native_name, native_place_strength;
    std::array<ClassicTeamScheduleCell,2> cells;
};
struct ClassicTeamSchedule {
    bool available = false;
    int league = 0, half = 0;
    std::string native_round_button;
    std::array<ClassicTeamScheduleRow,11> rows;
    std::array<HokPairing,6> last_pairings{};
    std::optional<GameDate> last_preview_date;
};

// Original1238:1B20(team,0,0), domestic team IDs0..51. half-1 selects the
// native initial page from the raw next-round byte; explicit halves are0/1.
// These two functions are pure: no source HOK/date/pairing mutation or RNG.
ClassicTeamSchedule classic_team_schedule(const HokFile& h, const ScheduleTables& tables,
    const SeasonCalendar& calendar, int team, int half = -1);
void draw_classic_team_schedule(ClassicCanvas& canvas, const HokFile& h,
    const ScheduleTables& tables, const SeasonCalendar& calendar, int team, int half = -1);

// Explicit native UI-working-image mutations, never run on canonical Game
// implicitly. Prepare after opening OR changing page. Finish restores only
// current pairings, retaining preview date until the outer main-menu return.
// False means native next-round0: original returns immediately/no mutation.
bool prepare_classic_team_schedule_working(HokFile& working, const ScheduleTables& tables,
    const SeasonCalendar& calendar, int team, int half = -1);
bool finish_classic_team_schedule_working(HokFile& working, const ScheduleTables& tables, int team);
//5CB4:27F5 ->4F6E:1271(0), called when Stärken returns to the main menu.
void normalize_classic_team_schedule_main_return(HokFile& working);

} // namespace em::ui
