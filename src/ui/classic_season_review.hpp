// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>

#include "game/season_review.hpp"
#include "ui/classic_canvas.hpp"

// End-of-season pages of the classic frontend: the overview "Saisonende" (titles, winners, promotions and
// relegations) and the personal season review of each human manager.

namespace em::ui {

// Season review of the human manager. The page is the main-menu header in its season-review form
// (ClassicApplication::draw_main_header with a review) under the title "Saisonende", the panel "Ereignisse",
// the KB13 symbols of the season's events, the history line and the prizes.
// The title and the empty "Ereignisse" panel, without the header.
void draw_classic_season_review_frame(ClassicCanvas&);
// The content of the panel: history line ("10. Platz der Bundesliga" ...), event symbols and prizes.
void draw_classic_season_review_body(ClassicCanvas&, const SeasonReview&);
// "Erhaltene Prämien: <amount>" or "... keine".
std::string classic_season_review_prizes(const SeasonReview&);
// Top-left corner of the n-th symbol frame: five per row from (0x4C,0x65), 0x23 apart, rows 0x1E apart.
std::pair<int, int> classic_season_review_slot(int n);
// The overview "Saisonende" shown before the review. face/manager: the human manager, drawn under
// the titles the manager's club won (human_winner).
void draw_classic_season_overview(ClassicCanvas&, const SeasonReview&, int face, const std::string& manager);
// With several human managers: under each title the face and name of the manager who holds it (winner_manager,
// faces/names indexed by manager; a negative face draws nothing).
void draw_classic_season_overview(ClassicCanvas&, const SeasonReview&, const std::array<int, 4>& faces,
                                  const std::array<std::string, 4>& names);
// A club name split at its first space into two lines, each cut until it is at most `width` pixels wide in the
// small font (0x46, 0x3C for the European cup column).
std::pair<std::string, std::string> classic_season_overview_lines(const ClassicCanvas&, const std::string& name, int width);

}  // namespace em::ui
