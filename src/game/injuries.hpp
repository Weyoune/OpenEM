// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Injuries and illnesses of a human manager's squad: the injury tables read from EM.EXE and the
// random rules for injuries in training and in matches. All draws use the Borland rand
// generator (BorlandRandom), in the order of the original game.
//
// Squad record fields (0x34 bytes, see formats/hok.hpp): +0x09 bit 1 = injured (bit 0 marks a
// player serving a match penalty, whose +0x0D is the penalty length and +0x00 / +0x01 the
// penalty counts), +0x0D = weeks out (0xFF = for good), +0x17 = injury type, +0x0A = role
// (0 = none).
//
// The injury itself (draw_injury plus the record update by the caller):
//   - the player's form (player record +0x17) drops by rand(10, 15) if it is above 20
//   - type t = rand(0, 27 - 6 * kind), taken when rand(0, 15) <= weight[t], else drawn
//     again; kind 0 (training) allows all 28 types including illnesses, kind 1 (a match) 0-21
//   - weeks = rand(min_weeks[t], max_weeks[t]); for the nine career-threatening types
//     (fractures of ankle, scaphoid, jaw, kneecap, eye, spine, Achilles tendon, lung, kidney)
//     the career ends (weeks = 0xFF) when rand(0, level * 2 + 10 - min_weeks / 4) == 0
//   - +0x09 |= 2, +0x17 = t, +0x0A = 0: the player leaves his line
//   - the message text is the type name
// Training, checked twice a week (season day % 7 == 0 and (day + 3) % 7 == 0): a player with
// +0x0D == 0 and +0x09 == 0 is hurt when
//   rand(0, level * 30 + d + 100) == 0, d = max(320 - 2 * (+0x13) + (no match today ? 50 : 0), 40)
// Matches, once per match minute for a manager's team that has at least four players with a
// role: x = clamp(40 - load + 20, 0, 40) with load = manager block +0xF0 (computed from the line
// strengths: sum / 5 + 20 in 0..40; 100 = fewer than nine players, then no match injuries); a
// player is hurt when rand(0, x * 20 + level * 200 + 200) == 0. The victim is picked with up to
// 150 tries of a random slot; he must have a role and play in the line on the ice; he is taken
// with rand(0, 10) <= 3 if skill 2 < skill 1, else <= 7, a goalkeeper only when rand(0, 2) != 0.
// Healing (once a week): +0x0D counts down while bit 1 is set (0xFF never); at 0 bits 0-1 of
// +0x09 are cleared.
#include <array>
#include <string>

#include "formats/binary.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr int kInjuryTypes = 28;    // injury and illness types; 0-21 can happen in a match
constexpr int kCareerOver = 0xFF;   // weeks value of a career-ending injury

// Injury tables from the data segment of EM.EXE.
struct InjuryTables {
    std::array<int, kInjuryTypes> weight{};                 // acceptance weight 0..15 per type
    std::array<std::array<int, 2>, kInjuryTypes> weeks{};   // weeks out: min, max
    std::array<int, 9> career_types{};                      // types that can end the career
    std::array<std::string, kInjuryTypes> names;            // type names (UTF-8)
};

// Reads the injury tables; throws FormatError if they are not where EM.EXE keeps them.
InjuryTables load_injury_tables(const Bytes& exe);

// Result of draw_injury().
struct Injury {
    int type = 0;   // 0..kInjuryTypes-1
    int weeks = 0;  // weeks out; kCareerOver = the career ends
};

// Draws type and duration of an injury (without updating the squad record):
// kind 0 = training, 1 = match; `level` is the game level.
Injury draw_injury(const InjuryTables& t, int kind, int level, BorlandRandom& rng);

// Does a fit player get hurt in training? `exhaustion_raw` is squad byte +0x13. One draw.
bool training_injury(int exhaustion_raw, bool no_match_today, int level, BorlandRandom& rng);

// One match minute: is a player of the team hurt? `load` = manager block +0xF0; a load of 100
// (too few players) never injures and draws nothing.
bool match_injury_minute(int load, int level, BorlandRandom& rng);

}  // namespace em
