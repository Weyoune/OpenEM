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

namespace em::ui {

std::pair<std::string, std::string> classic_season_overview_lines(const ClassicCanvas& c, const std::string& name,
                                                                  int width) {
    const size_t space = name.find(' ');
    std::string first = name.substr(0, space), second = space == std::string::npos ? "" : name.substr(space + 1);
    auto cut = [&](std::string& s) {
        while (!s.empty() && c.font().measure(ClassicFontId::Mini, s) > width) {
            s.pop_back();
            // keep UTF-8 sequences whole (umlauts are one native glyph)
            while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
            if (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0xC0) s.pop_back();
        }
    };
    cut(first);
    cut(second);
    return {first, second};
}

void draw_classic_season_overview(ClassicCanvas& c, const SeasonReview& review, int face, const std::string& manager) {
    SeasonReview own = review;
    for (size_t k = 0; k < 4; k++) own.winner_manager[k] = review.human_winner[k] ? 0 : -1;
    draw_classic_season_overview(c, own, std::array<int, 4>{face, -1, -1, -1}, std::array<std::string, 4>{manager, "", "", ""});
}

void draw_classic_season_overview(ClassicCanvas& c, const SeasonReview& review, const std::array<int, 4>& faces,
                                  const std::array<std::string, 4>& names) {
    constexpr auto mini = ClassicFontId::Mini;  // native font 3
    // ovr036:1673: ice background, panel 13 x 11 tiles at (4,1), title in font 1, two rules in colour 1
    c.background(22);
    c.frame(4, 1, 13, 11);
    c.text(kBold, 0, 0x18, "Saisonende", 7, 0, 0x13F);
    c.line(0x0B, 0x1B, 0x133, 0x1B, 1);
    c.line(0x0B, 0x81, 0x133, 0x81, 1);
    static constexpr const char* kLeagues[3] = {"Bundesliga", "Zweite Liga", "Oberliga"};  // DS:09EC
    for (int l = 0; l < 3; l++) c.text(kFlow, l * 0x6A, 0x8F, kLeagues[l], 0x1E, 0, l * 0x6A + 0x6A);
    // DS:077D: two heading lines per title, the flag of KB3 (DS:5351 {2,1,0,3}, 14 x 11 from (0x4E,0xE7))
    static constexpr const char* kHeadings[4][2] = {
        {"Weltmeister", ""}, {"DEB-", "Pokal"}, {"Deutscher", "Meister"}, {"Europa-", "Pokal"}};
    static constexpr int kFlags[4] = {2, 1, 0, 3};
    for (int k = 0; k < 4; k++) {
        c.text(kFlow, k * 0x50 + 7, 0x35, kHeadings[k][0], 0x1E, 0, k * 0x50 + 0x50);
        c.text(kFlow, k * 0x50 + 7, 0x3D, kHeadings[k][1], 0x1E, 0, k * 0x50 + 0x50);
        // drawn opaque: the flag pixels of index 0xC0 appear in the capture (KB3 loaded with sub_1A37D(..., 1))
        c.blit(3, k * 0x50 + 0x23, 0x22, kFlags[k] * 0x0E + 0x4E, 0xE7, 0x0E, 0x0B);
    }
    // ovr036:1881: the winner in two lines; under a title of the manager's club the manager's face and name
    for (int k = 0; k < 4; k++) {
        const auto [first, second] = classic_season_overview_lines(c, review.winners[size_t(k)], k == 3 ? 0x3C : 0x46);
        c.text(mini, k * 0x50 + 5, 0x48, first, 1, 0, k * 0x50 + 0x50);
        c.text(mini, k * 0x50 + 5, 0x50, second, 1, 0, k * 0x50 + 0x50);
        const int m = review.winner_manager[size_t(k)];
        if (m >= 0 && m < 4 && faces[size_t(m)] >= 0) {
            const int face = faces[size_t(m)];
            c.blit(0, k * 0x50 + 0x15, 0x54, 41 * (face % 3) + 1, 41 * (face / 3) + 1, 0x26, 0x26, true, kKey);
            c.text(mini, k * 0x50 + 5, 0x7E, names[size_t(m)], 1, 0, k * 0x50 + 0x50);
        }
    }
    // ovr036:1ADB and 1BC4: per league the clubs promoted into it ('@', colour 0x0C) from y 0xB0, the clubs
    // relegated into it ('&', 0x12) or the champion ('*', 0x16) from y 0x9C; marker at 100 * league + 0x14
    auto row = [&](const std::string& club, char marker, uint8_t color, int league, int y) {
        c.text(mini, league * 100 + 0x14, y, std::string(1, marker) + " ", color, 0);
        c.text(mini, league * 100 + 0x1C, y, club, 1, 0);
    };
    for (int l = 0; l < 3; l++) {
        const auto& up = review.promoted_into[size_t(l)];
        for (size_t n = 0; n < up.size() && n < 2; n++) row(up[n], '@', 0x0C, l, 0xB0 + 8 * int(n));
        if (l == 0) {
            row(review.winners[2], '*', 0x16, 0, 0x9C);
        } else {
            const auto& down = review.relegated_into[size_t(l)];
            for (size_t n = 0; n < down.size() && n < 2; n++) row(down[n], '&', 0x12, l, 0x9C + 8 * int(n));
        }
    }
}

}  // namespace em::ui
