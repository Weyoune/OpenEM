// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Original "Einstellungen" page (Gemischtes -> Einst., ovr028:2B3C). The page edits bytes of the save state:
//   DS:021D (state 0xA51C): 14 flags. 0..8 = Drittelpause/Ergebnisse/Tabelle (index = 3 * league + row),
//     9 = Torszenen (0 off, 1 on, 2 = rink stays on; cycles), 10 = DEB-Pokal, 11 = Europapokal, 12 = WM-Spiele,
//     13 = Hintergrundgrafik; every other value toggles 0/1.
//   DS:0120 (state 0x2C): Scrolling per human manager.
//   DS:0262 (state 0xB94F): game speed word, 0..70 in steps of 10 (the page shows speed / 9 + 1 of eight penguins).
//   DS:5A14 (state 0xAEB5) end year (>= 2000 = never), DS:0ED8 (state 0xAEB4) start level (shown as 6 - level).
#include <array>
#include "formats/hok.hpp"

namespace em {

struct GameSettings {
    std::array<uint8_t, 14> flags{};
    std::array<uint8_t, 4> scrolling{};
    int speed = 0;
    int end_year = 0;
    int level = 0;
};

constexpr int kSettingWidgets = 15;  // 14 flags + scrolling of the current manager

GameSettings read_settings(const HokFile& h);
// Click on one of the 15 switches. Returns the new value (Torszenen: 0..2). WM-Spiele also writes the camera
// selection of the manager's national team like the original (1902:000B), unless it is the manager's own club.
int toggle_setting(HokFile& h, int widget, int manager);
void set_game_speed(HokFile& h, int speed);

}  // namespace em
