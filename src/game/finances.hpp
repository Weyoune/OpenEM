// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Monthly finances of a manager (overlay 8 of EM.EXE). All amounts here are RAW values, i.e. the
// shown Deutschmark amount times 3 (see hok.hpp); divide by 3 (truncating) for the screen.
//
// Manager block: DS:965B + manager * 0x1AF in RAM, saved unchanged at HOK state 0x0DCD +
// manager * 0x1AF (checked with START/TAG1/MONAT1/AKTIE against RAM dumps). Offsets in the block:
//   +0x000 manager name, +0x014 u8 team index (DS:966F), +0x0EB u8 league 0..2 (DS:9746),
//   +0x0E4 u8 ticket price in DM (10..30, DS:973F), +0x0ED u8 home games played (DS:9748),
//   +0x0FE u8[] attendance / 1000 of each home game, +0x114 / +0x115 opponent of the best /
//   worst attendance, +0x116 u8[9] stadium extension levels, stride 2 (DS:9771), +0x133 u16[2]
//   spectator places of two kinds, stride 4 (DS:978E; sum = capacity; maintenance 15 / 12 each),
//   +0x18F balance (DS:97EA), +0x193 weekly recovery costs of the training (DS:97EE, paid weekly by 146e:0c89),
//   +0x197 monthly income as last computed by ovl08_16b7 (DS:97F2), +0x19B long[5] debt matrix
//   (DS:97F6): entry [p] of manager m is what party p owes m (4 = the bank; > 0 = m has lent
//   money to p / has savings at the bank, < 0 = m owes p). Party p keeps the negated value.
//
// Monthly income, ovl08_16b7(full):
//   strength = skill1[0] + skill1[1] + skill2[0] + skill2[1] of the manager's team
//   income   = strength * 15 * players / 3 + sum of the 11 advertising contracts (DS:C15B +
//              manager * 0x6E, 10 bytes each: long amount, +5 months left, +6 flags)
//   full:    + 1 % of the savings at the bank, + 2.5 % of money lent to other managers
//   clamped at >= 0 and stored at DS:97F2. "players" = ovl12_0d27(manager, 0): squad slots
//   0..28 whose player index (+0x0F) is not 0.
// Monthly expenses, ovl08_1871(full, with_interest):
//   130000 - level (DS:0ED8) * 10000 - 20 * league_bonus[league] (DS:52C2: 2400/1800/990)
//   full:          + open * 8.5 (DS:97EE)
//   + salary (+0x28) and monthly injury insurance (+0x30) of the first
//     "players" squad slots
//   + the same two longs of the pool players (manager block 4, 12 slots) whose player record
//     byte +0x1A names this manager
//   + (cost_factor (DS:02BC, 100) - 25) * 175000 / 100 * (2 - league)  (none in the Oberliga)
//   - 60000 in the second division
//   + sum of stadium level[i] * stadium_cost[i] (DS:0094: 24000 7500 33000 3000 4800 9000 15000
//     15000 7500)
//   + sum of places[i] * place_cost[i] (DS:00B8: 15, 12)
//   + three longs at DS:A07E + manager * 0x15 (open, 0 in the reference save)
//   with_interest: + loan interest (ovl15_02e8(1, all)) - balance / 10 if the balance is negative
//   clamped at >= 0.
// Checked against the "Totale" screen of the reference save AKTIE (MONAT1 plus 134000 DM savings):
// income 14715 DM (40125 + 4020 savings interest, raw), expenses 482566 DM (1447700 raw).
//
// Daily booking, ovl08_1d61 (called every day for each human manager):
//   interest = savings interest (bank 1 %, other managers 2 %)
//            - loan interest (ovl15_07e4: bank 4 %, other managers 2.5 %)
//            + balance / 10 if the balance is negative (10 % overdraft interest)
//   accrued (DS:02A0 + manager * 4) += interest / (days in month + 1)
//   on the 1st: every running advertising contract loses a month; at 0 it ends unless its flag
//     byte (+6 & 0x7F) is set (then a renewal message appears - open)
//   on month_length / 2 and the last day: sponsor offers (ovl08_2ba7)
//   on the last day: balance += income(no interest) - expenses(no interest) + accrued, clamped to
//     +-500,000,000 raw, accrued = 0; the monthly insurance lock (+0x2F bit 7) clears.
//
// Attendance of a home game, ovl08_21e5 (called by ovl14_2630 for each human manager whose team
// plays at home; DS:079D / 07A1 = 3 / 1 is the money factor):
//   strengths += 17; a guest from a lower class counts as rank 11, from a higher class as rank 0;
//   ranks above 11 count as 5
//   A = ((5 * hs / 4 + 5 * as / 4) / 2 + 200 * (22 - hr - ar / 2) / 22) / 2 * 70
//       + class_fans[home class] + 3500 per class the guest is higher - 800 per class it is lower
//   + level[i] * rand(Y * low[i] / 60, Y * high[i] / 60) per stadium item, Y = 110 - 2 * hr
//   + game level (DS:0ED8) * 500; + rand(1400, 1800) if places[1] < places[0] + 2000
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
// Checked: Köln (MONAT1) sells out its 7500 places, as in all four home games of the original.
#include <array>
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr int kStadiumItems = 9, kPlaceKinds = 2, kAdvertSlots = 11;
constexpr long kBalanceLimit = 500000000;  // raw, 0x1DCD6500

