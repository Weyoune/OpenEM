// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Finances of a human manager: monthly income and expenses, the daily interest booking, the
// spectators and ticket money of a home game and the weekly stadium upkeep/construction. The
// structures here are filled from a savegame (budget_from_save), changed by the daily simulation
// in Game and written back (store_budget).
//
// All amounts here are RAW values, i.e. the shown Deutschmark amount times 3 (see hok.hpp);
// divide by 3 (truncating) for the screen.
//
// Manager block in the HOK state: 0x0DCD + manager * 0x1AF (see manager_layout.hpp). Offsets in
// the block:
//   +0x000 manager name, +0x014 u8 team index, +0x0EB u8 league 0..2,
//   +0x0E4 u8 ticket price in DM (10..30), +0x0ED u8 home games played,
//   +0x0FE u8[] attendance / 1000 of each home game, +0x114 / +0x115 opponent of the best /
//   worst attendance, +0x116 u8[9] stadium extension levels, stride 2, +0x133 u16[2]
//   spectator places of two kinds, stride 4 (sum = capacity; maintenance 15 / 12 each),
//   +0x18F i32 balance, +0x193 i32 recovery costs of the training (booked weekly),
//   +0x197 i32 monthly income as last computed, +0x19B i32[5] debt matrix: entry [p] of
//   manager m is what party p owes m (4 = the bank; > 0 = m has lent money to p / has savings at
//   the bank, < 0 = m owes p). Party p keeps the negated value.
//
// Monthly income (monthly_income):
//   strength = skill1[0] + skill1[1] + skill2[0] + skill2[1] of the manager's team
//   income   = strength * 15 * players / 3 + sum of the 11 advertising contracts (10 bytes
//              each: i32 amount, +5 months left, +6 flags)
//   full:    + 1 % of the savings at the bank, + 2.5 % of money lent to other managers
//   clamped at >= 0 and stored in the manager block (+0x197). "players" is the number of squad
//   slots 0..28 whose player index (+0x0F) is not 0.
// Monthly expenses (monthly_expenses):
//   130000 - game level * 10000 - 20 * league_bonus[league] (2400 / 1800 / 990)
//   full:          + training recovery costs (+0x193) * 8.5
//   + salary (+0x28) and monthly injury insurance (+0x30) of the first
//     "players" squad slots
//   + the same two longs of the pool players (manager block 4, 12 slots) whose player record
//     byte +0x1A names this manager
//   + (cost_factor (default 100) - 25) * 175000 / 100 * (2 - league)  (none in the Oberliga)
//   - 60000 in the second division
//   + sum of stadium level[i] * stadium_cost[i] (24000 7500 33000 3000 4800 9000 15000
//     15000 7500)
//   + sum of places[i] * place_cost[i] (15, 12)
//   + three further i32 amounts per manager (purpose unknown, 0 in the reference saves)
//   with_interest: + loan interest - balance / 10 if the balance is negative
//   clamped at >= 0.
// Example: in a reference save with 134000 DM savings the "Totale" screen shows an income of
// 14715 DM (40125 + 4020 savings interest, raw) and expenses of 482566 DM (1447700 raw).
//
// Daily booking (finance_day, every day for each human manager):
//   interest = savings interest (bank 1 %, other managers 2 %)
//            - loan interest (bank 4 %, other managers 2.5 %)
//            + balance / 10 if the balance is negative (10 % overdraft interest)
//   accrued += interest / (days in month + 1)
//   on the 1st: every running advertising contract loses a month; at 0 it ends unless its flag
//     byte (+6 & 0x7F) is set (the original then shows a renewal message; not reproduced yet)
//   on month_length / 2 and the last day: sponsor offers (handled by the caller)
//   on the last day: balance += income(no interest) - expenses(no interest) + accrued, clamped to
//     +-500,000,000 raw, accrued = 0; the monthly insurance lock (+0x2F bit 7) clears.
//
// Attendance of a home game (home_attendance, for each human manager whose team plays at home;
// the money factor is 3 / 1):
//   strengths += 17; a guest from a lower class counts as rank 11, from a higher class as rank 0;
//   ranks above 11 count as 5
//   A = ((5 * hs / 4 + 5 * as / 4) / 2 + 200 * (22 - hr - ar / 2) / 22) / 2 * 70
//       + class_fans[home class] + 3500 per class the guest is higher - 800 per class it is lower
//   + level[i] * rand(Y * low[i] / 60, Y * high[i] / 60) per stadium item, Y = 110 - 2 * hr
//   + game level * 500; + rand(1400, 1800) if places[1] < places[0] + 2000
//   A = A * (100 + 20 * kind) / 100
//   mood S = min((hs + as + 30) / 2, 90); price effect Q = 100 - 5 * (price * 3 / 2), below -30
//   increased by half, else below 80 reduced by rand(Q / 5, Q / 2)
//   A = A * (100 + Q * (100 - 9 * S / 10) / 50) / 100 - 9000
//   if rand(150, 200) > A and price * 3 / 2 > (2 - home class) * 3 + 16: A /= rand(2, 5)
//   else while rand(200, 300) > A: A += 50
//   first division only: A * (item0 * 25 + 50) / 100 while item 0 < 2, A * 2 / 3 without item 2
//   A = A * (item4 * 3 + 93) / 100 * ... (item3 * 5 + 95) / 100; below 50: rand(50, 100)
//   at most the capacity places[0] + places[1]
//   balance += price * A * 3 (raw); league totals/extrema/count are updated here,
//   while Game writes the league attendance graph to the manager block.
// Example: a second-division club with 7500 places and the default prices sells out its stadium.
#include <functional>
#include <string>
#include <array>
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr int kStadiumItems = 9;   // stadium extensions (levels at manager block +0x116, stride 2)
constexpr int kPlaceKinds = 2;     // kinds of spectator places (counts at +0x133, stride 4)
constexpr int kAdvertSlots = 11;   // advertising contracts per manager
constexpr long kBalanceLimit = 500000000;  // raw, 0x1DCD6500; the balance is clamped to +-this

