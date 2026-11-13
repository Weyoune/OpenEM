// SPDX-License-Identifier: GPL-3.0-only
// Sponsor offers, contract renewal, stadium score and club image; see sponsors.hpp.
#include "game/sponsors.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;  // file offset of the EM.EXE data segment, see schedule.cpp
// Offsets of the sponsor constants within the data segment.
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

SponsorOffers sponsor_offers_from_save(const Bytes& state, int manager) {
    if (manager < 0 || manager >= 4) throw FormatError("HOK: invalid human manager index");
    SponsorOffers offers{};
    for (size_t k = 0; k < offers.size(); k++)
        for (size_t i = 0; i < offers[k].size(); i++) {
            const size_t o = 0xA52C + size_t(manager) * 0x1F4 + (k * kSponsorOffers + i) * 10;
            offers[k][i] = {long(int32_t(le32(state, o))), state.at(o + 4), state.at(o + 5), state.at(o + 6),
                            state.at(o + 7), le16(state, o + 8)};
        }
    return offers;
}

void store_sponsor_offers(Bytes& state, const SponsorOffers& offers, int manager) {
    if (manager < 0 || manager >= 4) throw FormatError("HOK: invalid human manager index");
    for (size_t k = 0; k < offers.size(); k++)
        for (size_t i = 0; i < offers[k].size(); i++) {
            const size_t o = 0xA52C + size_t(manager) * 0x1F4 + (k * kSponsorOffers + i) * 10;
            const AdRecord& a = offers[k][i];
            for (int b = 0; b < 4; b++) state.at(o + size_t(b)) = uint8_t(uint32_t(a.amount) >> (8 * b));
            state.at(o + 4) = uint8_t(a.boards);
            state.at(o + 5) = uint8_t(a.months);
            state.at(o + 6) = uint8_t(a.renewal);
            state.at(o + 7) = uint8_t(a.sponsor);
            state.at(o + 8) = uint8_t(a.min_image);
            state.at(o + 9) = uint8_t(a.min_image >> 8);
        }
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
    // unsigned 16-bit arithmetic of the original: places * 3 wraps at 65536 (above 21845 places)
    const uint16_t half = uint16_t(uint16_t(places[0] * 3) / 2);
    long s = std::min(long(uint16_t(half + uint16_t(places[1])) / 2), 50000L);
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

ImageCompetitionFactors image_competition_factors(const HokFile& save, int manager) {
    if (manager < 0 || manager >= 6 || save.state.size() < 0x8F88)
        throw FormatError("Image: invalid manager or truncated competition state");
    const auto& state = save.state;
    auto owned = [&](int team) {
        return team >= 0 && size_t(team) < save.teams.size() &&
               save.teams[size_t(team)].raw[0x16] == manager + 1;
    };
    auto group = [&](int kind, int index, int round) {
        // Is the manager's club in group `index` of competition `kind` (1 relegation round,
        // 2 European cup groups, 3 European cup final group) in `round`? Groups that are not
        // formed yet count as empty, as in the original. The first three schedule rows also
        // describe the return dates with the sides reversed.
        if (round < 0 || round >= 6 || (kind == 1 && state[0x03B6] == 0xFF) ||
            (kind == 3 && state[0x8F86] < 6)) return false;
        const size_t at = 0x03B8 + size_t(kind * 0x48 + index * 0x24 + (round % 3) * 6);
        for (size_t slot = 0; slot < 4; slot++) if (owned(state[at + slot])) return true;
        return false;
    };
    ImageCompetitionFactors factors;
    const int playoff_game = state[0x0259];
    const int relegation_round = state[0x8F87] * 2 + (playoff_game != 0) + (playoff_game > 2);
    factors.playoffs = group(1, 0, relegation_round) || group(1, 1, relegation_round);
    // The play-off query includes a drawn play-off participant even when
    // that particular series is already decided; the next bracket removes it.
    for (size_t slot = state[0x03B6]; slot < 32; slot++) {
        const int team = state[0x8E91 + slot];
        if (team == 0xFF) break;
        if (owned(team)) { factors.playoffs = 1; break; }
    }
    const int cup_round = state[0x8ED1];
    if (cup_round != 0) {
        factors.cup = 0;
        for (size_t slot = 0; slot < 32 && state[0x8E51 + slot] != 0xFF; slot++)
            if (owned(state[0x8E51 + slot])) { factors.cup = cup_round + 1; break; }
    }
    const int euro_round = state[0x8F86];
    const bool euro = euro_round < 6 ? group(2, 0, euro_round) || group(2, 1, euro_round)
                                     : group(3, 0, euro_round - 6);
    factors.euro = euro ? euro_round + 1 : 0;
    return factors;
}

namespace {
// The image formula of sponsors.hpp; `value(slot)` yields the market value of squad slot 0..count-1
// (and may draw random numbers itself).
template <class Value>
int calculate_club_image(const ImageInputs& in, size_t count, Value value, BorlandRandom& rng) {
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
    if (count != 0) {
        unsigned long sum = 0;
        for (size_t slot = 0; slot < count; slot++) sum += unsigned(value(slot) / 5500);
        mean = long(sum / count);
        s += mean;
        int stars = 0;
        for (size_t slot = 0; slot < count; slot++) {
            // Sequence explicitly: value recalculation may itself draw rand().
            const long current = value(slot);
            const int threshold = rng.range(35, 55);
            if (unsigned(current / 7000) > unsigned(mean * threshold / 10) && stars < 4) stars++;
        }
        s += rng.range(20, 50) * long(stars);
    }
    s += 3 * in.stadium;
    long image = s / 12 + (2 - lg) * 10;
    image = image * 85 / 100;
    return int(std::min(image, 100L));
}
}  // namespace

int club_image(const ImageInputs& in, BorlandRandom& rng) {
    return calculate_club_image(in, in.squad_values.size(),
                                [&](size_t slot) { return in.squad_values[slot]; }, rng);
}

int club_image(const ImageInputs& in, const std::vector<ContractPlayer>& squad,
               const ContractTables& contracts, BorlandRandom& rng) {
    return calculate_club_image(in, squad.size(),
        [&](size_t slot) { return player_worth(contracts, squad[slot], Worth::Value, rng); }, rng);
}

}  // namespace em
