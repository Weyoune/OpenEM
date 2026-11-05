// SPDX-License-Identifier: GPL-3.0-only
#include "game/transfers.hpp"

#include <algorithm>
#include <cstdlib>

namespace em {

bool seller_accepts(long price, long bid, BorlandRandom& rng) {
    price &= 0x7FFFFFFFL;
    if (price == 0) { //391D still consumes both threshold draws for a positive bid
        (void)rng.range(75,85);(void)rng.range(110,130);return bid>0;
    }
    if (price * rng.range(75, 85) / 100 > bid) return false;
    if (price * rng.range(110, 130) / 100 < bid) return true;
    return rng.range(70, 120) < bid * 100 / price;
}

long purchase_subsidy(long fee, BorlandRandom& rng) {
    if (rng.range(0, 6) != 0) return 0;
    return long(int64_t(fee) * rng.range(25, 65) / 30000 * 300);
}

int pick_bidder(const std::vector<HokTeam>& teams, int player_strength, bool foreign, BorlandRandom& rng) {
    const int lo = foreign ? 64 : 0, hi = std::min(foreign ? 149 : 42, int(teams.size()) - 1);
    int mult = 1;
    for (int tries = 0;; tries++) {
        if (tries == 100) mult = 2;
        if (tries == 200) mult = 3;
        const int t = rng.range(lo, hi);
        const HokTeam& team = teams[size_t(t)];
        if (team.raw && team.raw[0x16] != 0) continue;  // a manager's club
        const int c = (team.skill1[0] + team.skill1[1] + team.skill2[0] + team.skill2[1] + 100) / 6;
        if (tries > 300 || std::abs(c - player_strength) < rng.range(2, 10 * mult)) return t;
    }
}

long sale_offer(long value, bool foreign_club, BorlandRandom& rng) {
    const int f = foreign_club ? 1 : 0;
    return value * rng.range(87 + 25 * f, 125 + 30 * f) / 30000 * 300;
}

long club_offer(long value, BorlandRandom& rng) {
    long v = value;
    v += rng.range(int(v / 20), int(v / 10)) - v / 15;
    return v * rng.range(85, 115) / 30000 * 300;
}

bool player_accepts(const ContractTables& t, const ContractPlayer& p, int years, long salary, int matchday,
                    int matchdays, int season_day, BorlandRandom& rng) {
    if (years == 0) years = p.years;
    if (salary == 0) salary = p.salary;
    if (years == p.years) {
        if (p.salary < salary) return true;
        if (p.salary > salary) return false;
    }
    if (p.years > years) {  // shorter contract (REAL-CODE test): above 120 % accepted, up to 105 % refused, both without a draw
        if (p.salary * 120 / 100 < salary) return true;
        if (p.salary * 105 / 100 >= salary) return false;
    }
    // A longer contract has no early refusal (REAL-CODE test): the original draws both factors and then compares.
    const long left = 100 - long(matchday) * 100 / std::max(matchdays, 1);
    const long u = (years - 1) * 100L + left;
    long want = player_worth(t, p, Worth::Salary, rng) * rng.range(85, 115) / 100;
    long w = u / 6 + 110;
    w = (w << 3) / 10;
    want = want * rng.range(int(w - 6), int(w + 4)) / 100;
    if (season_day > 232) want = want * rng.range(27 - p.age + 105, 27 - p.age + 125) / 100;
    if (want >= 100000) return false;
    return salary > want;
}

}  // namespace em
