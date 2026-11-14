// SPDX-License-Identifier: GPL-3.0-only
// Club finances (see finances.hpp): the constant tables from EM.EXE, the manager budget in the savegame,
// monthly income and expenses, interest, home attendance and ticket income, the daily/monthly booking and the
// weekly stadium wear and construction progress.
#include "game/finances.hpp"

#include <algorithm>

namespace em {

namespace {
// File offset of the initialised data of EM.EXE (header size + start of the data segment), see schedule.cpp.
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;
// EM.EXE table offsets relative to kDgroupFile (see FinanceTables)
constexpr size_t kLeagueBonus = 0x52C2, kStadiumCost = 0x0094, kPlaceCost = 0x00B8;
constexpr size_t kCostFactor = 0x02BC, kLevel = 0x0ED8;
constexpr size_t kClassFans = 0x4FD0, kFansLow = 0x4FE0, kFansHigh = 0x4FF2, kMoneyNum = 0x079D, kMoneyDen = 0x07A1;
constexpr size_t kClassLimit1 = 0x07C1, kClassLimit2 = 0x07C2, kClassLimit3 = 0x06AD;

// HOK state layout (see hok.hpp and finances.hpp)
constexpr size_t kManagerBlock = 0x0DCD, kManagerSize = 0x1AF;  // manager blocks; fields below are relative to a block
constexpr size_t kTicketPrice = 0x0E4, kHomeGames = 0x0ED, kRecordGuest = 0x114, kMinGuest = 0x115;
constexpr size_t kAttendanceTotal = 0x183, kAttendanceRecord = 0x187, kAttendanceMin = 0x18B;
constexpr size_t kTeamIndex = 0x014, kLeague = 0x0EB, kStadium = 0x116, kPlaces = 0x133;
// balance, weekly training recovery costs, last monthly income, debts table (i32 per party)
constexpr size_t kBalance = 0x18F, kRecoveryCost = 0x193, kIncome = 0x197, kDebts = 0x19B;
constexpr int kBankParty = 4;  // block 4 is the bank: debts entry 4 of a manager = his bank account
constexpr size_t kSquad = 0x696D, kSquadRecord = 0x34, kSquadBlock = 0x618;
constexpr size_t kPlayers = 0x4FC6, kPlayerRecord = 0x21;
constexpr int kPoolManager = 4;  // squad block 4: players without a human club
constexpr size_t kAdverts = 0xACFC, kAdvertRecord = 10, kAdvertBlock = 0x6E;  // advertising contracts per manager
constexpr size_t kAccrued = 0;  // i32 per manager: interest accrued this month; the first block of the state
constexpr size_t kOtherCosts = 0x06C1, kOtherCostsStride = 0x15;  // three i32 amounts per manager

long sle32(const Bytes& b, size_t o) { return long(int32_t(le32(b, o))); }  // signed little-endian i32

void put32(Bytes& b, size_t o, long v) {  // stores the low 32 bits of v little endian (bounds-checked)
    for (int i = 0; i < 4; i++) b.at(o + size_t(i)) = uint8_t(uint32_t(v) >> (8 * i));
}

// Number of occupied squad slots of manager block m, counted as the game counts them: 29 slots (12 for the pool
// block 4), counting those with a player.
int count_players(const Bytes& s, int m) {
    const int slots = m == kPoolManager ? 0x1D - 0x11 : 0x1D;
    int n = 0;
    for (int i = 0; i < slots; i++)
        if (s[kSquad + size_t(m) * kSquadBlock + size_t(i) * kSquadRecord + 0x0F] != 0) n++;
    return n;
}

// Salary (+0x28) plus the monthly injury-insurance premium at +0x30.
long slot_costs(const Bytes& s, int m, int i) {
    const size_t r = kSquad + size_t(m) * kSquadBlock + size_t(i) * kSquadRecord;
    return sle32(s, r + 0x28) + sle32(s, r + 0x30);
}
}  // namespace

// Throws FormatError when the first stadium cost or league bonus does not match V1.00.
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

// The game level and the cost factor are saved with the game and override the EM.EXE defaults.
FinanceTables load_finance_tables(const Bytes& exe, const HokFile& save) {
    FinanceTables tables = load_finance_tables(exe);
    if (save.state.size() <= 0xAEB4 || save.state[0xAEB4] > 5)
        throw FormatError("HOK: invalid saved game level");
    tables.level = save.state[0xAEB4];  // state 0xAEB4: game level 0..5
    tables.cost_factor = sle32(save.state, 0x06B0);  // state 0x06B0: i32 cost factor
    return tables;
}

// Reads the budget of manager m (0..3, or 5 = the national coach) from the save state.
ManagerBudget budget_from_save(const HokFile& h, int m) {
    if (m < 0 || m > 5 || m == 4) throw FormatError("HOK: invalid human manager index");  // 5: the national coach (WM gate)
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
    // the game's cost sum walks the first `players` slots, assuming the squad is packed
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
    b.open_97ee = sle32(s, blk + kRecoveryCost);
    b.stored_income = sle32(s, blk + kIncome);
    b.bank = sle32(s, blk + kDebts + 4 * kBankParty);
    b.accrued = sle32(s, kAccrued + size_t(m) * 4);
    for (size_t i = 0; i < 3; i++) b.open_a07e += sle32(s, kOtherCosts + size_t(m) * kOtherCostsStride + i * 4);
    for (size_t i = 0; i < b.adverts.size(); i++) {
        const size_t o = kAdverts + size_t(m) * kAdvertBlock + i * kAdvertRecord;
        b.adverts[i] = {sle32(s, o), s.at(o + 5), s.at(o + 6), s.at(o + 4), s.at(o + 7), le16(s, o + 8)};
    }
    return b;
}

// Writes the budget fields back; the bank account is stored twice, as the manager's entry about the bank and,
// negated, as the bank's entry about the manager.
void store_budget(HokFile& h, const ManagerBudget& b, int m) {
    if (m < 0 || m >= 4) throw FormatError("HOK: invalid human manager index");
    Bytes& s = h.state;
    const size_t blk = kManagerBlock + size_t(m) * kManagerSize;
    put32(s, blk + kBalance, b.balance);
    put32(s, blk + kRecoveryCost, b.open_97ee);
    put32(s, blk + kIncome, b.stored_income);
    put32(s, blk + kDebts + 4 * kBankParty, b.bank);
    put32(s, kManagerBlock + kBankParty * kManagerSize + kDebts + 4 * size_t(m), -b.bank);
    put32(s, kAccrued + size_t(m) * 4, b.accrued);
    put32(s, blk + kAttendanceTotal, b.attendance_total);
    put32(s, blk + kAttendanceRecord, b.attendance_record);
    put32(s, blk + kAttendanceMin, b.attendance_min);
    s.at(blk + kHomeGames) = uint8_t(b.home_games);
    if (b.record_guest >= 0) s.at(blk + kRecordGuest) = uint8_t(b.record_guest);
    if (b.min_guest >= 0) s.at(blk + kMinGuest) = uint8_t(b.min_guest);
    s.at(blk + kTeamIndex) = uint8_t(b.team);
    s.at(blk + kLeague) = uint8_t(b.league);
    for (size_t i = 0; i < b.stadium.size(); i++) s.at(blk + kStadium + i * 2) = uint8_t(b.stadium[i]);
    for (size_t i = 0; i < b.places.size(); i++) {
        s.at(blk + kPlaces + i * 4) = uint8_t(b.places[i]);
        s.at(blk + kPlaces + i * 4 + 1) = uint8_t(b.places[i] >> 8);
    }
    for (size_t i = 0; i < b.adverts.size(); i++) {
        const size_t o = kAdverts + size_t(m) * kAdvertBlock + i * kAdvertRecord;
        put32(s, o, b.adverts[i].amount);
        s.at(o + 4) = uint8_t(b.adverts[i].board_owner);
        s.at(o + 5) = uint8_t(b.adverts[i].months_left);
        s.at(o + 6) = uint8_t(b.adverts[i].flags);
        s.at(o + 7) = uint8_t(b.adverts[i].sponsor);
        s.at(o + 8) = uint8_t(b.adverts[i].min_image);
        s.at(o + 9) = uint8_t(b.adverts[i].min_image >> 8);
    }
}

// Single-player view: only the bank is a party. The bank's own entry about the manager is
// -bank; the original reads that one, the result is the same.
// Savings: 1 % per month (computed as * 1 / 100 like the original, without an overflowing * 10).
long savings_interest(const ManagerBudget& b) { return b.bank > 0 ? b.bank / 100 : 0; }

long loan_interest(const ManagerBudget& b) { return b.bank < 0 ? -(b.bank * 4 / 100) : 0; }  // loan: 4 % per month

// Strength-based income (team strength * 15 * players / 3) plus advertising, with savings interest when `full`.
long monthly_income(const ManagerBudget& b, bool full) {
    long income = long(b.team_strength) * 15 * b.players / 3;
    for (const AdvertContract& a : b.adverts) income += a.amount;
    if (full) income += savings_interest(b);
    return std::max(income, 0L);
}

// Expense formula of finances.hpp; never negative.
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

int attendance_kind(GateCompetition competition, const GameRules& rules) {
    switch (competition) {
    case GateCompetition::Cup:
    case GateCompetition::Playoffs: return 1;
    case GateCompetition::EuroGroup: return 2;
    case GateCompetition::EuroFinal: return rules.v130() ? 2 : 0;
    case GateCompetition::League:
    case GateCompetition::WorldChampionship: return 0;
    }
    return 0;
}

// Spectators of a home game and the ticket income booked to the balance; league games also update the
// attendance statistics. The random draws and their order follow the original.
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
    // non-league games still sell tickets, but do not enter
    // the league-only totals, extrema, count or 22-byte attendance graph.
    if (m.league_statistics) {
        b.attendance_total += a;
        if (a >= b.attendance_record) {
            b.attendance_record = a;
            b.record_guest = m.guest;
        }
        if (a <= b.attendance_min) {
            b.attendance_min = a;
            b.min_guest = m.guest;
        }
        b.home_games = uint8_t(b.home_games + 1);
    }
    return a;
}

