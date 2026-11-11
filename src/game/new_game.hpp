// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Start of a new game (start menu ovr027:0135 with argument 1, "Los jetzt!" at ovr027:15A5, then the
// new-game generator ovr040:0730 = ovl16_0730).
//
// The original builds the whole world at random: it rolls all 199 player records (age, skills, form),
// shuffles the clubs into the leagues and fills the share history. The human managers' squads take their
// names from KB\23.vga: for every manager the generator walks the three position groups (goalkeepers,
// defence, attack: DS:0169 + 4 * start strength, e.g. 2/10/13), takes player IDs upwards from each
// group's range (DS:0382 - DS:015C + 1) and copies 20 bytes of the club's database entry in slot order
// (sub_66E44: entry found by the club name) to the player record +0x00..+0x13 (+0x11 = 0), sets +0x1A =
// manager and +0x1D = club.
//
// OpenEM does not yet reproduce that random world. It starts from a fresh start state written by the
// original itself - the "Nobody" state that every installation ships as BACKUP.HOK / AUTOSAVE.HOK
// (10.09.1993, season day 1, one manager) - and applies the choices of the start menu:
//   * manager block 0 (state 0x0DCD): name +0x00 (19 letters), face +0x13, club +0x14; face DS:0178
//     (state 0x01EE); header preview 0 (file 0x12 manager, 0x25 club)
//   * club records: +0x16 = manager slot (1 for manager 0, 0 = computer), +0x34 bit 7 = human club
//   * the squad of manager 0 (block 0 of DS:5DC3): its players take the names of the new club from
//     KB\23.vga in ID order = slot order, as ovl16_0730 assigns them; vacant database slots keep the
//     generated name; +0x1D = new club.
// Values of the start state (skills, contracts, money, stadium, settings) stay as the original rolled
// them for that start. This is listed in docs/DEVIATIONS.md.
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "formats/db.hpp"
#include "formats/hok.hpp"

namespace em {

struct NewGameSetup {
    std::string name;  // native character set, 2..19 letters
    int face = 0;      // 0..11
    int team = 0;      // team index 0..47 (club board of the start menu)
};

// The settings of the start menu (ovr027:0135 with argument 1): DS:0118..011B and the menu variable var_57.
//   values[0] level choice 0..5 (DS:0ED8 = state 0xAEB4 = 5 - choice), values[1] end choice 0..3 (last season
//   1994/1996/1998, 3 = 22222: DS:5A14 = state 0xAEB5), values[2] squad size 0..2 (DS:011A), values[3] squad strength
//   0..2 (DS:011B, table DS:4B42 + 3 * league + choice for the new squads, ovr027:1D74); mode = var_57 (bit 0 league
//   and cups, bit 1 WM; DS:D0B9 = state 0x025A = 0, 2, 1, 4 for 0..3, ovr027:1808..1832).
struct NewGameOptions {
    std::array<int, 4> values{3, 3, 0, 1};  // EM.EXE dseg:0118 = 03 03 00 01
    int mode = 3;                           // ovr027:0161
    bool cup_live = false;                  // "Aber klar" to the DEB cup question of the first round (ovl05_4c7b(0, 0))
};
inline constexpr std::array<int, 4> kNewGameOptionMax{5, 3, 2, 2};  // dseg:011C = 05 03 02 02

// A press on the arrow of option row `row` (ovr027:07AF..095F): direction -1 up, +1 down. A change that would
// give end choice 0 (1994) with a squad size other than 0 is taken back; the values stay within 0..max.
void step_new_game_option(NewGameOptions& options, int row, int direction);
// A press on the mode buttons (ovr027:06C6..07A7): 0 = league and cups (refused while more than one manager row
// has a name), 1 = WM. Returns false when refused.
bool toggle_new_game_mode(NewGameOptions& options, int button, int named_managers);
// The highscore list of the settings (ovr027:096D..0A39, DS:0256 = state 0x06AE): 0 = none.
uint16_t new_game_highscore_file(const NewGameOptions& options);
// Writes the settings into a start state as "Los jetzt!" does (ovr027:1808..1863): state 0x28..0x2B, 0x025A, 0xAEB4,
// 0xAEB5 and 0x06AE. It does not regenerate anything that depends on them (salaries, balances, squads).
void apply_new_game_options(HokFile& h, const NewGameOptions& options);

// A fresh start state written by the original: year 1993, season day 1, one human manager, no match played.
bool is_new_game_base(const HokFile& h);
// BACKUP.HOK, then AUTOSAVE.HOK of the data directory, if one is a fresh start state.
std::optional<std::filesystem::path> find_new_game_base(const std::filesystem::path& data_directory);
// The start state with manager 0 replaced by `setup` (throws FormatError on invalid input).
HokFile start_new_game(const HokFile& base, const Database& db, const NewGameSetup& setup);
// The same with the start menu settings written into the state (apply_new_game_options).
HokFile start_new_game(const HokFile& base, const Database& db, const NewGameSetup& setup, const NewGameOptions& options);

// A new game with two to four managers (hot seat). The shipped start state has a squad for one manager only, so the
// world is built by the new-game generator (game/world_generator.hpp, three seeds = `seed`). The start menu shows the
// clubs in the order of `base`; each chosen club is looked up by its name in the generated world (the league shuffle
// keeps every club in its division). The settings come from `options` (start menu: level, end
// choice, squad size, mode; DS:011B and DS:0256 are written afterwards), else DS:0118..011A of `base`. `db_raw` = KB\23.vga.
HokFile start_new_world(const Bytes& exe, const Bytes& db_raw, const HokFile& base, const std::vector<NewGameSetup>& managers,
                        uint16_t seed, const NewGameOptions* options = nullptr, std::vector<int>* cup_places = nullptr,
                        std::vector<int>* cup_reveal = nullptr, std::array<uint32_t, 2>* random_states = nullptr);
// random_states: Borland and GameRandom at the end of the generator; the original's day loop goes on with both (REAL-CODE)

}  // namespace em
