// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The new-game world of EM.EXE, built the way the original builds it (docs/logic/WELTGENERATOR.md).
//
// The original keeps the game state in DGROUP and two heap blocks (players DS:8253 -> 200 x 0x21,
// teams DS:9657 -> 150 x 0x3D, both zeroed by _main_0). A new game runs, in this order:
//   _main_0 (ovr034:0685):  srand(time) [seed 1]; Borland rand(0, 2) (intro); ovl04_4d3b team records
//                           from KB\23.vga (Borland); srand(time) [seed 2]; ovl00_0539 GameRandom
//                           warm-up; ovl10_05c4; DS:5CCB = 2, DS:5CCD = 0, DS:D0E6 = 2; ovl06_099d
//                           league shuffle (GameRandom); ovl05_51a5; ovl05_5a4d; session id (Borland)
//   start menu ovr027:15A5 "Los jetzt!": managers, clubs, faces, settings; ovl16_0730 players and
//                           squads (Borland); ovl05_194b(0) WM schedule; ovl05_1e6f(0) EuroCup groups
//                           (Borland); the manager loop ovr027:1B27..2076 (club draw, squad strengths,
//                           values, image, sponsor offers, lineup; Borland) and ovl16_0480(0)
//   _main_0 after the menu: 162c:1c5b(10) club values (GameRandom); srand(time) [seed 3];
//                           1238:1663 player distribution; ovl05_4c7b(0, 0) DEB cup draw;
//                           ovl11_34ef transfer list; share prices DS:01FD / 50 and 90 market days
// generate_world repeats this on an image of DGROUP taken from EM.EXE and writes the HOK state
// through the block map of the save writer ovl16_11bd. REAL-CODE new game of 11.11.2026 (seeds
// 0x4880, intro draws replayed): every Borland and GameRandom draw and every byte of the state
// equal to the original, apart from the first game day the recorded state already contains.
// A few bytes no ported step writes are set as every original start state holds them. The date
// fields are set to the season start (10.09.1993, day 1) like in the shipped start state.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/scorers.hpp"

namespace em {

struct WorldManager {
    std::string name;  // native character set (ISO 646-DE), 1..19 characters
    int face = 0;      // 0..11 (DS:0178 + manager, manager block +0x13)
    int team = 0;      // club 0..35 (index in the team records after the league shuffle)
};

// The choices of the start menu (ovr027:0135, argument 1).
struct WorldSettings {
    std::vector<WorldManager> managers;  // 1..4 human managers, in menu order
    int level = 3;       // DS:0118: DS:0ED8 (game level for salaries) = 5 - level
    int end_choice = 0;  // DS:0119: 0..2 -> last season 1994/1996/1998 (DS:5A14 = 0x7CA + 2 x), 3 -> 22222
    int squad_size = 0;  // DS:011A "Start-Liga" (0 Bundesliga, 1, 2): the start division of the club draw; also the squad group sizes DS:0169 + 4 x: 0 = 2/10/13, 1 = 2/9/11, 2 = 2/8/10
    int squad_strength = 1;  // DS:011B "Start-Stärke" (1 = Mittelfeld): strength of the new squads, DS:4B42 + 3 x DS:011A + choice
    int start_mode = 0;  // menu variable var_57 (0..3): DS:D0B9 = 0, 2, 1, 4
    bool cup_live = false;  // "Aber klar" to the DEB cup question of ovl05_4c7b(0, 0): the places are uncovered (Borland)
    bool menu_order_only = false;  // skip the manager loop (its club draw swaps names): the club order of the start menu
};

// The three srand(time()) seeds (low 16 bits of time(), _srand 1000:0B4A via ovl00_0571).
// GameRandom (DS:E017) starts at 0 with the program and is never seeded.
struct WorldSeeds {
    uint16_t start = 1;  // ovr034:0708, before the intro and the team records
    uint16_t menu = 1;   // ovr034:08A5, before the warm-up, the league shuffle and the start menu
    uint16_t after = 1;  // ovr034:0929, after the start menu
    // The intro animation (ovr034:0721..074D, sub_24D7E) draws Borland (0,7) and (0,27) for as long as it runs before a key:
    // the number depends on the player. For a comparison with a recorded start these draws are replayed here (upper bounds).
    std::vector<int> intro_draws;
};

// Manager -> Neuer in a running game (ovr027 with var_64 = old manager count, Amiga FUN_001075e8 line 6247): the new
// manager takes the next manager block and the club `manager.team` (an index of the state, no manager's club), then
// sub_1902B, +0xE6..+0xEA = 30, 50 history entries 0xFF, the pool draft (per position group DS:0169[4 x DS:011A + g]
// players: Borland rand(DS:0381 + g, DS:0382 + g - 1) until a free player (ID < 0xB4, +0x1A = 6), at most 1000 tries,
// signed for one season with ovl09_2cd1, owner, nibbles rand(3, 12), club, age rand(19, 31)), the transfer-list rows of
// his players or of a club of the same name leave the list (sub_24BB8), then the manager loop for him (club draw of the
// start division, strengths, values, image, sponsor offers, lineup). `rng` is the game's Borland stream, advanced.
HokFile add_manager_to_game(const Bytes& exe, const Bytes& db, const HokFile& save, const WorldManager& manager,
                            BorlandRandom& rng);

struct WorldResult {
    HokFile save;
    std::array<uint8_t, 3> distribution_cursors{};  // DS:48FB (not part of the save)
    uint32_t game_random = 0;                        // DS:E017 after the generator
    uint32_t borland = 0;                            // Borland seed after the 90 market days
    uint32_t session_id = 0;                         // DS:02C4
    std::vector<int> cup_places, cup_reveal;         // first DEB cup round: clubs per place, uncovering order (cup_live)
    std::vector<std::string> log;                    // steps and draw counts (for debugging)
};

// `exe` = EM.EXE, `db` = KB\23.vga (36 648 bytes). Throws FormatError on invalid input.
WorldResult generate_world(const Bytes& exe, const Bytes& db, const WorldSettings& settings, const WorldSeeds& seeds);

}  // namespace em
