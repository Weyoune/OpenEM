// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <optional>
#include <vector>

namespace em::ui {
enum class ClassicWmAction { None, ClubInfo, Page, Round, PreviousFixtures };
struct ClassicWmHit {
    ClassicWmAction action = ClassicWmAction::None;
    int team = -1, mode = -1, page = -1, round = -1;
    int left = 0, top = 0, right = -1, bottom = -1; //inclusive native hitbox
};
struct ClassicWmButton {
    ClassicWmHit hit;
    std::string native;
};
struct ClassicWmTableRow {
    int team = -1, index = 0, baseline = 0, badge = 0, badge_x = 0;
    int points = 0, minus_points = 0, goals_for = 0, goals_against = 0;
    uint8_t color = 7;
    std::string native_rank, native_stats;
    ClassicWmHit hit;
};
struct ClassicWmFixture {
    int top = 0, home = -1, away = -1, home_badge = 0, away_badge = 0;
    uint8_t home_border = 7, away_border = 7;
    std::array<int,2> scores{}; //unsigned bytes, including future/sentinel values
    std::array<std::string,2> native_scores;
    std::array<ClassicWmHit,2> hits;
};
struct ClassicWmKnockoutRow {
    int pair = 0, home = -1, away = -1, baseline = 0, markers = 0;
    std::string native_home, native_away, native_score;
    std::array<ClassicWmHit,2> hits;
};
struct ClassicWmHeading { int baseline = 0; std::string native; };
struct ClassicWmView {
    int page = 11, group = 0, phase = 0, round = 0, incoming_numeric_width = 1;
    int active_manager = 0, frame_top = 2, frame_rows = 12, pair_count = 0;
    bool available = true;
    std::string native_title, native_caption;
    std::array<ClassicWmTableRow,6> table;
    std::array<ClassicWmFixture,3> fixtures;
    std::vector<ClassicWmHeading> headings;
    std::vector<ClassicWmKnockoutRow> knockout;
    std::vector<ClassicWmButton> buttons;
    int preview_calendar_index = -1;
    std::optional<GameDate> preview_date;
};

//5396:0EBA(page,0,0): tables11/12, fixtures8/9, WM knockout10.
//round=-1 selects min(raw WM phase,4). Width is the caller's DS0374 value.
//The raw view and renderer never sort, book, draw RNG or mutate working dates.
ClassicWmView classic_wm_view(const HokFile&,const SeasonCalendar&,int page=11,
                            int round=-1,int incomingNumericWidth=1,int activeManager=0);
ClassicWmHit classic_wm_hit(const ClassicWmView&,int x,int y);
void draw_classic_wm(ClassicCanvas&,const HokFile&,const SeasonCalendar&,int page=11,
                     int round=-1,int incomingNumericWidth=1,int activeManager=0);
//The KO Gruppe button returns PreviousFixtures; the controller retains8/9.
//Only explicit KO entry may write the three derived date words/model date.
bool prepare_classic_wm_working(HokFile&,const SeasonCalendar&,int page=10);
} // namespace em::ui