// Constants of the finance rules, read from the user's EM.EXE (some overridden by the save).
struct FinanceTables {
    std::array<long, 3> league_bonus{};             // per league: 2400 / 1800 / 990
    std::array<long, kStadiumItems> stadium_cost{};  // monthly cost per level of each stadium item
    std::array<long, kPlaceKinds> place_cost{};  // monthly maintenance per place: 15, 12
    long cost_factor = 100;                         // cost/difficulty factor in percent
    int level = 2;                                  // game (difficulty) level
    // attendance (home_attendance)
    std::array<long, 4> class_fans{};               // base fans per class: 4800 3250 1200 (400)
    std::array<int, kStadiumItems> extension_fans_low{}, extension_fans_high{};  // fan range per stadium level
    long money_num = 3, money_den = 1;              // the x3 money factor as a fraction
    std::array<int, 3> class_limit{};               // team index limits of the classes: 12, 24, 52
};

// Loads the default tables from EM.EXE.
FinanceTables load_finance_tables(const Bytes& exe);
// As above, but takes the runtime game level and cost factor from the save (the EXE holds only
// their defaults).
FinanceTables load_finance_tables(const Bytes& exe, const HokFile& save);

// One advertising contract (10-byte record in the HOK state at 0xACFC + manager * 0x6E).
struct AdvertContract {
    long amount = 0;      // +0 i32: raw per month, 0 = slot free
    int months_left = 0;  // +5: months the contract still runs
    int flags = 0;        // +6: if any of the low 7 bits is set, the contract does not end at 0 months
    int board_owner = 255;  // +4: shared id for all boards of one contract; 255 = free
    int sponsor = 0;       // +7: sponsor index
    int min_image = 0;     // +8 u16: image the club needed for the offer
};

