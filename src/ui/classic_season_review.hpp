// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <string>

#include "game/season_review.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {

// Season review of the human manager (ovl02_0000 = ovr026, docs/logic/MELDUNGEN.md). The page is the main-menu
// header in its 0xFE form (ClassicApplication::draw_main_header) under the title "Saisonende", the panel
// "Ereignisse" (ovl01_0f6a), the KB13 symbols, the history line and the prizes.
void draw_classic_season_review_frame(ClassicCanvas&);                          // ovl01_0f6a without the header
void draw_classic_season_review_body(ClassicCanvas&, const SeasonReview&);     // ovr026:00EA..04D4
std::string classic_season_review_prizes(const SeasonReview&);                  // "Erhaltene Prämien: ..."
// Top-left corner of the n-th symbol frame (146e:07c6): five per row from (0x4C,0x65), 0x23 apart, rows 0x1E apart.
std::pair<int, int> classic_season_review_slot(int n);
// The overview "Saisonende" shown before the review (ovl12_1657). face/manager: the human manager, drawn under
// the titles the manager's club won (human_winner).
void draw_classic_season_overview(ClassicCanvas&, const SeasonReview&, int face, const std::string& manager);
// A club name split at its first space into two lines, each cut until it is at most `width` pixels wide in the
// small font (ovr036:18E6..19C3; 0x46, 0x3C for the European cup column).
std::pair<std::string, std::string> classic_season_overview_lines(const ClassicCanvas&, const std::string& name, int width);

}  // namespace em::ui
