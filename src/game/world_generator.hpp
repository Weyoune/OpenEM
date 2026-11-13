// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The new-game world, built the way the original program builds it from EM.EXE and KB\23.vga,
// with every random draw in the original order, so the same seeds give the same world.
//
// The original keeps the game state in its data segment plus two separately allocated buffers
// (200 player records of 0x21 bytes, 150 team records of 0x3D bytes, both zeroed at start). A new
// game runs, in this order:
//   program start:   seed 1; Borland rand(0, 2) (intro); the team records from KB\23.vga
//                    (Borland); seed 2; warm-up of the game's own generator; manager reset;
//                    league list start values; the league shuffle (game generator); the
//                    European cup entrants; competition tables cleared; session id (Borland)
//   "Los jetzt!":    managers, clubs, faces, settings; players and squads (Borland); WM reset;
//                    EuroCup groups (Borland); the manager loop (club draw, squad strengths,
//                    values, image, sponsor offers, lineup; Borland) and the free national players
//   after the menu:  club values of the season start (game generator); seed 3; player
//                    distribution; DEB cup draw; transfer list; share prices (target / 50) and
//                    90 market days
// generate_world repeats this on an image of the data segment taken from EM.EXE and writes the HOK
// state through the block map of the save format. With the same seeds and intro draws, every draw
// of both random streams and every byte of the state equal the original's, apart from the first
// game day the original's first save already contains. A few bytes no ported step writes are set
// as every original start state holds them. The date fields are set to the season start
// (10.09.1993, day 1) like in the shipped start state.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/scorers.hpp"

namespace em {

// One human manager as chosen in the start menu.
struct WorldManager {
    std::string name;  // the game's character set (ISO 646-DE), 1..18 characters (2..18 for Manager -> Neuer)
    int face = 0;      // 0..11 (manager block +0x13)
    int team = 0;      // club 0..47 (index in the team records after the league shuffle)
};

// The choices of the start menu.
struct WorldSettings {
    std::vector<WorldManager> managers;  // 1..4 human managers, in menu order
    int level = 3;       // "Level" 0..5; the game level for salaries is 5 - level
    int end_choice = 0;  // 0..2 -> last season 1994/1996/1998 (end year 1994 + 2 x), 3 -> 22222 (endless)
    int squad_size = 0;  // "Start-Liga" (0 Bundesliga, 1, 2): the start division of the club draw; also the squad
                         // group sizes (goalkeepers/defence/attack): 0 = 2/10/13, 1 = 2/9/11, 2 = 2/8/10
    int squad_strength = 1;  // "Start-Stärke" (1 = Mittelfeld): strength of the new squads, from the EXE table
                             // indexed by 3 x start league + choice
    int start_mode = 0;  // start-menu mode bits (0..3), stored as game mode 0, 2, 1, 4 (see wm_mode.hpp)
    bool cup_live = false;  // "Aber klar" to the DEB cup question: the places are uncovered live (Borland)
    bool menu_order_only = false;  // skip the manager loop (its club draw swaps names): the club order of the start menu
};

// The three random seeds the program takes from the clock (low 16 bits of time()). The game's
// own generator starts at 0 with the program and is never seeded.
struct WorldSeeds {
    uint16_t start = 1;  // before the intro and the team records
    uint16_t menu = 1;   // before the warm-up, the league shuffle and the start menu
    uint16_t after = 1;  // after the start menu
    // The intro animation draws Borland (0,7) and (0,27) for as long as it runs before a key: the number depends on
    // the player. To reproduce a particular start these draws are replayed here (their upper bounds, in order).
    std::vector<int> intro_draws;
};

// Manager -> Neuer in a running game: the new manager takes the next manager block and the club `manager.team` (an
// index of the state, not another manager's club). Then, as in the original: reporting mode 2 for the club, cup
// phase bytes +0xE6..+0xEA = 30, 50 history entries 0xFF, and the pool draft (per position group as many players as
// the start-league setting's group size: Borland draws in the group's ID range until a free player (ID < 0xB4,
// owner +0x1A = 6), at most 1000 tries, signed for one season, owned by the manager, nibbles rand(3, 12) twice,
// club, age rand(19, 31)); the transfer-list rows of his players or of a club of the same name leave the list; then
// the manager loop for him (club draw of the start division, strengths, values, image, sponsor offers, lineup).
// `rng` is the game's Borland stream and is advanced. Returns the new save; the header keeps the date and identity
// of `save` and gets the new manager count and preview row.
HokFile add_manager_to_game(const Bytes& exe, const Bytes& db, const HokFile& save, const WorldManager& manager,
                            BorlandRandom& rng);

// Everything generate_world produces.
struct WorldResult {
    HokFile save;                                    // the start save
    std::array<uint8_t, 3> distribution_cursors{};  // player distribution cursors (not part of the save)
    uint32_t game_random = 0;                        // state of the game's generator after the world
    uint32_t borland = 0;                            // Borland seed after the 90 market days
    uint32_t session_id = 0;                         // session id of the new game
    std::vector<int> cup_places, cup_reveal;         // first DEB cup round: clubs per place, uncovering order (cup_live)
    std::vector<std::string> log;                    // steps and draw counts (for debugging)
};

// `exe` = EM.EXE, `db` = KB\23.vga (36 648 bytes). Throws FormatError on invalid input.
WorldResult generate_world(const Bytes& exe, const Bytes& db, const WorldSettings& settings, const WorldSeeds& seeds);

}  // namespace em
