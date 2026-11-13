// SPDX-License-Identifier: GPL-3.0-only
// Statistiken -> Verein page and the manager history text (see classic_stats_club.hpp).
#include "ui/classic_stats_club.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace em::ui {
namespace {
constexpr auto flow = ClassicFontId::Flow, mini = ClassicFontId::Mini;
// Save state: manager blocks (0x1AF bytes each) and the career year (u16).
constexpr size_t kManagers = 0x0DCD, kCareerYear = 0xB951;
// Graph layout tables of the game, index 0 EuroCup, 1 DEB-Pokal, 2 Liga: label position, axis left/top/bottom
// and the spacing of the axis ticks.
constexpr std::array<int, 3> kLabelX{0x4B, 0x11, 0x36}, kLabelY{0x17, 0x45, 0xA8};
constexpr std::array<int, 3> kLeft{0x87, 0x4B, 0x73}, kTop{0x0C, 0x3A, 0x65}, kStep{3, 3, 4}, kBottom{0x1F, 0x4D, 0xAE};
constexpr std::array<uint8_t, 3> kLeagueColor{0x17, 0x16, 0x1D};  // bar colour per league
constexpr std::array<int, 3> kSplit{6, 4, 0};                     // where each graph label is split into two lines
constexpr std::array<std::string_view, 3> kNames{"Europacup", "DEB-Pokal", "Liga"};  // graph labels
constexpr std::array<int, 4> kLeagueFirst{0, 12, 24, 0};              // first team index of each league
constexpr std::array<std::string_view, 4> kLeagues{"Bundesliga", "Zweite Liga", "Oberliga", ""};
constexpr std::array<std::string_view, 4> kPlayoff{"VIERTELFINALE", "HALBFINALE", "SPIELE 5.-8.", "ENDSPIEL"};
// DEB-Pokal round reached (index = cup byte & 7).
constexpr std::array<std::string_view, 8> kCupRounds{"", "Erste Runde ", "Achtelfinale ", "Viertelfinale ",
                                                     "Halbfinale ", "Endspiel ", "DEB-Pokal", "Europapokal"};
// European cup phase reached (index = (euro byte & 7) >> 1).
constexpr std::array<std::string_view, 4> kEuroPhases{"Liga", "Gruppe", "Endrunde", "Play-Off"};

// Decimal text, non-negative values padded to width with the blank digit '%'.
std::string number(long value, int width) {
    const bool negative = value < 0;
    std::string s = std::to_string(negative ? -value : value);
    if (negative) return "-" + s;
    if (int(s.size()) < width) s.insert(0, size_t(width - int(s.size())), '%');
    return s;
}
// Block of the current manager (save state 0x0788).
size_t manager_block(const HokFile& h) {
    const int manager = h.state.at(0x0788);
    if (manager >= 4) throw FormatError("classic club statistics: invalid manager");
    return kManagers + size_t(manager) * 0x1AF;
}
}  // namespace

std::string classic_history_place(int place, int league, int width) {
    if ((place & 0x7F) < 0x32 && !(place & 0x80)) {
        if (league < 0 || league > 3) throw FormatError("classic club statistics: invalid league");
        return number((place & 0x7F) - kLeagueFirst[size_t(league)], width) + "." + " Platz der " + std::string(kLeagues[size_t(league)]);
    }
    if (place & 0x80) return "Deutscher Meister";
    int round = (place & 0x7F) - 0x33;
    if (round == 2) round = 3;
    if (round < 0 || round > 3) throw FormatError("classic club statistics: invalid play-off code");
    return "Play-Off " + std::string(kPlayoff[size_t(round)]);
}

int classic_stats_club_years(const HokFile& h) {
    return std::min<int>(le16(h.state, kCareerYear), 50);
}

