// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Market value, salary and contract renewal of a player (overlay 11 of EM.EXE). Amounts are raw
// (shown Deutschmark times 3), like everywhere in the game.
//
// ovl11_3d36(record, mode): mode bit 0 = 0 market value / 1 salary, bit 1 = value divided by 3.
// Called for every squad record when a game starts (ovl03_0135: value -> +0x2C, salary -> +0x28)
// and by the contract and transfer screens. With w = the six weights at DS:5312 + kind * 6
// (value 58 4 4 5 4 25, salary 70 8 7 5 8 2) and the squad record fields (see hok.hpp):
//   p  = (skill1 + skill2) / 2 * w0 / 100
//      + w1 * (goals * 100 / 20) / 100     goals = min((+0x03 * 30 + +0x04 * 30 + +0x05 * 40) / 100, 20)
//      + w2 * (games * 100 / 200) / 100    games = min((+0x1C * 50 + +0x1E * 20 + +0x20 * 30) / 100, 200)
//      + experience (+0x0E) * w3 / 100
//      + w4 * (bonus * 100 / 20) / 100     bonus = clamp(+0x00 * 6 + +0x01, 0, 20)
//      + w5 * (youth * 100 / 27) / 100     youth = clamp(45 - age, 0, 27)
//   salary: p = p^3 / (level (DS:0ED8) * 200 + 1400); with s = skill1 + skill2 above 100 halved
//           beyond 100: p = p * (s - 35) / 100 when s > 135; at least 2; p *= 85
//   value:  p *= 5000, minus 50000 above 55000; mode bit 1: / 3; flag +0x09 bit 7: * 1.6 if
//           +0x16 > 63, else * rand(95, 100) / 100
//   both:   p = p * 10 / 3; goalkeepers (position 0) * 1.6
//   salary: (p + league_bonus[class]) / 300 * 300 (DS:52C2: 2400 1800 990; class of the team as in
//           ovl05_5aa5 / 2)
//   value:  p / 3000 * 3000
// Checked: all 25 values and salaries of the Köln squad in START.
//
// Renewal, ovl11_41db ("Vertragsverhandlung"): the player asks for
//   demand = salary * mood / 100 * (((years - 1) * 100 + 100 - matchday * 100 / matchdays) / 6
//            + 122) * 8 / 10 + 4) / 100
//   (mood DS:E1A4 = rand(85, 115), drawn every day by ovl10_1cd2; matchday / matchdays of the
//   manager's league from DS:07A9 / DS:07B6), after season day 232 times (152 - age) / 100.
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

struct ContractTables {
    std::array<std::array<int, 6>, 2> weights{};  // DS:5312: [0] value, [1] salary
    std::array<long, 3> league_bonus{};           // DS:52C2
    int level = 2;                                // DS:0ED8
    std::array<int, 3> class_limit{};             // DS:07C1, 07C2, 06AD
};

ContractTables load_contract_tables(const Bytes& exe);

// The squad record fields ovl11_3d36 reads, plus the player's age, position and team.
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

enum class Worth { Value = 0, Salary = 1 };
using ContractRandomDraw=std::function<int(int,int)>;
// ovl11_3d36. `rng` is only used for the value of flagged players.
long player_worth(const ContractTables& t, const ContractPlayer& p, Worth kind, BorlandRandom& rng,
                  bool value_third = false);
long player_worth(const ContractTables&,const ContractPlayer&,Worth,const ContractRandomDraw&,
                  bool value_third=false);

// ovl11_41db: salary the player asks for a new contract of `years` seasons.
long renewal_demand(const ContractTables& t, const ContractPlayer& p, int years, int mood, int matchday,
                    int matchdays, int season_day, BorlandRandom& rng);

}  // namespace em
