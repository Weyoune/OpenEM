// SPDX-License-Identifier: GPL-3.0-only
#include "game/finances.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;  // see schedule.cpp
constexpr size_t kLeagueBonus = 0x52C2, kStadiumCost = 0x0094, kPlaceCost = 0x00B8;
constexpr size_t kCostFactor = 0x02BC, kLevel = 0x0ED8;
constexpr size_t kClassFans = 0x4FD0, kFansLow = 0x4FE0, kFansHigh = 0x4FF2, kMoneyNum = 0x079D, kMoneyDen = 0x07A1;
constexpr size_t kClassLimit1 = 0x07C1, kClassLimit2 = 0x07C2, kClassLimit3 = 0x06AD;

// HOK state layout (see hok.hpp and finances.hpp)
constexpr size_t kManagerBlock = 0x0DCD, kManagerSize = 0x1AF;
constexpr size_t kTicketPrice = 0x0E4, kHomeGames = 0x0ED, kRecordGuest = 0x114, kMinGuest = 0x115;
constexpr size_t kAttendanceTotal = 0x183, kAttendanceRecord = 0x187, kAttendanceMin = 0x18B;
constexpr size_t kTeamIndex = 0x014, kLeague = 0x0EB, kStadium = 0x116, kPlaces = 0x133;
constexpr size_t kBalance = 0x18F, kOpen97ee = 0x193, kIncome = 0x197, kDebts = 0x19B;
constexpr int kBankParty = 4;
constexpr size_t kSquad = 0x696D, kSquadRecord = 0x34, kSquadBlock = 0x618;
constexpr size_t kPlayers = 0x4FC6, kPlayerRecord = 0x21;
constexpr int kPoolManager = 4;

long sle32(const Bytes& b, size_t o) { return long(int32_t(le32(b, o))); }

// ovl12_0d27(manager, 0): 29 slots (12 for the pool block 4), counting those with a player.
int count_players(const Bytes& s, int m) {
    const int slots = m == kPoolManager ? 0x1D - 0x11 : 0x1D;
    int n = 0;
    for (int i = 0; i < slots; i++)
        if (s[kSquad + size_t(m) * kSquadBlock + size_t(i) * kSquadRecord + 0x0F] != 0) n++;
    return n;
}

// Salary (+0x28) plus the signing-on long at +0x30 of squad slot i of block m.
long slot_costs(const Bytes& s, int m, int i) {
    const size_t r = kSquad + size_t(m) * kSquadBlock + size_t(i) * kSquadRecord;
    return sle32(s, r + 0x28) + sle32(s, r + 0x30);
}
}  // namespace

FinanceTables load_finance_tables(const Bytes& exe) {
    FinanceTables t;
    auto at = [&](size_t ds) { return long(int32_t(le32(exe, kDgroupFile + ds))); };
    for (size_t i = 0; i < t.league_bonus.size(); i++) t.league_bonus[i] = at(kLeagueBonus + 4 * i);
    for (size_t i = 0; i < t.stadium_cost.size(); i++) t.stadium_cost[i] = at(kStadiumCost + 4 * i);
    for (size_t i = 0; i < t.place_cost.size(); i++) t.place_cost[i] = at(kPlaceCost + 4 * i);
    t.cost_factor = at(kCostFactor);
    t.level = exe.at(kDgroupFile + kLevel);
    for (size_t i = 0; i < t.class_fans.size(); i++) t.class_fans[i] = at(kClassFans + 4 * i);
    for (size_t i = 0; i < size_t(kStadiumItems); i++) {
        t.extension_fans_low[i] = le16(exe, kDgroupFile + kFansLow + 2 * i);
        t.extension_fans_high[i] = le16(exe, kDgroupFile + kFansHigh + 2 * i);
    }
    t.money_num = at(kMoneyNum);
    t.money_den = at(kMoneyDen);
    t.class_limit = {exe.at(kDgroupFile + kClassLimit1), exe.at(kDgroupFile + kClassLimit2),
                     exe.at(kDgroupFile + kClassLimit3)};
    if (t.stadium_cost[0] != 24000 || t.league_bonus[0] != 2400)
        throw FormatError("EM.EXE: finance tables not found (not V1.00?)");
    return t;
}

