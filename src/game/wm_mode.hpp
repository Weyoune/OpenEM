// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The standalone world championship ("Nur WM", manual pp. 12, 45-48; docs/logic/WM.md "WM-Modus").
//
// Start menu ovr027:0135: the two mode icons are the bits of the menu variable var_57 (default 3): bit 0 Liga/Cup
// (icon at x 0xC1, toggled by a left click with x < 0xFE, only while at most one manager row is filled), bit 1 WM
// (icon at x 0x103). "Los jetzt!" needs var_57 != 0 (ovr027:15AF). ovr027:1808 stores DS:D0B9 (state 0x025A):
//   var_57 = 1 (league/cups only) -> 2, var_57 = 2 (WM only) -> 1, var_57 = 3 (both) -> 4, 0 -> 0.
// With var_57 = 2 the highscore list is H.30000 (DS:0256 = 0x7530, state 0x06AE, ovr027:0A0A).
//
// Day loop ovl14_2e0f (ovr038:2E5E): in the WM-only mode every entry skips the club calendar - day counter DS:03B7
// and cursor DS:A969 run on without playing anything until the first WM day (calendar byte 0x10) - sets the
// appointment flag DS:D0BA[0] (state 0x0688) and goes to the trainer appointment (ovr038:356F): manager block 5 gets
// the manager's name, the nation DS:06AC (state 0x068C, 56 = Deutschland), the grant DS:029C on its account, the
// nation record +0x16 = 6. The menu then runs as the national coach (byte_2F6DE = 5). After the final ovr038:3972
// sets the flag again, so the next season starts with the next appointment and the placement grant.
//
// The national coach's menu (ovr034:1E6D, byte_2F6DE = 5) uses the second availability table DS:5159 + 0x2A
// (6 bytes per menu, 1 = night sky, not selectable): Manager all off except Highscore (on when a list exists and the
// mode is not "Liga/WM", ovr034:1EDC), Mannschaft without Verträge, Statistiken/Tabellen/Paarungen only WM (item 5),
// Stärken only WM (item 3), Gemischtes complete. Mannschaft -> Transfer opens the WM squad (ovl07_29ac), Training
// the friendlies (ovl07_1f27).
//
// Friendlies ovl07_1f27 / ovl05_0e35: at most 8 (DS:D0C1, state 0x068D), against any of the eleven other nations.
// Price DS:026C[nation - 52] (state 0x0658, raw = DM x 3), doubled for a home game, paid from the national account
// (manager block 5 +0x18F). q = (price / 2000 + rand(0, 50)) / 50 is drawn before the match; after it the goal
// difference d gives the squad bonus a (see friendly_bonus); with a > 0 every nominee's current skills and form
// (squad +0x10..+0x12) rise by a, one of them by a - 1, capped at 99. Opening the page sets every nominee's
// exhaustion +0x13 to 70. The results are kept in state 0x068E/0x0696/0x069E/0x06A6 (home, away, goals).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/hok.hpp"
#include "game/new_game.hpp"

namespace em {

class BorlandRandom;

constexpr size_t kStateGameMode = 0x025A;      // DS:D0B9
constexpr uint8_t kModeWmOnly = 1, kModeLeagueOnly = 2, kModeLeagueAndWm = 4;
constexpr int kStartModeLeague = 1, kStartModeWm = 2, kStartModeBoth = 3;  // var_57 bits
constexpr uint16_t kWmOnlyHighscoreFile = 0x7530;  // ovr027:0A0A
constexpr int kMaxFriendlies = 8;

// DS:D0B9 for the start-menu bits (ovr027:1808).
uint8_t native_game_mode(int start_bits);
bool wm_only_mode(const HokFile& h);
// ovr027:096D: the notepad (a highscore list is kept) for the start-menu bits and the options DS:0118..011B.
bool start_menu_highscore(int start_bits, const std::array<uint8_t, 4>& options);
// The start menu's mode written into a fresh start state: DS:D0B9 and, for "Nur WM", the list H.30000.
void apply_start_mode(HokFile& h, int start_bits);
// A new "Nur WM" game: the fresh start state of new_game.hpp with the mode applied (one manager only).
HokFile start_wm_only_game(const HokFile& base, const Database& db, const NewGameSetup& setup);

// DS:5159 (club table) and DS:5159 + 0x2A (national coach), 6 entries per menu, 1 = not selectable.
struct MenuAvailability {
    std::array<std::array<uint8_t, 42>, 2> table{};
};
MenuAvailability load_menu_availability(const Bytes& exe);
// Whether a main-menu item is drawn as night sky and ignored. `national` = the national coach's view (byte_2F6DE = 5);
// `wm_started`: the tournament is running (Transfer and Training of the national coach then stay closed, as the native
// NATLIVQ menu shows; the switch itself was not located in the listing).
bool menu_item_disabled(const MenuAvailability& m, const HokFile& h, bool national, int managers, int menu, int item,
                        bool wm_started);

// Friendlies.
long friendly_price(const HokFile& h, int nation, bool home);  // raw (DM x 3)
struct FriendlyRecord {
    int home = -1, away = -1, home_goals = 0, away_goals = 0;
};
std::vector<FriendlyRecord> friendly_records(const HokFile& h);
// ovl07_1f27 after the match (ovr031:26C7..27B0): `difference` = own goals - opponent goals, q as above.
// Draws rand(1, q) on an away game with q > 0.
int friendly_bonus(int difference, int q, bool away, BorlandRandom& rng);
// Applies the bonus to the nominees of manager block 5 (ovr031:27BC..2875).
void apply_friendly_bonus(HokFile& h, int bonus, int q);

}  // namespace em
