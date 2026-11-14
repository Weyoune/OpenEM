// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Original "Einstellungen" page (Gemischtes -> Einst.). The page edits bytes of the save state:
//   state 0xA51C: 14 flags. 0..8 = Drittelpause/Ergebnisse/Tabelle (index = 3 * league + row),
//     9 = Torszenen (0 off, 1 on, 2 = rink stays on; cycles), 10 = DEB-Pokal, 11 = Europapokal, 12 = WM-Spiele,
//     13 = Hintergrundgrafik; every other value toggles 0/1.
//   state 0x2C: Scrolling per human manager (4 bytes).
//   state 0xB94F: game speed word, 0..70 in steps of 10 (the page shows speed / 9 + 1 of eight penguins).
//   state 0xAEB5: end year word (>= 2000 = never), state 0xAEB4: start level (shown as 6 - level).
#include <array>
#include <string>
#include "formats/hok.hpp"

namespace em {

// The values shown on the settings page.
struct GameSettings {
    std::array<uint8_t, 14> flags{};     // the 14 switches (see above)
    std::array<uint8_t, 4> scrolling{};  // per human manager, 0/1
    int speed = 0;                       // 0..70
    int end_year = 0;                    // last season year, >= 2000 = no end
    int level = 0;                       // stored level (5 - start menu choice)
    std::string version = "V1.00";       // version text of EM.EXE ("V1.00" or "V1.30", see GameRules); set by Game::settings
};

constexpr int kSettingWidgets = 15;  // 14 flags + scrolling of the current manager

// Reads the settings from the save state.
GameSettings read_settings(const HokFile& h);
// Click on one of the 15 switches (14 = scrolling of `manager`). Returns the new value (Torszenen: 0..2).
// WM-Spiele also sets the reporting mode of the coached national team like the original (camera on/off,
// see set_team_reporting), unless it is the manager's own club. Throws FormatError for invalid arguments.
int toggle_setting(HokFile& h, int widget, int manager);
// Sets the game speed (0..70 in steps of 10); throws FormatError otherwise.
void set_game_speed(HokFile& h, int speed);

}  // namespace em