ManagerBudget budget_from_save(const HokFile& h, int m) {
    const Bytes& s = h.state;
    const size_t blk = kManagerBlock + size_t(m) * kManagerSize;
    ManagerBudget b;
    b.team = s[blk + kTeamIndex];
    b.league = s[blk + kLeague];
    if (b.team >= 0 && size_t(b.team) < h.teams.size()) {
        const HokTeam& t = h.teams[size_t(b.team)];
        b.team_strength = t.skill1[0] + t.skill1[1] + t.skill2[0] + t.skill2[1];
    }
    b.players = count_players(s, m);
    // ovl08_1871 walks the first `players` slots, assuming the squad is packed
    for (int i = 0; i < b.players; i++) b.squad_costs += slot_costs(s, m, i);
    const int pool = count_players(s, kPoolManager);
    for (int i = 0; i < pool; i++) {
        // squad +0x0F holds the player index plus 1 (0 = free slot)
        const int p = s[kSquad + kPoolManager * kSquadBlock + size_t(i) * kSquadRecord + 0x0F] - 1;
        if (s[kPlayers + size_t(p) * kPlayerRecord + 0x1A] == m) b.pool_costs += slot_costs(s, kPoolManager, i);
    }
    for (int i = 0; i < kStadiumItems; i++) b.stadium[size_t(i)] = s[blk + kStadium + 2 * size_t(i)];
    for (int i = 0; i < kPlaceKinds; i++) b.places[size_t(i)] = le16(s, blk + kPlaces + 4 * size_t(i));
    b.ticket_price = s[blk + kTicketPrice];
    b.home_games = s[blk + kHomeGames];
    b.attendance_total = sle32(s, blk + kAttendanceTotal);
    b.attendance_record = sle32(s, blk + kAttendanceRecord);
    b.attendance_min = sle32(s, blk + kAttendanceMin);
    b.record_guest = s[blk + kRecordGuest];
    b.min_guest = s[blk + kMinGuest];
    b.balance = sle32(s, blk + kBalance);
    b.open_97ee = sle32(s, blk + kOpen97ee);
    b.stored_income = sle32(s, blk + kIncome);
    b.bank = sle32(s, blk + kDebts + 4 * kBankParty);
    return b;
}

// Single-player view: only the bank is a party. The bank's own entry about the manager is
// -bank; ovl08 reads that one, the result is the same.
long savings_interest(const ManagerBudget& b) { return b.bank > 0 ? b.bank * 10 / 1000 : 0; }

long loan_interest(const ManagerBudget& b) { return b.bank < 0 ? -(b.bank * 4 / 100) : 0; }

long monthly_income(const ManagerBudget& b, bool full) {
    long income = long(b.team_strength) * 15 * b.players / 3;
    for (const AdvertContract& a : b.adverts) income += a.amount;
    if (full) income += savings_interest(b);
    return std::max(income, 0L);
}

long monthly_expenses(const FinanceTables& t, const ManagerBudget& b, bool full, bool with_interest) {
    const int league = std::clamp(b.league, 0, 2);
    long cost = 130000 - long(t.level) * 10000 - t.league_bonus[size_t(league)] * 20;
    if (full) cost += (b.open_97ee << 3) + b.open_97ee / 2;
    cost += b.squad_costs + b.pool_costs;
    cost += (t.cost_factor - 25) * 175000 / 100 * (2 - league);
    if (league == 1) cost -= 60000;
    for (int i = 0; i < kStadiumItems; i++) cost += b.stadium[size_t(i)] * t.stadium_cost[size_t(i)];
    for (int i = 0; i < kPlaceKinds; i++) cost += b.places[size_t(i)] * t.place_cost[size_t(i)];
    cost += b.open_a07e;
    if (with_interest) {
        cost += loan_interest(b);
        if (b.balance < 0) cost -= b.balance / 10;
    }
    return std::max(cost, 0L);
}

int team_class(const FinanceTables& t, int team) {
    if (team < t.class_limit[0]) return 0;
    if (team < t.class_limit[1]) return 1;
    if (team <= t.class_limit[2]) return 2;
    return 0;  // European clubs count like the first division
}

