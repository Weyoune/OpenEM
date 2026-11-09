// SPDX-License-Identifier: GPL-3.0-only
// Season review page of the human manager (ovl02_0000 = ovr026), see docs/logic/MELDUNGEN.md.
#include "ui/classic_season_review.hpp"

#include "ui/classic_stats_club.hpp"
#include "ui/session.hpp"

namespace em::ui {
namespace {
constexpr auto kBold = ClassicFontId::NewBold;  // native font 1
constexpr auto kFlow = ClassicFontId::Flow;     // native font 2
constexpr int kSymbols = 13;                    // KB13: 6 x 3 symbols of 0x18 x 0x15, the frame at (0x90,0)
constexpr uint8_t kKey = 0xC0;                  // transparent colour of sub_21BC9
constexpr int kNumberWidth = 5;                 // DS:0374 when the review runs: the place is padded with '%'
}  // namespace

std::pair<int, int> classic_season_review_slot(int n) {
    int x = 0x4C, y = 0x65;
    for (; n > 0; n--) {
        x += 0x23;
        if (x > 0xE6) { x = 0x4C; y += 0x1E; }
    }
    return {x, y};
}

void draw_classic_season_review_frame(ClassicCanvas& c) {
    // ovr025:0F6A: title in font 1, panel 13 x 6 tiles, "Ereignisse" in font 2 and the white rule under it
    c.text(kBold, 0, 0x13, "Saisonende", 0x1E, 0, 0x13F);
    c.frame(4, 0x50, 13, 6);
    c.text(kFlow, 0, 0x59, "Ereignisse", 10, 0x1C, 0x141);
    c.line(0x0D, 0x5B, 0x132, 0x5B, 7);
}

std::string classic_season_review_prizes(const SeasonReview& review) {
    // ovr026:0443: DS:1A02 plus the amount with thousands points (DS:038D = 0) and the currency, or "keine"
    return "Erhaltene Prämien: " + (review.prizes != 0 ? money(review.prizes) : std::string("keine"));
}

void draw_classic_season_review_body(ClassicCanvas& c, const SeasonReview& review) {
    // ovr026:00EA: history line (ovl04_1eb9); a text ending in '.' gets "Platz" (DS:4B35)
    std::string place = classic_history_place(review.place, review.league, kNumberWidth);
    if (!place.empty() && place.back() == '.') place += "Platz";
    c.text(kFlow, 0, 0xAA, place, 7, 0, 0x140);
    // ovr026:022A..03FA (146e:07ff): the symbol frame, then the symbol 3 pixels inside it
    for (size_t n = 0; n < review.icons.size(); n++) {
        const auto [x, y] = classic_season_review_slot(int(n));
        const int icon = review.icons[n];
        c.blit(kSymbols, x, y, 0x90, 0, 0x1E, 0x1B, true, kKey);
        c.blit(kSymbols, x + 3, y + 3, (icon % 6) * 0x18, (icon / 6) * 0x15, 0x18, 0x15, true, kKey);
    }
    if (review.icons.empty()) c.text(kFlow, 10, 0x64, "Keine besonderen Ereignisse...", 7, 0, 0x131);
    c.text(kFlow, 0, 0xB4, classic_season_review_prizes(review), 7, 0, 0x140);
}

}  // namespace em::ui