// Constants of ovl08 read from EM.EXE (DGROUP).
struct FinanceTables {
    std::array<long, 3> league_bonus{};             // DS:52C2, per league
    std::array<long, kStadiumItems> stadium_cost{};  // DS:0094
    std::array<long, kPlaceKinds> place_cost{};  // DS:00B8
    long cost_factor = 100;                         // DS:02BC
    int level = 2;                                  // DS:0ED8
    // attendance (ovl08_21e5)
    std::array<long, 4> class_fans{};               // DS:4FD0: 4800 3250 1200 (400)
    std::array<int, kStadiumItems> extension_fans_low{}, extension_fans_high{};  // DS:4FE0 / 4FF2
    long money_num = 3, money_den = 1;              // DS:079D / 07A1: the x3 money factor
    std::array<int, 3> class_limit{};               // DS:07C1, 07C2, 06AD: 12, 24, 52
};

FinanceTables load_finance_tables(const Bytes& exe);
// Runtime difficulty/cost factor are serialized in HOK; EXE holds only defaults.
FinanceTables load_finance_tables(const Bytes& exe, const HokFile& save);

struct AdvertContract {
    long amount = 0;      // raw per month, 0 = slot free
    int months_left = 0;  // +5
    int flags = 0;        // +6
    int board_owner = 255;  // +4: shared id for all boards of one contract; 255 = free
    int sponsor = 0;       // +7
    int min_image = 0;     // +8 u16
};

// Everything ovl08 reads for one manager. Amounts are raw (x3).
struct ManagerBudget {
    int team = -1;
    int league = 0;
    int team_strength = 0;  // skill1[0] + skill1[1] + skill2[0] + skill2[1]
    int players = 0;        // ovl12_0d27(manager, 0)
    long squad_costs = 0;   // salaries (+0x28) and insurance (+0x30) of the first `players` slots
    long pool_costs = 0;    // pool players tied to this manager
    std::array<int, kStadiumItems> stadium{};
    std::array<int, kPlaceKinds> places{};  // the two kinds of spectator places (sum = capacity)
    std::array<AdvertContract, kAdvertSlots> adverts{};  // HOK state 0xACFC + manager * 0x6E
    int ticket_price = 0;   // DM
    int home_games = 0;
    // League attendance statistics (+0x183 total, +0x187 record, +0x18B lowest, 99999 before the first
    // league home; +0x114 / +0x115 the guests of record and lowest game)
    long attendance_total = 0, attendance_record = 0, attendance_min = 99999;
    int record_guest = -1, min_guest = -1;
    long open_97ee = 0;   // DS:97EE
    long open_a07e = 0;   // three i32 amounts at DS:A07E, HOK state 0x06C1 + manager * 0x15
    long balance = 0;
    long bank = 0;          // > 0 savings at the bank, < 0 bank loan
    long accrued = 0;       // DS:02A0, interest collected for the month end
    long stored_income = 0; // DS:97F2 as found in the save
};

// Reads the manager block, the team and the squad of manager `m` (0-based) from a save.
ManagerBudget budget_from_save(const HokFile& h, int m);
// Writes mutable finances, native advertising contracts and accrued interest back to the HOK.
// Includes the board group, sponsor and required image of every contract.
void store_budget(HokFile& h, const ManagerBudget& b, int m);

long savings_interest(const ManagerBudget& b);  // per month, >= 0
long loan_interest(const ManagerBudget& b);     // per month, >= 0
long monthly_income(const ManagerBudget& b, bool full);
long monthly_expenses(const FinanceTables& t, const ManagerBudget& b, bool full, bool with_interest);

// One home game of a human manager (ovl14_2630 -> ovl08_21e5).
struct AttendanceMatch {
    int home_class = 0, away_class = 0;        // team_class()
    int home_strength = 0, away_strength = 0;  // attendance_strength()
    int home_rank = 5, away_rank = 5;          // 0-based table position (> 11 counts as 5)
    int guest = -1;
    int kind = 0;  // 0 league; 1 / 2 other competitions (DS:D0C2 = 6 or 1 / 3), capped at 3
    bool league_statistics = true;  // ovl14_2630 books attendance records only for native mode 0
};

// ovl05_5aa5 / 2: 0 first division (and European clubs), 1 second division, 2 Oberliga and
// national teams.
int team_class(const FinanceTables& t, int team);
// ovl13_0026: (skill 1 + skill 2 + form of both kinds) / 6.
int attendance_strength(const HokTeam& team);
// ovl08_21e5: spectators of a home game; the ticket money (price * spectators, raw) is booked
// to the balance. Like ovl14_2630 it also counts the home game and updates the statistics.
long home_attendance(const FinanceTables& t, ManagerBudget& b, const AttendanceMatch& m, BorlandRandom& rng);

// ovl08_1d61 for one day. `day` is the day of the month (1-based), `month_days` its length.
void finance_day(const FinanceTables& t, ManagerBudget& b, int day, int month_days);

// ovl08_0376: weekly wear followed by the eleven construction countdowns.
// Planned levels/place counts and remaining weeks live in the native manager block.
int stadium_week(const FinanceTables& t, ManagerBudget& b, Bytes& state, int manager, BorlandRandom& rng);

}  // namespace em
