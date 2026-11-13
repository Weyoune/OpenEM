// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The standalone world championship ("Nur WM", manual pp. 12, 45-48) and the national coach's
// side of the game: the game mode chosen in the start menu, the main-menu items the national coach
// may use, and the friendlies before the tournament.
//
// Start menu: the two mode icons are two bits (default 3, both on): bit 0 Liga/Cup (its icon is
// toggled by a left click only while at most one manager row is filled), bit 1 WM. "Los jetzt!"
// needs at least one bit. The game mode stored in the save (state 0x025A) is:
//   bits 1 (league/cups only) -> 2, bits 2 (WM only) -> 1, bits 3 (both) -> 4, 0 -> 0.
// "Nur WM" keeps its highscore list in H.30000 (file number 30000 = 0x7530 at state 0x06AE).
//
// Day loop in the WM-only mode: every entry skips the club calendar - the day counter runs on
// without playing anything until the first WM day (calendar event 0x10) - sets the appointment
// flag (state 0x0688) and runs the coach appointment: manager block 5 gets the manager's name, the
// nation (state 0x068C, 56 = Deutschland), the appointment grant on its account, and the nation's
// team record +0x16 = 6. The menu then runs as the national coach. After the final the flag is set
// again, so the next season starts with the next appointment and the placement grant.
//
// The national coach's main menu uses the second availability table from EM.EXE (6 bytes per
// menu, 1 = drawn as night sky, not selectable): Manager all off except Highscore (on when a list
// exists and the mode is not "Liga/WM"), Mannschaft without Verträge, Statistiken/Tabellen/
// Paarungen only WM (item 5), Stärken only WM (item 3), Gemischtes complete. Mannschaft ->
// Transfer opens the WM squad nomination, Training the friendlies.
//
// Friendlies: at most 8 (count at state 0x068D), against any of the eleven other nations. The
// price is an i32 per nation at state 0x0658 + 4 * (nation - 52) (raw = DM x 3), doubled for a
// home game, paid from the national account (manager block 5 +0x18F). q = (price / 2000 +
// rand(0, 50)) / 50 is drawn before the match; after it the goal difference gives the squad bonus
// a (see friendly_bonus); with a > 0 every nominee's current skills and form (squad record
// +0x10..+0x12) rise by a, one of them by a - 1, capped at 99. Opening the page sets every
// nominee's exhaustion (+0x13) to 70. The results are kept in four u8[8] arrays at state
// 0x068E / 0x0696 / 0x069E / 0x06A6 (home nation, away nation, home goals, away goals).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/hok.hpp"
#include "game/new_game.hpp"

namespace em {

class BorlandRandom;

constexpr size_t kStateGameMode = 0x025A;      // save state byte: the game mode (kMode*, 0 = none)
constexpr uint8_t kModeWmOnly = 1, kModeLeagueOnly = 2, kModeLeagueAndWm = 4;  // values of kStateGameMode
constexpr int kStartModeLeague = 1, kStartModeWm = 2, kStartModeBoth = 3;  // start-menu icon bits
constexpr uint16_t kWmOnlyHighscoreFile = 0x7530;  // highscore file number of "Nur WM" (H.30000)
constexpr int kMaxFriendlies = 8;                  // friendlies allowed per season

// The stored game mode (kStateGameMode value) for the start-menu bits.
uint8_t native_game_mode(int start_bits);
// Whether the save is a "Nur WM" game.
bool wm_only_mode(const HokFile& h);
// Whether the start menu shows the notepad, i.e. a highscore list is kept, for the start-menu
// bits and the four start options (level, duration, start league, strength): always for "Nur WM",
// otherwise with the league bit, level > 2 and (strength 1, or duration 0 with start league 0).
bool start_menu_highscore(int start_bits, const std::array<uint8_t, 4>& options);
// Writes the start menu's mode into a fresh start state: the game mode and, for "Nur WM", the
// highscore file number 30000.
void apply_start_mode(HokFile& h, int start_bits);
// A new "Nur WM" game: the fresh start state of new_game.hpp with the mode applied (one manager only).
HokFile start_wm_only_game(const HokFile& base, const Database& db, const NewGameSetup& setup);

// The main-menu availability tables read from EM.EXE: table 0 for club managers, table 1 for the
// national coach; 7 menus x 6 items each (index menu * 6 + item), non-zero = not selectable.
struct MenuAvailability {
    std::array<std::array<uint8_t, 42>, 2> table{};
};
MenuAvailability load_menu_availability(const Bytes& exe);
// Whether a main-menu item is drawn as night sky and ignored. `national` = the national coach's
// view; `managers` = number of human managers; `wm_started`: the tournament is running (Transfer
// and Training of the national coach then stay closed, as the original's menu shows). Besides the
// table, the game adjusts single items before drawing the menu: Highscore needs a list (and is off
// for the national coach in the "Liga/WM" mode); for club managers in league-only games the WM
// items are off, "Neuer" is off with four managers, and "Werbung" is off in a game that ends in
// 1994 (end year 0x7CA at state 0xAEB5). Out-of-range menu/item counts as disabled.
bool menu_item_disabled(const MenuAvailability& m, const HokFile& h, bool national, int managers, int menu, int item,
                        bool wm_started);

// Friendlies.
// Price of a friendly against `nation` (52..63), raw (DM x 3); doubled for a home game; 0 for
// an invalid nation.
long friendly_price(const HokFile& h, int nation, bool home);
// One played friendly (team indices of the nations and the goals).
struct FriendlyRecord {
    int home = -1, away = -1, home_goals = 0, away_goals = 0;
};
// The friendlies played this season, in order (at most kMaxFriendlies).
std::vector<FriendlyRecord> friendly_records(const HokFile& h);
// The squad bonus after a friendly: `difference` = own goals - opponent goals, q as above.
// An away game with q > 0 adds rand(1, q) (the only draw), otherwise one is subtracted; q < 2
// lowers it further, q > 2 adds q. The value is truncated to a signed byte, clamped to
// (q > 2 ? 1 : 0) .. (q < 2 ? 1 : 2), and q > 3 adds one more.
int friendly_bonus(int difference, int q, bool away, BorlandRandom& rng);
// Applies the bonus to the nominees of manager block 5 (the first <nominee count> squad rows):
// both skills and form rise by `bonus`, one of them by bonus - 1 (with q > 2 skill 1, else
// skill 2), capped at 99. Nothing happens for bonus <= 0.
void apply_friendly_bonus(HokFile& h, int bonus, int q);

}  // namespace em
