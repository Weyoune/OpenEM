// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <optional>

// Classic four-team group pages (European cup groups, final round,
// promotion groups): view model and renderer.
namespace em::ui {
// One result/date cell of a fixture (column 0 first leg, 1 return leg).
struct ClassicEuroCell {
    bool played = false;
    int calendar_index = -1;    // season day of a future game, -1 if none shown
    std::array<int,2> scores{}; // goals as stored (unsigned bytes), swapped for the return leg
    std::string native; // score, or date; future dates occur only in the first match row of a block
};
// One pairing with both legs.
struct ClassicEuroFixture {
    int home = 0, away = 0, baseline = 54;   // team indices, text baseline
    uint8_t home_color = 30, away_color = 1; // 24 for teams run by a human manager
    std::string native_home, native_away;
    std::array<ClassicEuroCell,2> cells;
};
// One row of the group table, in stored ranking order.
struct ClassicEuroTableRow {
    int team = 0, index = 0, baseline = 135;  // team, its index in the group (0..3), baseline
    int points = 0, minus_points = 0, goals_for = 0, goals_against = 0;
    uint8_t color = 30;
    std::string native_name, native_stats;
};
// A complete group page.
struct ClassicEuroView {
    int page = 3, kind = 2, group = 0, completed = 0;  // page id; kind 1 promotion, 2 cup group, 3 final round; played rounds
    bool available = true;                  // false: "Die Paarungen stehen noch nicht fest..."
    std::string native_title;
    std::array<std::string,2> native_buttons; // labels of the two navigation buttons
    std::array<int,2> button_pages{4,5};      // page each button leads to (7 = league tables)
    std::array<ClassicEuroFixture,6> fixtures;
    std::array<ClassicEuroTableRow,4> rows;
    std::optional<GameDate> last_preview_date; // the last future date printed, if any
};

// European cup pages: 3 = group 1, 4 = group 2, 5 = final round. Reads the
// stored data as is: no sorting, no random numbers, no booking or date
// change. Throws FormatError for other pages or an inconsistent state.
ClassicEuroView classic_euro_view(const HokFile&,const SeasonCalendar&,int page = 3);
void draw_classic_euro(ClassicCanvas&,const HokFile&,const SeasonCalendar&,int page = 3);
// Applies the page's one side effect on the game's working state: the
// current date becomes the last future date printed. Returns false when no
// future date was drawn (including an unavailable final round). Counters,
// calendar position, pairings and tables are not written.
bool prepare_classic_euro_working(HokFile&,const SeasonCalendar&,int page = 3);
// The same page for the promotion groups 0/1 (kind 1). Unlike the European
// cup, an undrawn promotion round hides the whole group, and only positions
// 0 and 2 of each five-event play-off block in the calendar count toward its
// six dates. Both buttons lead back to page 7.
ClassicEuroView classic_promotion_view(const HokFile&,const SeasonCalendar&,int group);
void draw_classic_promotion(ClassicCanvas&,const HokFile&,const SeasonCalendar&,int group);
bool prepare_classic_promotion_working(HokFile&,const SeasonCalendar&,int group);
} // namespace em::ui