int attendance_strength(const HokTeam& team) {
    int sum = 0;
    for (int k = 0; k < 2; k++) sum += team.skill1[k] + team.skill2[k] + team.form[k];
    return sum / 6;
}

long home_attendance(const FinanceTables& t, ManagerBudget& b, const AttendanceMatch& m, BorlandRandom& rng) {
    const long hs = m.home_strength + 17, as = m.away_strength + 17;
    const int hc = std::clamp(m.home_class, 0, 3), ac = std::clamp(m.away_class, 0, 3);
    long hr = m.home_rank, ar = m.away_rank;
    if (hc < ac) ar = 11;  // guest from a lower class
    else if (hc > ac) ar = 0;
    if (hr > 11) hr = 5;
    if (ar > 11) ar = 5;
    const long price = long(b.ticket_price) * t.money_num / t.money_den / 2;  // DM * 3 / 2

    long a = ((5 * hs / 4 + 5 * as / 4) / 2 + 200 * (22 - hr - ar / 2) / 22) / 2 * 70;
    a += t.class_fans[size_t(hc)];
    if (hc > ac) a += (hc - ac) * 3500L;
    if (hc < ac) a -= (ac - hc) * 800L;
    const long y = 110 - 2 * hr;
    for (size_t i = 0; i < size_t(kStadiumItems); i++)
        a += long(b.stadium[i]) * rng.range(int(y * t.extension_fans_low[i] / 60), int(y * t.extension_fans_high[i] / 60));
    a += t.level * 500L;
    if (b.places[1] < b.places[0] + 2000) a += rng.range(1400, 1800);
    const long capacity = long(b.places[0]) + b.places[1];

    a = a * (100 + 20 * std::min(m.kind, 3)) / 100;
    const long mood = std::min((hs + as + 30) / 2, 90L);
    long q = 100 - 5 * price;
    if (q < -30) q += q / 2;
    else if (q < 80) q -= rng.range(int(q / 5), int(q / 2));
    a = a * (100 + q * (100 - 9 * mood / 10) / 50) / 100 - 9000;

    bool collapsed = false;  // a far too expensive ticket empties the hall
    if (rng.range(150, 200) > a && price > (2 - hc) * 3 + 16) {
        a /= rng.range(2, 5);
        collapsed = true;
    }
    while (!collapsed && rng.range(200, 300) > a) a += 50;

    if (hc == 0 && b.stadium[0] < 2) a = a * (b.stadium[0] * 25 + 50) / 100;
    if (hc == 0 && b.stadium[2] == 0) a = a * 2 / 3;
    a = a * (b.stadium[4] * 3 + 93) / 100;
    a = a * (b.stadium[3] * 5 + 95) / 100;
    if (a < 50) a = rng.range(50, 100);
    a = std::min(a, capacity);

    b.balance += long(b.ticket_price) * a * t.money_num / t.money_den;
    // ovl14_2630: statistics (the per-game list at +0x0FE holds a / 1000; not kept here)
    b.attendance_total += a;
    if (a >= b.attendance_record) {
        b.attendance_record = a;
        b.record_guest = m.guest;
    }
    if (a <= b.attendance_min) {
        b.attendance_min = a;
        b.min_guest = m.guest;
    }
    b.home_games++;
    return a;
}

void finance_day(const FinanceTables& t, ManagerBudget& b, int day, int month_days) {
    long interest = savings_interest(b) - loan_interest(b);
    if (b.balance < 0) interest += b.balance / 10;
    b.accrued += interest / (month_days + 1);

    if (day == 1)
        for (AdvertContract& a : b.adverts) {
            if (a.amount == 0 || a.months_left == 0) continue;
            if (--a.months_left == 0 && (a.flags & 0x7F) == 0) a = AdvertContract{};
        }

    if (day == month_days) {
        b.balance += monthly_income(b, false) - monthly_expenses(t, b, false, false) + b.accrued;
        b.balance = std::clamp(b.balance, -kBalanceLimit, kBalanceLimit);
        b.accrued = 0;
    }
}

}  // namespace em
