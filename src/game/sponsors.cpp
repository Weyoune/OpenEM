// SPDX-License-Identifier: GPL-3.0-only
#include "game/sponsors.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;  // see schedule.cpp
constexpr size_t kKindFactor = 0x4F05, kEconomy = 0x02C0, kStadiumWeight = 0x4F9C;
}  // namespace

SponsorTables load_sponsor_tables(const Bytes& exe) {
    SponsorTables t;
    auto at = [&](size_t ds) { return long(int32_t(le32(exe, kDgroupFile + ds))); };
    for (size_t i = 0; i < t.kind_factor.size(); i++) t.kind_factor[i] = at(kKindFactor + 4 * i);
    t.economy = at(kEconomy);
    for (size_t i = 0; i < t.stadium_weight.size(); i++) t.stadium_weight[i] = at(kStadiumWeight + 4 * i);
    if (t.kind_factor[2] != 32 || t.stadium_weight[0] != 4333) throw FormatError("EM.EXE: sponsor tables not found");
    return t;
}

void generate_sponsor_offers(const SponsorTables& t, int image, bool forced, SponsorOffers& offers,
                             BorlandRandom& rng) {
    const int d = image * 10;
    for (int k = 0; k < kSponsorKinds; k++)
        for (int i = 0; i < kSponsorOffers; i++) {
            AdRecord& o = offers[size_t(k)][size_t(i)];
            o.sponsor = i;
            bool create = true;
            if (!forced) {
                if (rng.range(0, i * 2 / 3) != 0) continue;  // the offer stays as it is
                if (i >= 5 && k == 2)
                    create = false;
                else if (rng.range(0, 5 - d / 200) != 0)
                    create = rng.range(0, 4) > i;
            }
            if (!create) {
                o.amount = 0;
                continue;
            }
            const int r = rng.range(20, 22);
            long a = long(rng.range(d * r, d * r / 10 * 13)) * t.kind_factor[size_t(k)] / 10;
            a = a * ((i + 3 * (k == 2 ? 1 : 0)) * 8 + 65) * 6 / 100;
            a = a * (t.economy - 20) / 100;
            o.amount = a;
            o.boards = rng.range(0, r - 15) == 0 ? rng.range(2, 3) : 1;
            const int years = rng.range(1, 2);
            o.renewal = rng.range(0, 7) == 0 ? rng.range(1, 3 - years) * 12 : 0;
            o.months = years * 12;
            o.min_image = d / 10;
        }
}

bool sponsor_renews(const AdRecord& contract, int image, BorlandRandom& rng) {
    return long(rng.range(90, 120)) * contract.min_image / 100 < image;
}

long stadium_score(const SponsorTables& t, const std::array<int, 9>& stadium, const std::array<int, 2>& places) {
    long s = std::min((places[0] * 3L / 2 + places[1]) / 2, 50000L);
    for (size_t i = 0; i < 9; i++) {
        long level = stadium[i];
        if (i == 0 && level > 0) level--;
        if (i == 2 && level < 1) level = 1;
        if (i == 3) level = 2;  // fixed in the original
        if (i == 4) level = 3;
        s += level * t.stadium_weight[i];
    }
    return s / 700;
}

int club_image(const ImageInputs& in, BorlandRandom& rng) {
    const int lg = std::clamp(in.league, 0, 2);
    long s = (2 - lg) * (in.in_competition * 80L + 170);
    s += rng.range(32, 32) * long(in.cup - 2);
    s += rng.range(47, 47) * long(in.euro);
    int form = 0;
    for (int r : in.recent_results) form += r + 1;
    const int window = in.matchday - std::max(in.matchday - 5, 0);
    form = form && window > 0 ? form / window : 6;
    s += rng.range(12, 12) * long(11 - form);
    long mean = 0;
    if (!in.squad_values.empty()) {
        unsigned long sum = 0;
        for (long v : in.squad_values) sum += unsigned(v / 5500);
        mean = long(sum / in.squad_values.size());
        s += mean;
        int stars = 0;
        for (long v : in.squad_values)
            if (unsigned(v / 7000) > unsigned(mean * rng.range(35, 55) / 10) && stars < 4) stars++;
        s += rng.range(20, 50) * long(stars);
    }
    s += 3 * in.stadium;
    long image = s / 12 + (2 - lg) * 10;
    image = image * 85 / 100;
    return int(std::min(image, 100L));
}

}  // namespace em
