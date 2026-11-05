// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <optional>

namespace em::ui {
struct ClassicEuroCell {
    bool played = false;
    int calendar_index = -1;
    std::array<int,2> scores{}; // unsigned original bytes, reversed for return leg
    std::string native; // future dates occur only in the first match row of a block
};
struct ClassicEuroFixture {
    int home = 0, away = 0, baseline = 54;
    uint8_t home_color = 30, away_color = 1;
    std::string native_home, native_away;
    std::array<ClassicEuroCell,2> cells;
};
struct ClassicEuroTableRow {
    int team = 0, index = 0, baseline = 135;
    int points = 0, minus_points = 0, goals_for = 0, goals_against = 0;
    uint8_t color = 30;
    std::string native_name, native_stats;
};
struct ClassicEuroView {
    int page = 3, kind = 2, group = 0, completed = 0;
    bool available = true;
    std::string native_title;
    std::array<std::string,2> native_buttons;
    std::array<int,2> button_pages{4,5};
    std::array<ClassicEuroFixture,6> fixtures;
    std::array<ClassicEuroTableRow,4> rows;
    std::optional<GameDate> last_preview_date;
};

//5396:0EBA(page,0,0) ->4996:27A5. Only pages3/4/5 (groups1/2/final).
// Pure raw view and renderer: no sorting, RNG, booking or date mutation.
ClassicEuroView classic_euro_view(const HokFile&,const SeasonCalendar&,int page = 3);
void draw_classic_euro(ClassicCanvas&,const HokFile&,const SeasonCalendar&,int page = 3);
//Explicit UI working-image date effect. Returns false when no future date was
//drawn (including unavailable final). No counter/cursor/pairing/table writes.
bool prepare_classic_euro_working(HokFile&,const SeasonCalendar&,int page = 3);
//The same native27A5 renderer, kind1: promotion groups0/1. Unlike Euro,
//uninitialized draw03B6 hides the whole group, and only calendar40h positions
//0/2 in each five-event block count toward its six dates. Both buttons return7.
ClassicEuroView classic_promotion_view(const HokFile&,const SeasonCalendar&,int group);
void draw_classic_promotion(ClassicCanvas&,const HokFile&,const SeasonCalendar&,int group);
bool prepare_classic_promotion_working(HokFile&,const SeasonCalendar&,int group);
} // namespace em::ui
