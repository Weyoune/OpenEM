// SPDX-License-Identifier: GPL-3.0-only
// Statistiken -> WM page (see classic_stats_wm.hpp).
#include "ui/classic_stats_wm.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace em::ui {
namespace {
constexpr auto bold = ClassicFontId::NewBold, mini = ClassicFontId::Mini;
// Save state: league table rows (0x50 bytes per team) and team records (61 bytes).
constexpr size_t kTable = 0x3BA5, kTeams = 0x17E7;
constexpr int kFirstNation = 52;                    // team index of the first national team
constexpr std::array<int, 3> kSeparators{0x2C, 0x35, 0x47};  // y of the rules under places 1, 2 and 3

// Decimal text, non-negative values padded to width with the blank digit '%'.
std::string number(long value, int width) {
    const bool negative = value < 0;
    std::string s = std::to_string(negative ? -value : value);
    if (negative) return "-" + s;
    if (int(s.size()) < width) s.insert(0, size_t(width - int(s.size())), '%');
    return s;
}
// League table row of national index 0..11.
size_t row(int nation) { return kTable + size_t(kFirstNation + nation) * 0x50; }
// x of the nation's flag in the KB12 flag strip: the u32 at table row +0x4C times 9.
int flag_x(const HokFile& h, int nation) {
    const uint32_t index = le32(h.state, row(nation) + 0x4C);
    if (index > 40) throw FormatError("classic WM statistics: invalid flag index");
    return int(index) * 9;
}
}  // namespace

ClassicStatsWmView classic_stats_wm_view(const HokFile& h) {
    ClassicStatsWmView view;
    for (int i = 0; i < 12; ++i) {
        view.order[size_t(i)] = i;
        for (size_t k = 0; k < 0x18 && h.state.at(row(i) + 0x34 + k); ++k) {
            const int place = h.state.at(row(i) + 0x34 + k);
            if (place < 9) view.score[size_t(i)] += 9 - place;
        }
    }
    for (size_t i = 0; i < 12; ++i)
        for (size_t j = i + 1; j < 12; ++j)
            if (view.score[size_t(view.order[i])] < view.score[size_t(view.order[j])]) std::swap(view.order[i], view.order[j]);
    return view;
}

void draw_classic_stats_wm(ClassicCanvas& c, const HokFile& h) {
    const auto view = classic_stats_wm_view(h);
    c.background(22);
    c.frame(0x10, 2, 12, 6);
    c.frame(0x10, 0x77, 12, 6);
    c.text_native(bold, 0, 0x15, "WM-Statistik", 7, 1, 0x13F);
    c.line(0x1B, 0x17, 0x124, 0x17, 7);
    for (int col = 0, x = 0x1F; col < 0x13; ++col, x += 0x0E) {
        if (col != 0 && col <= 2) continue;
        for (int r = 0, y = 0x24; r < 8; ++r, y += 9) {
            c.rect(x, y, x + 12, y + 7, 1);
            c.line(x, y, x + 12, y, 0x17);
            c.line(x, y, x, y + 7, 0x17);
            c.line(x + 12, y + 1, x + 12, y + 7, 0x16);
            c.line(x + 1, y + 7, x + 11, y + 7, 0x16);
        }
    }
    for (int r = 0; r < 8; ++r) {
        c.text_native(mini, 0x17, 0x2A + 9 * r, number(r + 1, 1) + ".", r == 0 ? 17 : 7);
        if (r < 3) c.line(0x1F, kSeparators[size_t(r)], 0x127, kSeparators[size_t(r)], 8);
    }
    // The original copies the twelve crests KB(80 + (team +0x34 & 63)) into a 456x38 strip and shows it
    // opaquely in the national team boxes (without EMS memory that strip stays black; OpenEM draws the
    // crests as on a machine with EMS).
    Image cell(13, 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 13; ++x) cell.at(x, y) = c.pixels().at(0x1F + x, 0x24 + y);
    for (int i = 0, x = 0x18, y = 0x83, fy = 0x7A; i < 12; ++i) {
        c.rect(x, y, x + 0x29, y + 0x29, 1);
        c.line(x, y, x + 0x29, y, 0x17);
        c.line(x, y, x, y + 0x29, 0x17);
        c.line(x + 0x29, y + 1, x + 0x29, y + 0x29, 0x1D);
        c.line(x + 1, y + 0x29, x + 0x28, y + 0x29, 0x1D);
        c.blit(80 + (h.state.at(kTeams + size_t(kFirstNation + i) * 61 + 0x34) & 63), x + 2, y + 2, 0, 0, 38, 38);
        for (int yy = 0; yy < 8; ++yy)  // as in the original: a copy of grid cell (31,36) taken from the screen
            for (int xx = 0; xx < 13; ++xx) c.pixels().at(x + 0x0E + xx, fy + yy) = cell.at(xx, yy);
        c.blit(12, x + 0x10, fy + 1, flag_x(h, i), 0, 9, 6);
        for (size_t k = 0; k < 0x18 && h.state.at(row(i) + 0x34 + k); ++k) {
            const int place = h.state.at(row(i) + 0x34 + k);
            if (place < 9) c.blit(12, int(k) * 0x0E + 0x4B, place * 9 + 0x1C, flag_x(h, i), 0, 9, 6);
        }
        x += 0x2E;
        if (x > 0x10E) { x = 0x18; y += 0x2E; fy += 0x62; }
    }
    for (int r = 1; r < 9; ++r) {
        const int nation = view.order[size_t(r - 1)];
        c.blit(12, 0x21, r * 9 + 0x1C, flag_x(h, nation), 0, 9, 6);
        c.text_native(mini, 0x2D, 0x21 + 9 * r, number(view.score[size_t(nation)], 3) + " P.", 30, 6);
    }
}

int classic_stats_wm_team(int x, int y) {
    if (x <= 0x17 || x >= 0x12B || y <= 0x83 || y >= 0xDF) return -1;
    return kFirstNation + (x - 0x17) / 0x2E + (y > 0xB1 ? 6 : 0);
}

}  // namespace em::ui
