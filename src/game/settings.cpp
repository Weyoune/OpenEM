// SPDX-License-Identifier: GPL-3.0-only
// The settings page values in the save state and their Game wrappers; see settings.hpp.
#include "game/settings.hpp"
#include "game/game.hpp"
#include "game/reporting.hpp"

namespace em {
namespace {
// State offsets of the settings (see settings.hpp) and of the coached nation and the manager blocks.
constexpr size_t kFlags = 0xA51C, kScrolling = 0x2C, kSpeed = 0xB94F, kEndYear = 0xAEB5, kLevel = 0xAEB4;
constexpr size_t kNationalTeam = 0x68C, kManagers = 0xDCD, kManagerSize = 0x1AF;
}  // namespace

GameSettings read_settings(const HokFile& h) {
    GameSettings s;
    for (size_t i = 0; i < s.flags.size(); i++) s.flags[i] = h.state.at(kFlags + i);
    for (size_t i = 0; i < s.scrolling.size(); i++) s.scrolling[i] = h.state.at(kScrolling + i);
    s.speed = h.state.at(kSpeed) | h.state.at(kSpeed + 1) << 8;
    s.end_year = h.state.at(kEndYear) | h.state.at(kEndYear + 1) << 8;
    s.level = h.state.at(kLevel);
    return s;
}

int toggle_setting(HokFile& h, int widget, int manager) {
    if (widget < 0 || widget >= kSettingWidgets || manager < 0 || manager >= 4) throw FormatError("Settings: invalid switch");
    if (widget == 14) {
        auto& v = h.state.at(kScrolling + size_t(manager));
        v ^= 1;
        return v;
    }
    auto& v = h.state.at(kFlags + size_t(widget));
    if (widget == 9) {
        if (++v == 3) v = 0;  // the "state 2 unavailable" branch needs a flag the shipped game keeps at 0
    } else {
        v ^= 1;
    }
    if (widget == 12) {
        const int team = h.state.at(kNationalTeam);
        if (h.state.at(kManagers + size_t(manager) * kManagerSize + 0x14) != team) set_team_reporting(h, team, v << 1);
    }
    return v;
}

void set_game_speed(HokFile& h, int speed) {
    if (speed < 0 || speed > 70 || speed % 10) throw FormatError("Settings: invalid game speed");
    h.state.at(kSpeed) = uint8_t(speed);
    h.state.at(kSpeed + 1) = uint8_t(speed >> 8);
}

GameSettings Game::settings() const {
    GameSettings s = read_settings(save_);
    s.version = rules_.label();
    return s;
}
int Game::toggle_setting(int widget, int manager) { return em::toggle_setting(save_, widget, manager); }
void Game::set_game_speed(int speed) { em::set_game_speed(save_, speed); }

}  // namespace em
