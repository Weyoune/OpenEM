// SPDX-License-Identifier: GPL-3.0-only
#include "game/season_end.hpp"

#include <algorithm>

namespace em {

long season_prizes(bool champion, int league, bool cup, bool second_title, bool third_title) {
    long money = 0;
    if (champion) money += league == 0 ? kChampionPrize : kChampionPrizeLower;
    if (cup) money += kTitlePrize;
    if (second_title) money += kTitlePrize;
    if (third_title) money += kTitlePrize;
    return money;
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
    constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0, kBounds = 0x0125;
    RatingBounds b;
    for (size_t c = 0; c < 3; c++) b.bounds[c] = {exe.at(kDgroupFile + kBounds + 2 * c), exe.at(kDgroupFile + kBounds + 2 * c + 1)};
    if (b.bounds[0][0] != 69 || b.bounds[0][1] != 94) throw FormatError("EM.EXE: rating bounds not found");
    return b;
}

void rerate_team(HokTeam& team, int team_class, const RatingBounds& b, GameRandom& rng) {
    const auto& lim = b.bounds[size_t(std::clamp(team_class, 0, 2))];
    const long r = rng.range(0, 6);
    const long trend = rng.range(0, 2 * r + 6) - 6;
    for (int k = 0; k < 2; k++)
        team.skill1[k] = std::clamp(int(team.skill1[k] + rng.range(trend - 1, trend + 1)), lim[0], lim[1]);
    for (int k = 0; k < 2; k++) {
        int v = team.skill2[k];
        if (rng.range(0, 2) == 0) v += int(rng.range(trend - 1, trend + 1));
        team.skill2[k] = std::clamp(v, lim[0], lim[1]);
    }
    team.form[0] = int(rng.range(45, 55));
    team.form[1] = int(rng.range(45, 55));
    team.energy[0][0] = int(rng.range(45, 55));  // the third loop step writes +0x1E
}

}  // namespace em
