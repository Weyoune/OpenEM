// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Start of a new game: the settings of the start menu ("Neues Spiel", confirmed with "Los jetzt!")
// and two ways to build the starting world.
//
// The original builds the whole world at random: it rolls all 199 player records (age, skills, form),
// shuffles the clubs into the leagues and fills the share history. The human managers' squads take their
// names from KB\23.vga: for every manager the generator walks the three position groups (goalkeepers,
// defence, attack; the group sizes depend on the chosen squad strength, e.g. 2/10/13), takes player IDs
// upwards from each group's range and copies 20 bytes of the club's database entry (found by the club
// name) in slot order to the player record +0x00..+0x13 (+0x11 = 0), sets +0x1A = manager and +0x1D = club.
//
// start_new_world reproduces that generator (game/world_generator.hpp) and is used for any number of
// managers. start_new_game is the simpler single-manager path: it starts from a fresh start state
// written by the original itself - the "Nobody" state that every installation ships as BACKUP.HOK /
// AUTOSAVE.HOK (10.09.1993, season day 1, one manager) - and applies the choices of the start menu:
//   * manager block 0 (state 0x0DCD): name +0x00 (19 letters), face +0x13, club +0x14; the face is also
//     stored at state 0x01EE; header preview 0 (file 0x12 manager, 0x25 club)
//   * club records: +0x16 = manager slot (1 for manager 0, 0 = computer), +0x34 bit 7 = human club
//   * the squad of manager 0 (squad block 0): its players take the names of the new club from
//     KB\23.vga in ID order = slot order, as the generator assigns them; vacant database slots keep the
//     generated name; +0x1D = new club.
// On that path the values of the start state (skills, contracts, stadium) stay as the original rolled
// them for that start; this is a known deviation from the original.
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "formats/db.hpp"
#include "formats/hok.hpp"

namespace em {

// One human manager as entered in the start menu.
struct NewGameSetup {
    std::string name;  // native character set, 2..19 letters
    int face = 0;      // 0..11
    int team = 0;      // team index 0..47 (club board of the start menu)
};

// The settings of the start menu, saved at state 0x28..0x2B plus the game mode:
//   values[0] level choice 0..5 (state 0xAEB4 = 5 - choice), values[1] end choice 0..3 (last season
//   1994/1996/1998, 3 = 22222, i.e. no end; state 0xAEB5), values[2] squad size 0..2, values[3] squad
//   strength 0..2 (selects the strength of the generated squads per league); mode: bit 0 league and
//   cups, bit 1 WM (stored at state 0x025A as 0, 2, 1, 4 for modes 0..3).
struct NewGameOptions {
    std::array<int, 4> values{3, 3, 0, 1};  // the defaults stored in EM.EXE
    int mode = 3;                           // league and cups plus WM
    bool cup_live = false;                  // "Aber klar" to the DEB cup question of the first round
    std::vector<int> intro_draws;           // random draws the intro animation made before the seeds (WorldSeeds);
                                            // only needed to reproduce one particular original start exactly
};
inline constexpr std::array<int, 4> kNewGameOptionMax{5, 3, 2, 2};  // upper limit of each value (from EM.EXE)

// A press on the arrow of option row `row` (0..3): direction -1 up, +1 down. A change that would
// give end choice 0 (1994) with a squad size other than 0 is taken back; the values stay within 0..max.
// Throws FormatError for an invalid row or direction.
void step_new_game_option(NewGameOptions& options, int row, int direction);
// A press on the mode buttons: 0 = league and cups (refused while more than one manager row has a
// name), 1 = WM. Each press toggles its mode bit. Returns false when refused.
bool toggle_new_game_mode(NewGameOptions& options, int button, int named_managers);
// The highscore list the settings qualify for (stored at state 0x06AE): 0 = none, 30000 for WM-only
// games, otherwise level + (end << 2) + (size << 5) + (strength << 7) - 2. League games qualify only
// with level choice > 2 and either squad strength 1 or end choice 0 with squad size 0.
uint16_t new_game_highscore_file(const NewGameOptions& options);
// Writes the settings into a start state as "Los jetzt!" does: state 0x28..0x2B, 0x025A, 0xAEB4,
// 0xAEB5 (u16 last season year) and 0x06AE. It does not regenerate anything that depends on them
// (salaries, balances, squads). Throws FormatError on out-of-range values.
void apply_new_game_options(HokFile& h, const NewGameOptions& options);

// A fresh start state written by the original: year 1993, season day 1, one human manager, no match played.
bool is_new_game_base(const HokFile& h);
// BACKUP.HOK, then AUTOSAVE.HOK of the data directory, if one is a fresh start state.
std::optional<std::filesystem::path> find_new_game_base(const std::filesystem::path& data_directory);
// The start state with manager 0 replaced by `setup` (throws FormatError on invalid input).
HokFile start_new_game(const HokFile& base, const Database& db, const NewGameSetup& setup);
// The same with the start menu settings written into the state (apply_new_game_options); the
// manager's balance (block +0x18F) is set for the chosen level as the original does:
// (5 - level choice) * 150000 + 1200000 in the internal money unit.
HokFile start_new_game(const HokFile& base, const Database& db, const NewGameSetup& setup, const NewGameOptions& options);

// A new game with one to four managers (hot seat). The shipped start state has a squad for one manager only, so the
// world is built by the new-game generator (game/world_generator.hpp, all three seeds = `seed`). The start menu shows the
// clubs in the order of `base`; each chosen club is looked up by its name in the generated world (the league shuffle
// keeps every club in its division). The settings come from `options` (start menu: level, end
// choice, squad size, mode; the highscore list and the other menu values are written afterwards), else from
// state 0x28..0x2B of `base`. `db_raw` = KB\23.vga. Manager names are cut to 18 letters as in the original.
// Optional outputs: `cup_places` / `cup_reveal` the generator's DEB cup draw data, `random_states` the
// states of the Borland and the game random generator at the end of the generator; the original's day
// loop continues with both.
HokFile start_new_world(const Bytes& exe, const Bytes& db_raw, const HokFile& base, const std::vector<NewGameSetup>& managers,
                        uint16_t seed, const NewGameOptions* options = nullptr, std::vector<int>* cup_places = nullptr,
                        std::vector<int>* cup_reveal = nullptr, std::array<uint32_t, 2>* random_states = nullptr);

}  // namespace em
