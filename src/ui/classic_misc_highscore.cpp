// SPDX-License-Identifier: GPL-3.0-only
// The classic Highscore page (see classic_misc_highscore.hpp).
#include "ui/classic_misc_highscore.hpp"

namespace em::ui {
namespace {
constexpr auto kRowFont = ClassicFontId::Mini;  // the game's font 3
// The number in at least five places, padded with '%' (a blank digit-wide glyph of the font), as the game prints it.
std::string padded(int value) {
    std::string s = std::to_string(value);
    if (s.size() < 5) s.insert(0, 5 - s.size(), '%');
    return s;
}
}  // namespace

void draw_classic_highscore(ClassicCanvas& c, const HighscoreTable& table, bool wm_only) {
    c.background(195);
    c.text(ClassicFontId::NewBold, 0, 15, "Highscore", 14, 1, 320);
    for (int i = 0; i < kHighscoreRows; i++) {
        const auto r = table.row(i);
        if (r.score == 0) continue;
        const int y = 154 + 10 * i;
        const uint8_t color = r.game == 0 ? 7 : 10;
        std::string name = r.name;
        while (c.font().measure_native(kRowFont, name) > 57) name.pop_back();
        c.text_native(kRowFont, 56, y, name, color, 4);
        c.text_native(kRowFont, 115, y, r.club, color, 4);
        c.text_native(kRowFont, 207, y, padded(r.score), color, 4);
        if (wm_only) continue;
        std::string titles = "(X/X/X)";
        for (size_t k = 0; k < 3; k++) titles[1 + 2 * k] = char('0' + r.titles[k]);
        c.text_native(kRowFont, 240, y, titles, color, 4);
    }
}
}  // namespace em::ui
