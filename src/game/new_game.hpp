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
#include <filesystem>
#include <optional>
#include <string>

#include "formats/db.hpp"
#include "formats/hok.hpp"

namespace em {

struct NewGameSetup {
    std::string name;  // native character set, 2..19 letters
    int face = 0;      // 0..11
    int team = 0;      // team index 0..47 (club board of the start menu)
};

// A fresh start state written by the original: year 1993, season day 1, one human manager, no match played.
bool is_new_game_base(const HokFile& h);
// BACKUP.HOK, then AUTOSAVE.HOK of the data directory, if one is a fresh start state.
std::optional<std::filesystem::path> find_new_game_base(const std::filesystem::path& data_directory);
// The start state with manager 0 replaced by `setup` (throws FormatError on invalid input).
HokFile start_new_game(const HokFile& base, const Database& db, const NewGameSetup& setup);

}  // namespace em
