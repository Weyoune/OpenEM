// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "ui/classic_canvas.hpp"
#include <optional>
#include <vector>

namespace em::ui {
enum class ClassicPlayoffAction { None, ClubInfo, Liga };
struct ClassicPlayoffHit {
    ClassicPlayoffAction action = ClassicPlayoffAction::None;
    int team = -1, mode = -1, pair = -1;
    int left = 0, top = 0, right = -1, bottom = -1; //inclusive
};
struct ClassicPlayoffScore {
    int x = 0, markers = 0;
    uint8_t color = 1;
    std::string native;
};
struct ClassicPlayoffRow {
    int pair = 0, home = -1, away = -1, baseline = 0;
    uint8_t home_color = 30, away_color = 1;
    std::string native_home, native_away;
    std::vector<ClassicPlayoffScore> scores;
};
struct ClassicPlayoffHeading { int baseline = 0; std::string native; };
struct ClassicPlayoffView {
    int stage = 0, game = 0, cursor = 255;
    bool available = false;
    int frame_top = 70, frame_rows = 5, pair_count = 1;
    std::string native_title = "Play-Off Spiele", native_caption;
    std::vector<ClassicPlayoffHeading> headings;
    std::vector<ClassicPlayoffRow> rows;
    int preview_calendar_index = -1;
    std::optional<GameDate> preview_date;
};

//4996:38F3(type2,phase0), reached from Bundesliga fixtures' middle control.
//Reads complete native bracket/history, including old stages. Pure: no Game,
//RNG, booking, strength refresh, working-date mutation or screen restoration.
ClassicPlayoffView classic_playoff_view(const HokFile&,const SeasonCalendar&);
ClassicPlayoffHit classic_playoff_hit(const ClassicPlayoffView&,int x,int y);
void draw_classic_playoff(ClassicCanvas&,const HokFile&,const SeasonCalendar&);
//Explicit modal entry action on the controller's transient HOK working image.
//Also calculates the hidden preview date when the bracket is not available.
bool prepare_classic_playoff_working(HokFile&,const SeasonCalendar&);
} // namespace em::ui
