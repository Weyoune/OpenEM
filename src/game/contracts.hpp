// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Market value, salary and contract renewal of a player. Amounts are raw
// (shown Deutschmark times 3), like everywhere in the game.
//
// The game computes value and salary with one routine; its mode selects market value or salary and, for the
// value, an optional division by 3. It runs for every squad record when a game starts (value -> +0x2C,
// salary -> +0x28) and on the contract and transfer screens. With w = the six weights of the kind
// (value 58 4 4 5 4 25, salary 70 8 7 5 8 2; a table in EM.EXE) and the squad record fields (see hok.hpp):
//   p  = (skill1 + skill2) / 2 * w0 / 100
//      + w1 * (goals * 100 / 20) / 100     goals = min((+0x03 * 30 + +0x04 * 30 + +0x05 * 40) / 100, 20)
//      + w2 * (games * 100 / 200) / 100    games = min((+0x1C * 50 + +0x1E * 20 + +0x20 * 30) / 100, 200)
//      + experience (+0x0E) * w3 / 100
//      + w4 * (bonus * 100 / 20) / 100     bonus = clamp(+0x00 * 6 + +0x01, 0, 20)
//      + w5 * (youth * 100 / 27) / 100     youth = clamp(45 - age, 0, 27)
//   salary: p = p^3 / (level * 200 + 1400) (level = the game level); with s = skill1 + skill2 above 100 halved
//           beyond 100: p = p * (s - 35) / 100 when s > 135; at least 2; p *= 85
//   value:  p *= 5000, minus 50000 above 55000; mode bit 1: / 3; flag +0x09 bit 7: * 1.6 if
//           +0x16 > 63, else * rand(95, 100) / 100
//   both:   p = p * 10 / 3; goalkeepers (position 0) * 1.6
//   salary: (p + league_bonus[class]) / 300 * 300 (league bonus 2400 1800 990; class 0 = first
//           division, 1 = second, 2 = Oberliga, by the team index)
//   value:  p / 3000 * 3000
// Checked: all 25 values and salaries of the Köln squad in the START save.
//
// Renewal ("Vertragsverhandlung"): the player asks for
//   demand = salary * mood / 100 * (((years - 1) * 100 + 100 - matchday * 100 / matchdays) / 6
//            + 122) * 8 / 10 + 4) / 100
//   (mood = rand(85, 115), drawn anew every day; matchday / matchdays of the
//   manager's league), after season day 232 times (152 - age) / 100.
//   On game levels above 0 the demand is accepted as it is; on level 0 the manager may type an
//   amount. Record +0x30 is optional monthly injury insurance, toggled separately
//   in the third dialog row; a new uninsured contract leaves it zero.
#include <array>
#include <cstdint>
#include <functional>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/scorers.hpp"

namespace em {

// Constants of the value and salary formula, read from EM.EXE. The offsets are relative to the start of the
// program's initialised data in the file (file offset 0x16DD0).
struct ContractTables {
    std::array<std::array<int, 6>, 2> weights{};  // +0x5312, 2 x 6 bytes: [0] value, [1] salary
    std::array<long, 3> league_bonus{};           // +0x52C2, 3 x i32: salary bonus per league class
    int level = 2;                                // +0x0ED8: game level (difficulty)
    std::array<int, 3> class_limit{};             // +0x07C1, +0x07C2, +0x06AD: first team index of the 2nd and 3rd
                                                  // class, last team index of the 3rd class
};

// Reads the tables; throws FormatError when the weights do not match V1.00 (or the file is too short).
ContractTables load_contract_tables(const Bytes& exe);

// The squad record fields the value/salary formula reads, plus the player's age, position and team.
struct ContractPlayer {
    int bonus_a = 0, bonus_b = 0;                 // +0x00, +0x01
    int league_goals = 0, cup_goals = 0, other_goals = 0;  // +0x03, +0x04, +0x05
    int flags = 0;                                // +0x09
    int years = 0;                                // +0x0B
    int experience = 0;                           // +0x0E
    int skill1 = 0, skill2 = 0;                   // +0x10, +0x11
    int open16 = 0;                               // +0x16
    std::array<int, 3> games{};                   // +0x1C, +0x1E, +0x20
    long salary = 0, value = 0;                   // +0x28, +0x2C (raw)
    int age = 0, position = 0, team = -1;         // player record +0x14, +0x18, +0x1D
    int player = -1;  // global player index for a contract decision; not a serialized field
};

// Squad slot `slot` of manager `m` (block 4 = the pool); nullopt-like: player < 0 if free.
ContractPlayer contract_player(const HokFile& h, int m, int slot, int* player_index = nullptr);

enum class Worth { Value = 0, Salary = 1 };  // what player_worth computes
// A random draw rand(lo, hi), both ends inclusive, from the game's generator.
using ContractRandomDraw=std::function<int(int,int)>;
// Market value or monthly salary (raw, x3) by the formula above; `value_third` is the value mode that divides by 3.
// `rng` is only used for the value of flagged players.
long player_worth(const ContractTables& t, const ContractPlayer& p, Worth kind, BorlandRandom& rng,
                  bool value_third = false);
long player_worth(const ContractTables&,const ContractPlayer&,Worth,const ContractRandomDraw&,
                  bool value_third=false);

// Contract negotiation: salary the player asks for a new contract of `years` seasons (formula above), given the
// day's mood (85..115), the manager's league matchday / number of matchdays and the season day.
long renewal_demand(const ContractTables& t, const ContractPlayer& p, int years, int mood, int matchday,
                    int matchdays, int season_day, BorlandRandom& rng);
// The same dialog at the season change for other years than the proposal: the
// day's mood, the rest of the season of the new season (matchday 0: 100 %) and, after season day 232, the age factor
// (152 - age) / 100 - with the age of the player in squad slot 3 of manager `m`, not of the negotiating player: the
// original reuses a loop counter that the preceding code leaves at 3 (a bug of the original that the player sees in
// the offered salary, reproduced here).
long season_change_demand(const ContractTables& t, const HokFile& h, int m, const ContractPlayer& p, int years, int mood,
                          BorlandRandom& rng);

}  // namespace em