// Everything the finance rules read and write for one manager. Amounts are raw (x3).
struct ManagerBudget {
    int team = -1;          // team index of the manager
    int league = 0;         // 0 first division, 1 second division, 2 Oberliga
    int team_strength = 0;  // skill1[0] + skill1[1] + skill2[0] + skill2[1]
    int players = 0;        // occupied squad slots (player index != 0)
    long squad_costs = 0;   // salaries (+0x28) and insurance (+0x30) of the first `players` slots
    long pool_costs = 0;    // pool players tied to this manager
    std::array<int, kStadiumItems> stadium{};  // stadium extension levels
    std::array<int, kPlaceKinds> places{};  // the two kinds of spectator places (sum = capacity)
    std::array<AdvertContract, kAdvertSlots> adverts{};  // HOK state 0xACFC + manager * 0x6E
    int ticket_price = 0;   // DM
    int home_games = 0;     // home games played this season
    // League attendance statistics (+0x183 total, +0x187 record, +0x18B lowest, 99999 before the first
    // league home; +0x114 / +0x115 the guests of record and lowest game)
    long attendance_total = 0, attendance_record = 0, attendance_min = 99999;
    int record_guest = -1, min_guest = -1;
    long open_97ee = 0;   // manager block +0x193: training recovery costs
    long open_a07e = 0;   // sum of three i32 amounts at HOK state 0x06C1 + manager * 0x15 (meaning unknown)
    long balance = 0;       // manager block +0x18F
    long bank = 0;          // > 0 savings at the bank, < 0 bank loan
    long accrued = 0;       // interest collected for the month end
    long stored_income = 0; // monthly income (+0x197) as found in the save
};

// Reads the manager block, the team and the squad of manager `m` (0-based) from a save.
ManagerBudget budget_from_save(const HokFile& h, int m);
// Writes mutable finances, the advertising contracts and accrued interest back to the HOK.
// Includes the board group, sponsor and required image of every contract.
void store_budget(HokFile& h, const ManagerBudget& b, int m);

long savings_interest(const ManagerBudget& b);  // per month, >= 0
long loan_interest(const ManagerBudget& b);     // per month, >= 0
// Monthly income (see the file comment); `full` adds the savings/lending interest.
long monthly_income(const ManagerBudget& b, bool full);
// Monthly expenses (see the file comment); `full` adds the training recovery costs,
// `with_interest` the loan and overdraft interest.
long monthly_expenses(const FinanceTables& t, const ManagerBudget& b, bool full, bool with_interest);

// One home game of a human manager, as passed to home_attendance().
struct AttendanceMatch {
    int home_class = 0, away_class = 0;        // team_class()
    int home_strength = 0, away_strength = 0;  // attendance_strength()
    int home_rank = 5, away_rank = 5;          // 0-based table position (> 11 counts as 5)
    int guest = -1;                            // team index of the guest
    int kind = 0;  // 0 league; 1 / 2 other competitions (depending on the competition mode), capped at 3
    bool league_statistics = true;  // the original books attendance records only for league games
};

// Class of a team: 0 first division (and European clubs), 1 second division, 2 Oberliga and
// national teams.
int team_class(const FinanceTables& t, int team);
// Strength used by the attendance formula: (skill 1 + skill 2 + form of both kinds) / 6.
int attendance_strength(const HokTeam& team);
// Spectators of a home game; the ticket money (price * spectators, raw) is booked to the
// balance. It also counts the home game and updates the attendance statistics.
long home_attendance(const FinanceTables& t, ManagerBudget& b, const AttendanceMatch& m, BorlandRandom& rng);

// The daily booking for one day. `day` is the day of the month (1-based), `month_days` its length.
void finance_day(const FinanceTables& t, ManagerBudget& b, int day, int month_days);

// Weekly stadium step: wear of the extensions followed by the eleven construction countdowns.
// Planned levels/place counts and remaining weeks live in the manager block of `state`.
// Returns the number of constructions completed this week.
// `message` (optional) receives the two text lines of a wear-loss message; it must draw the
// message date rand(0, 3) itself (as the message queue does). Without it only that draw is made.
int stadium_week(const FinanceTables& t, ManagerBudget& b, Bytes& state, int manager, BorlandRandom& rng,
                 const std::function<void(const std::string&, const std::string&)>& message = nullptr);

}  // namespace em
