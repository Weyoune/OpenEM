// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <optional>
#include <vector>

// World Championship pages of the classic frontend: the two group tables (pages 11/12), the group fixtures
// (pages 8/9) and the final round bracket (page 10).

namespace em::ui {
// What a click does: open a team's club info, switch page, switch round, or return to the last fixture page.
enum class ClassicWmAction { None, ClubInfo, Page, Round, PreviousFixtures };
// A click target: action and its argument (team with mode 2, page or round) and the inclusive hit box.
struct ClassicWmHit {
    ClassicWmAction action = ClassicWmAction::None;
    int team = -1, mode = -1, page = -1, round = -1;
    int left = 0, top = 0, right = -1, bottom = -1; // inclusive hit box, as in the original
};
// A button: its click target and label.
struct ClassicWmButton {
    ClassicWmHit hit;
    std::string native;
};
// One group table row: team, its index in the group, text baseline, flag sprite (KB number) and its x,
// points, minus points (2 * games - points), goals for/against, colour and texts.
struct ClassicWmTableRow {
    int team = -1, index = 0, baseline = 0, badge = 0, badge_x = 0;
    int points = 0, minus_points = 0, goals_for = 0, goals_against = 0;
    uint8_t color = 7;
    std::string native_rank, native_stats;
    ClassicWmHit hit;
};
// One group fixture: top y, home/away team, their flag sprites and border colours (18 for a human manager's
// team), and the stored score bytes.
struct ClassicWmFixture {
    int top = 0, home = -1, away = -1, home_badge = 0, away_badge = 0;
    uint8_t home_border = 7, away_border = 7;
    std::array<int,2> scores{}; // unsigned bytes, including future/sentinel values
    std::array<std::string,2> native_scores;
    std::array<ClassicWmHit,2> hits;
};
// One final round pairing; markers are the dots of scores stored with +30/+60 (as on the play-off page).
struct ClassicWmKnockoutRow {
    int pair = 0, home = -1, away = -1, baseline = 0, markers = 0;
    std::string native_home, native_away, native_score;
    std::array<ClassicWmHit,2> hits;
};
// A stage heading of the final round.
struct ClassicWmHeading { int baseline = 0; std::string native; };
// Everything one WM page shows.
struct ClassicWmView {
    int page = 11, group = 0, phase = 0, round = 0, incoming_numeric_width = 1;  // phase: WM round (0x8F85)
    int active_manager = 0, frame_top = 2, frame_rows = 12, pair_count = 0;
    bool available = true;          // final round: pairings exist (phase >= 5)
    std::string native_title, native_caption;
    std::array<ClassicWmTableRow,6> table;
    std::array<ClassicWmFixture,3> fixtures;
    std::vector<ClassicWmHeading> headings;
    std::vector<ClassicWmKnockoutRow> knockout;
    std::vector<ClassicWmButton> buttons;
    int preview_calendar_index = -1;      // final round: calendar index of the next WM day
    std::optional<GameDate> preview_date; // and its date
};

// Builds one page as the original does: tables 11/12, fixtures 8/9, final round 10. round = -1 selects
// min(WM phase, 4). incomingNumericWidth is the number pad width the original carries over from the page the
// user came from. The view and renderer never sort, book, use random numbers or change working dates.
// Throws FormatError on invalid arguments or state.
ClassicWmView classic_wm_view(const HokFile&,const SeasonCalendar&,int page=11,
                            int round=-1,int incomingNumericWidth=1,int activeManager=0);
// Hit test at (x, y).
ClassicWmHit classic_wm_hit(const ClassicWmView&,int x,int y);
// Paints the page.
void draw_classic_wm(ClassicCanvas&,const HokFile&,const SeasonCalendar&,int page=11,
                     int round=-1,int incomingNumericWidth=1,int activeManager=0);
// The final round's Gruppe button returns PreviousFixtures; the controller keeps the last fixture page 8/9.
// Only entering the final round (page 10) writes the three date fields (0x8E21 day, 0x8E25 month - 1, 0x8E29
// year) and h.date of the working copy; returns false otherwise or without a preview date.
bool prepare_classic_wm_working(HokFile&,const SeasonCalendar&,int page=10);
} // namespace em::ui