// One day of bookkeeping: interest accrues daily (a month's interest / (month_days + 1)); on the 1st the advertising
// contracts count down; on the last day income minus expenses plus the accrued interest is booked and the balance
// clamped to +-kBalanceLimit.
void finance_day(const FinanceTables& t, ManagerBudget& b, int day, int month_days) {
    long interest = savings_interest(b) - loan_interest(b);
    if (b.balance < 0) interest += b.balance / 10;
    b.accrued += interest / (month_days + 1);

    if (day == 1)
        for (AdvertContract& a : b.adverts) {
            if (a.amount == 0 || a.months_left == 0) continue;
            // an expired contract has only its amount cleared; the other fields survive until the record is reused.
            if (--a.months_left == 0 && (a.flags & 0x7F) == 0) a.amount = 0;
        }

    if (day == month_days) {
        b.stored_income = monthly_income(b, false);  // as the game stores it: excludes savings interest
        b.balance += b.stored_income - monthly_expenses(t, b, false, false) + b.accrued;
        b.balance = std::clamp(b.balance, -kBalanceLimit, kBalanceLimit);
        b.accrued = 0;
    }
}

// Weekly stadium step: possibly one random loss (wear of a building or places closed by the DEB), then the
// construction countdowns (manager block +0x128.., one byte per item); a finished item takes its planned level
// (+0x117 + 2 * item) or planned places (+0x135 + 4 * kind). Returns the number of finished constructions.
int stadium_week(const FinanceTables& t, ManagerBudget& b, Bytes& state, int manager, BorlandRandom& rng,
                 const std::function<void(const std::string&, const std::string&)>& message) {
    if (manager < 0 || manager >= 4) throw FormatError("Stadium: invalid manager");
    // the game's texts (item names, endings of "Ihre", two-line messages, place kinds) in its character set
    static const char* const kItem[9] = {"]berdachung", "Anzeigetafel", "K}hlanlage", "Sitzkomfort", "Zustand",
                                         "Verkehrsanb.", "Gastronomie", "Kino", "Bowlingbahn"};
    static const char* const kEnding[9] = {"r", "r", "r", "", "", "r", "r", "m", "r"};
    static const char* const kPlaceKinds[2] = {"Sitzpl{tze", "Stehpl{tze"};
    auto report = [&](const std::string& a, const std::string& b2) {
        if (message) message(a, b2); else rng.range(0, 3);  // the date draw of the message
    };
    auto thousands = [](long v) {  // the game's number formatting without decimals: digits grouped by '.'
        std::string s = std::to_string(v);
        for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ".");
        return s;
    };
    const size_t block = kManagerBlock + size_t(manager) * kManagerSize;
    // the original stops after the first actual loss. Even an empty item
    // consumes the preceding probability and division draws.
    if (rng.range(0, t.level * 2 + 8) == 0) {
        for (int item = 0; item < 11; item++) {
            if (item < 9 && (item == 2 || item == 5) && t.level != 0) continue;
            const int maximum = item >= 9 ? 3000 : (item == 3 || item == 4 ? 2500 : 5000);
            if (rng.range(0, maximum) >= (5 - t.level) * 20 || b.league >= rng.range(1, 3)) continue;
            if (item < 9) {
                if (b.stadium[size_t(item)] == 0) continue;
                b.stadium[size_t(item)]--;
                if (item == 3 || item == 4)
                    report("Ihre Fans sind einfach zu st}rmisch...",
                           item == 3 ? "Der Sitzkomfort mu~ leider etwas schlechter beurteilt werden."
                                     : "Das schl{gt sich auch auf den Allgemeinzustand des Stadions nieder.");
                else
                    report("Der Zahn der Zeit nagt auch an IHREM Stadion...",
                           std::string("Er knabberte etwas an Ihre") + kEnding[item] + " " + kItem[item] + ".");
            } else {
                int& places = b.places[size_t(item - 9)];
                if (places == 0) continue;
                const int loss = (rng.range(places / 20, places / 6) + 150) / 100 * 100;
                places = uint16_t(places - loss);
                report("Der DEB hat die Sicherheit Ihres Stadions ger}gt !",
                       thousands(loss) + " " + kPlaceKinds[item - 9] + " wurden gesperrt.");
            }
            break;
        }
    }
    int completed = 0;
    for (size_t item = 0; item < 11; item++) {
        uint8_t& weeks = state.at(block + 0x128 + item);
        if (weeks == 0 || --weeks != 0) continue;
        if (item < 9) {
            b.stadium[item] = state.at(block + 0x117 + item * 2);
            state.at(block + 0x117 + item * 2) = 0;
        } else {
            const size_t planned = block + 0x135 + (item - 9) * 4;
            b.places[item - 9] = le16(state, planned);
            state.at(planned) = state.at(planned + 1) = 0;
        }
        completed++;
        rng.range(0, 3);  // the completion message picks one of four texts: one draw per building
    }
    for (size_t i = 0; i < b.stadium.size(); i++) state.at(block + kStadium + i * 2) = uint8_t(b.stadium[i]);
    for (size_t i = 0; i < b.places.size(); i++) {
        state.at(block + kPlaces + i * 4) = uint8_t(b.places[i]);
        state.at(block + kPlaces + i * 4 + 1) = uint8_t(b.places[i] >> 8);
    }
    return completed;
}

}  // namespace em
