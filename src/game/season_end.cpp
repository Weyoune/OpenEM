// SPDX-License-Identifier: GPL-3.0-only
// Season-end rules: prizes, scoring titles, career history, licence withdrawal, player turnover
// and new team ratings; see season_end.hpp.
#include "game/season_end.hpp"

#include <algorithm>

namespace em {

long season_prizes(bool promoted, int league, bool scorer, bool goalkeeper, bool fair_play) {
    long money = 0;
    if (promoted) money += league == 0 ? kPromotionPrize : kPromotionPrizeLower;
    if (scorer) money += kTitlePrize;
    if (goalkeeper) money += kTitlePrize;
    if (fair_play) money += kTitlePrize;
    return money;
}

std::array<int, 2> season_scoring_winners(const std::vector<SeasonScorer>& players,
                                          int first_team, int last_team) {
    auto order = players;
    auto score = [](const SeasonScorer& p) {
        // The goalkeeper's ratio is negative, putting eligible skaters first.
        // Assists/minutes are signed 16-bit values as in the original, and a 400-minute minimum applies.
        const int value = int16_t(uint16_t(p.assists_or_minutes));
        return p.position == 0 ? (value < 400 ? -99999L : -long(p.goals) * 6000 / value)
                               : long(p.goals) + value;
    };
    for (size_t i = 0; i < order.size(); i++)
        for (size_t j = i + 1; j < order.size(); j++)
            if (score(order[j]) > score(order[i]) ||
                (score(order[j]) == score(order[i]) && order[j].games < order[i].games))
                std::swap(order[i], order[j]);
    std::array<int, 2> winners{-1, -1};
    for (const auto& p : order) {
        if (p.team < first_team || p.team > last_team) continue;
        const size_t kind = p.position == 0 ? 1 : 0;
        if (winners[kind] >= 0) continue;
        if (kind == 1 ? int16_t(uint16_t(p.assists_or_minutes)) < 399
                      : p.goals + int16_t(uint16_t(p.assists_or_minutes)) <= 1) continue;
        winners[kind] = p.team;
    }
    return winners;
}

bool financial_license_due(long balance, int league, uint8_t flags, int withdrawals) {
    return balance < -2000000 && league >= 0 && league < 2 &&
           (flags & 0x28) == 0 && withdrawals >= 0 && withdrawals < 2;
}

void append_career_season(Bytes& state, int manager, int year, const CareerSeason& season) {
    if (manager < 0 || manager >= 4 || year < 0 || year > 65535 ||
        season.place < 1 || season.place > 53 || season.league < 0 || season.league > 2 ||
        season.cup_round < 0 || season.cup_round > 30 || season.euro_phase < 0 || season.euro_phase > 30)
        throw FormatError("Season: invalid career history entry");
    const size_t block = 0x0DCD + size_t(manager) * 0x1AF;
    const size_t base = block + 0x18;
    if (block + 0xEF > state.size()) throw FormatError("Season: truncated career history");
    if (year > 49) {
        std::move(state.begin() + long(base + 4), state.begin() + long(base + 200),
                  state.begin() + long(base));
        state.at(block + 0xEE)++;
    }
    const size_t row = base + size_t(std::min(year, 49)) * 4;
    state.at(row) = uint8_t((season.place | (season.champion ? 0x80 : 0)) + season.league * 12);
    state.at(row + 1) = uint8_t(season.league);
    state.at(row + 2) = uint8_t(season.cup_round + (season.cup_winner ? 0x80 : 0));
    if (season.euro_phase != 0 && season.euro_phase != 30)
        state.at(row + 3) = uint8_t(season.euro_winner ? 0x8D : season.euro_phase + 8);
}

void insert_financial_relegation(std::array<int, 4>& teams, const std::array<int, 4>& ranking,
                                int league, int team, int withdrawals) {
    if (league < 0 || league >= 2 || team < league * 12 || team >= (league + 1) * 12 ||
        withdrawals < 0 || withdrawals >= 2)
        throw FormatError("Season: invalid financial relegation case");
    std::array<bool, 4> seen{};
    for (int slot : ranking) {
        if (slot < 0 || slot >= 4)  // the original does not check for doubled entries (an unsorted order is all 0)
            throw FormatError("Season: invalid relegation ranking");
        seen[size_t(slot)] = true;
    }
    const int upper_limit = (league + 1) * 12;
    for (int place = withdrawals + 2; place < 4; place++) {
        const size_t slot = size_t(ranking[size_t(place)]);
        if (teams[slot] < upper_limit) {
            teams[slot] = team;
            return;
        }
    }
    // Both remaining losers belong to the lower division. Promote the first
    // one into the corresponding winner slot, then insert the licensed club.
    // The original does not remove another occurrence of that club.
    const size_t winner = size_t(ranking[size_t(withdrawals)]);
    const size_t loser = size_t(ranking[size_t(withdrawals + 2)]);
    teams[winner] = teams[loser];
    teams[loser] = team;
}

bool player_retires(int age, BorlandRandom& rng) { return age > rng.range(37, 42); }

NewPlayer new_player(BorlandRandom& rng) {
    NewPlayer p;
    p.age = rng.range(18, 25);
    const int base = rng.range(40, 92);
    p.skill1 = rng.range(base - 5, base + 5);
    p.skill2 = rng.range(base - 5, base + 5);
    return p;
}

RatingBounds load_rating_bounds(const Bytes& exe) {
    // file offset of the data segment, offset of the bounds table within it
    constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0, kBounds = 0x0125;
    RatingBounds b;
    for (size_t c = 0; c < 3; c++) b.bounds[c] = {exe.at(kDgroupFile + kBounds + 2 * c), exe.at(kDgroupFile + kBounds + 2 * c + 1)};
    if (b.bounds[0][0] != 69 || b.bounds[0][1] != 94) throw FormatError("EM.EXE: rating bounds not found");
    return b;
}

void rerate_team(HokTeam& team, int team_class, const RatingBounds& b, GameRandom& rng, int mode) {
    const auto& lim = b.bounds[size_t(std::clamp(team_class, 0, 2))];
    const int lo = mode == 3 ? lim[0] : 1, hi = mode == 3 ? lim[1] : 99;
    const int step = mode == 3 ? 3 : 1;  // the original passes 1 or 3
    const int plus = mode == 3 ? 3 : 1;
    const long r = rng.range(0, 2 * step);
    const long trend = rng.range(0, 2 * r + 2 * plus) - (step + plus);
    for (int k = 0; k < 2; k++)
        team.skill1[k] = std::clamp(int(team.skill1[k] + rng.range(trend - 1, trend + 1)), lo, hi);
    for (int k = 0; k < 2; k++) {
        int v = team.skill2[k];
        const bool change = rng.range(0, 2) == 0 || mode == 3;  // drawn but ignored in mode 3, as in the original
        if (change) v += int(rng.range(trend - 1, trend + 1));
        team.skill2[k] = std::clamp(v, lo, hi);
    }
    team.form[0] = int(rng.range(45, 55));
    team.form[1] = int(rng.range(45, 55));
    team.energy[0][0] = int(rng.range(45, 55));  // the third loop step writes +0x1E
}

}  // namespace em