void draw_classic_stats_club(ClassicCanvas& c, const HokFile& h, int selected) {
    const Bytes& s = h.state;
    const size_t block = manager_block(h);
    const int career = le16(s, kCareerYear), years = classic_stats_club_years(h);
    if (selected < 0) selected = years - 1;
    if (years && selected >= years) throw FormatError("classic club statistics: invalid year");
    c.background(22);
    c.frame(0x3F, 4, 10, 2);
    c.frame(4, 0x33, 10, 2);
    c.frame(0x2A, 0x5C, 10, 5);
    c.frame(0x15, 0xC6, 10, 2);
    for (size_t i = 0; i < 3; ++i) {
        const std::string_view name = kNames[i];
        c.text_native(flow, kLabelX[i], kLabelY[i], name.substr(0, size_t(kSplit[i])), 10, 28);
        c.text_native(flow, kLabelX[i], kLabelY[i] + 9, name.substr(size_t(kSplit[i])), 10, 28);
        int countdown = 0, y = kTop[i];
        for (; y < kBottom[i]; ++y) {
            if (countdown == 0) { countdown = kStep[i]; c.pixels().at(kLeft[i] - 1, y) = 6; }
            --countdown;
        }
        --y;  // the axis ends one pixel above the bottom limit, as in the original
        c.line(kLeft[i], y, kLeft[i], kTop[i], 6);
        c.line(kLeft[i], y, kLeft[i] + 0x98, y, 6);
    }
    if (career) c.blit(4, 0xF2, 0xD0, 0x14, 0, 9, 17, true, 0xC0);
    auto rect = [&](int l, int t, int r, int b, uint8_t color) { if (t <= b && l <= r) c.rect(l, t, r, b, color); };
    for (int year = 0; year < years; ++year) {
        const size_t row = block + 0x18 + size_t(year) * 4;
        int v = s.at(row);
        if (v & 0x80) v = 1;
        else {
            v &= 0x7F;
            if (v > 0x32) v = v == 0x35 ? 2 : v == 0x34 ? 4 : v == 0x33 ? 6 : v;
        }
        const int x = kLeft[2] + year * 3;
        int k = 2;
        for (; k * 12 >= v; --k)
            rect(x + 2, kBottom[2] - 0x1A - (2 - k) * 0x18, x + 3, kBottom[2] - 2 - (2 - k) * 0x18, v == 1 ? 0x1E : kLeagueColor[size_t(k)]);
        rect(x + 2, kBottom[2] - 1 - (0x25 - v) * 2, x + 3, kBottom[2] - 2 - (2 - k) * 0x18, v == 1 ? 0x1E : kLeagueColor[size_t(k)]);
        int cup = s.at(row + 2);
        uint8_t color = 0x1B;
        if (cup & 0x80) { cup = 6; color = 0x1E; }
        cup &= 0x7F;
        rect(kLeft[1] + year * 3 + 2, kBottom[1] - cup * 3 - 1, kLeft[1] + year * 3 + 3, kBottom[1] - 2, color);
        const int euro = s.at(row + 3);
        if ((euro & 0x7F) >> 3) {
            int phase = euro & 7;
            color = 0x1B;
            if (euro & 0x80) { phase = 6; color = 0x1E; }
            rect(kLeft[0] + year * 3 + 2, kBottom[0] - phase * 3 - 1, kLeft[0] + year * 3 + 3, kBottom[0] - 2, color);
        }
    }
    if (!career) {
        c.text_native(mini, 0x16, 0xDB, "Im ersten Jahr gibt's hier noch keine Eintr{ge...", 30, 0, 0x104);
        return;
    }
    // Selection marker under every graph and the detail lines.
    for (size_t i = 0; i < 3; ++i) {
        const int x = kLeft[i] + selected * 3, b = kBottom[i];
        rect(kLeft[i], b, kLeft[i] + 0x98, b + 5, 4);
        rect(x + 2, b, x + 3, b + 5, 0x10);
        c.line(x + 1, b + 1, x + 4, b + 1, 0x10);
        c.line(x, b + 2, x + 5, b + 2, 0x10);
    }
    rect(0x20, 0xCD, 0xD9, 0xE5, 4);
    c.text_native(mini, 0x28, 0xD3, number(selected + s.at(block + 0xEE) + 1993, 1), 30, 0);
    const size_t row = block + 0x18 + size_t(selected) * 4;
    const int place = s.at(row);
    if (place == 0xFF) {
        c.text_native(mini, 0x42, 0xD3, "...spielten Sie noch gar nicht mit !", 30, 0);
        return;
    }
    std::string text = classic_history_place(place, s.at(row + 1));
    uint8_t color = (place & 0x80) ? 7 : 0x0B;  // "Deutscher Meister" is white
    c.text_native(mini, 0x42, 0xD3, text, color, 0);
    const int cup = s.at(row + 2);
    text = "DEB-Pokal: ";
    color = 0x0B;
    if (cup & 0x80) { text += "SIEGER"; color = 7; }
    else text += kCupRounds[size_t(cup & 7)];
    c.text_native(mini, 0x42, 0xDB, text, color, 0);
    const int euro = s.at(row + 3);
    text = "Europacup: ";
    color = 0x0B;
    if (euro & 0x80) { text += "SIEGER "; color = 7; }
    else if (euro & 7) text += kEuroPhases[size_t((euro & 7) >> 1)];
    if (((euro & 0x7F) >> 3) == 0) text += "Nicht im Wettbewerb";
    c.text_native(mini, 0x42, 0xE3, text, color, 0);
}

int classic_stats_club_arrow(int x, int y) {
    if (x < 0xF2 || x > 0xF2 + 8) return 0;
    if (y >= 0xD0 && y <= 0xD0 + 8) return 1;
    if (y >= 0xD9 && y <= 0xD9 + 8) return -1;
    return 0;
}

}  // namespace em::ui
