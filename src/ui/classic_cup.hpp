// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <optional>

// Classic DEB-Pokal page: view model, hit testing and renderer.
namespace em::ui {

// One result of a pairing.
struct ClassicCupScore {
    int x = 0, markers = 0;  // column; number of marker dots after the score (0..2)
    uint8_t color = 1;       // alternates 1 / 30 between games
    std::string native;      // "hh:aa" in the game's encoding
};
// One pairing of the current round.
struct ClassicCupRow {
    int pair = 0, home = -1, away = -1, baseline = 0;  // draw index, team indices, text baseline
    int hit_min_y = 0, hit_max_y = 0;                  // clickable y range
    uint8_t home_color = 30, away_color = 1;           // 24 for teams run by a human manager
    std::string native_home, native_away;
    std::vector<ClassicCupScore> scores;
};
// Pairings of one league combination (e.g. Bundesliga against Oberliga).
struct ClassicCupGroup {
    int heading_baseline = 0, empty_baseline = -1;  // empty_baseline >= 0: draw "---------"
    std::string native_heading;
    std::vector<ClassicCupRow> rows;
};
// The complete page.
struct ClassicCupView {
    int frame_top = 0, frame_rows = 0, pair_count = 0;  // frame geometry; number of pairings
    std::string native_caption;                         // round title and, if any, next cup date
    int preview_calendar_index = -1;                    // calendar index of the next cup day, or -1
    std::optional<GameDate> preview_date;               // its date
    std::array<ClassicCupGroup,6> groups;
};
// Result of a click on the page.
struct ClassicCupHit {
    int team = -1, mode = -1; // clicked team; club-info mode 0 (home) or 1 (away)
    int pair = -1, baseline = 0, min_y = 0, max_y = 0;  // the row that was hit
};

// Builds the DEB-Pokal page as reached from "Tabellen" and "Paarungen" in
// the main menu. The stored draw and results are already in home/away
// order. Reads only the save state and calendar: no random numbers, no
// change to the game; preview_date is extra information for the caller.
// Throws FormatError on a truncated or inconsistent state.
ClassicCupView classic_cup(const HokFile& h, const SeasonCalendar& calendar);
// Maps a click to the team in that row (team -1 if none).
ClassicCupHit classic_cup_hit(const ClassicCupView& view, int x, int y);
// Draws the complete page on the ice wallpaper (KB/22) with frame, icons and text.
void draw_classic_cup(ClassicCanvas& canvas, const HokFile& h, const SeasonCalendar& calendar);
// The same page from a prepared view (used by the cup table during the match display, ui/classic_match_view).
void draw_classic_cup_view(ClassicCanvas& canvas, const ClassicCupView& view);

} // namespace em::ui
