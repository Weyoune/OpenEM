// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <optional>

namespace em::ui {

struct ClassicCupScore {
    int x = 0, markers = 0;
    uint8_t color = 1;
    std::string native;
};
struct ClassicCupRow {
    int pair = 0, home = -1, away = -1, baseline = 0;
    int hit_min_y = 0, hit_max_y = 0;
    uint8_t home_color = 30, away_color = 1;
    std::string native_home, native_away;
    std::vector<ClassicCupScore> scores;
};
struct ClassicCupGroup {
    int heading_baseline = 0, empty_baseline = -1;
    std::string native_heading;
    std::vector<ClassicCupRow> rows;
};
struct ClassicCupView {
    int frame_top = 0, frame_rows = 0, pair_count = 0;
    std::string native_caption;
    int preview_calendar_index = -1;
    std::optional<GameDate> preview_date;
    std::array<ClassicCupGroup,6> groups;
};
struct ClassicCupHit {
    int team = -1, mode = -1; //club-info mode0/home or1/away
    int pair = -1, baseline = 0, min_y = 0, max_y = 0;
};

//4996:38F3(0,0), both main Tabellen/Paarungen ->DEB-Pokal. Raw current
//draw/results are already in native home orientation. No Game, date, RNG,
//team-refresh or draw mutation; preview_date is explicit controller metadata.
ClassicCupView classic_cup(const HokFile& h, const SeasonCalendar& calendar);
ClassicCupHit classic_cup_hit(const ClassicCupView& view, int x, int y);
//Complete subordinate page on original wallpaper22; native frame/icons/text.
void draw_classic_cup(ClassicCanvas& canvas, const HokFile& h, const SeasonCalendar& calendar);
// The same page from a prepared view (used by the cup table during the match display, ui/classic_match_view).
void draw_classic_cup_view(ClassicCanvas& canvas, const ClassicCupView& view);

} // namespace em::ui
