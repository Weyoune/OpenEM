// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_settings.hpp"
#include <array>

namespace em::ui {
namespace {
// DS:4C06 (x), DS:4BE8 (y), DS:4BCA (sprite size 0 = 19x23, 1 = 11x13) of the 15 switches.
constexpr std::array<int, kSettingWidgets> kX{222, 222, 222, 252, 252, 252, 282, 282, 282, 200, 184, 214, 200, 244, 220};
constexpr std::array<int, kSettingWidgets> kY{130, 144, 158, 130, 144, 158, 130, 144, 158, 24, 188, 204, 218, 39, 54};
constexpr std::array<int, kSettingWidgets> kSize{1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0};
constexpr auto kSmall = ClassicFontId::Mini;
constexpr auto kLarge = ClassicFontId::Flow;

// ovr028:29E5. KB33 holds the on (penguin, row 0) and off (hole, row 23) sprites; 0xC0 is transparent.
void icon(ClassicCanvas& c, int x, int y, int value, int size) {
    c.blit(33, x, y, size * 19, value ? 0 : 23, 19 - 8 * size, 23 - 10 * size, true, 0xC0);
}
// ovr028:2A46. Eight penguins on a colour 4 strip, the first speed / 9 + 1 of them on.
void speed_row(ClassicCanvas& c, int speed) {
    c.rect(190, 84, 289, 96, 4);
    int on = speed / 9 + 1;
    for (int k = 0; k < 8; k++) {
        c.blit(33, 190 + 12 * k, 84, 19, on ? 0 : 23, 11, 13, true, 0xC0);
        if (on) on--;
    }
}
}  // namespace

void draw_classic_settings(ClassicCanvas& c, const GameSettings& s, int manager) {
    c.background(22);
    c.frame(56, 1, 10, 6);
    c.frame(92, 119, 9, 3);
    c.frame(46, 179, 10, 3);
    for (const auto& label : {std::pair{"Drittelpause", 140}, {"Ergebnisse", 152}, {"Tabelle", 164}})
        c.text(kSmall, 100, label.second, label.first, 7, 0, 200);
    for (int league = 0; league < 3; league++)
        c.text(kSmall, 215 + 30 * league, 128, std::to_string(league + 1) + ".Liga", 30);
    for (const auto& label : {std::pair{"DEB-Pokal", 193}, {"Europapokal", 209}, {"WM-Spiele", 225}})
        c.text(kSmall, 100, label.second, label.first, 7, 0, 100);
    for (const auto& label : {std::pair{"Torszenen", 35}, {"Hintergrundgrafik", 55}, {"Scrolling", 75}, {"Spielgeschwindigkeit", 95}})
        c.text(kLarge, 120, label.second, label.first, 7, 0, 120);
    for (int i = 0; i < kSettingWidgets; i++)
        icon(c, kX[size_t(i)], kY[size_t(i)], i < 14 ? s.flags[size_t(i)] : s.scrolling[size_t(manager)], kSize[size_t(i)]);
    if (s.flags[9] == 2) {  // rink-stays-on lens drawn into the hole (DS:4BD9/4BDE/4BE3 pixel table)
        c.rect(207, 36, 210, 38, 0x12);
        constexpr int xs[5]{207, 207, 210, 210, 208}, ys[5]{36, 38, 36, 38, 37}, colors[5]{9, 9, 9, 9, 24};
        for (int n = 0; n < 5; n++) c.rect(xs[n], ys[n], xs[n], ys[n], uint8_t(colors[n]));
    }
    speed_row(c, s.speed);
    std::string version = "V1.00 - Ende: ";
    version += s.end_year < 2000 ? std::to_string(s.end_year) : " Nie.";
    version += " (Level " + std::to_string(6 - s.level) + ")";
    c.text(kSmall, 62, 10, version, 1);
    c.text(kSmall, 62, 16, "RAM: " + std::to_string(kClassicSettingsFreeMemory) + " Bytes frei.", 2);
}

ClassicSettingsHit classic_settings_hit(int x, int y) {
    ClassicSettingsHit hit;
    for (int i = 0; i < kSettingWidgets; i++) {
        const size_t k = size_t(i);
        if (x >= kX[k] && x < kX[k] + 19 - 8 * kSize[k] && y >= kY[k] && y < kY[k] + 23 - 10 * kSize[k]) {
            hit.widget = i;
            return hit;
        }
    }
    if (x > 190 && x < 286 && y > 83 && y < 97) hit.speed = (x - 190) / 12 * 10;
    return hit;
}
}  // namespace em::ui
