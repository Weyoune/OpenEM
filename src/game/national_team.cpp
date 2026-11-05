// SPDX-License-Identifier: GPL-3.0-only
#include "game/national_team.hpp"
#include "game/lineup.hpp"

#include <algorithm>
#include <cstring>

namespace em {
namespace {
constexpr size_t kPlayers = 0x4FC6, kPlayerSize = 33;
constexpr size_t kSquad = 0x696D + 5 * 0x618, kRecord = 52;
constexpr size_t kNation = 0x068C;
constexpr int kPlaces = 29;

int player_id(const HokFile& h, size_t player) {
    return int((h.players.at(player).raw - h.state.data() - kPlayers) / kPlayerSize) + 1;
}
int player_kind(const HokPlayer& p) { return p.position == 0 ? 0 : p.position < 60 ? 1 : 2; }
void put32(Bytes& state, size_t at, long value) {
    for (int byte = 0; byte < 4; byte++) state.at(at + size_t(byte)) = uint8_t(uint32_t(value) >> (byte * 8));
}
}  // namespace

NationalTeamTables load_national_team_tables(const Bytes& exe) {
    NationalTeamTables t;
    for (size_t kind = 0; kind < 3; kind++) t.places[kind] = exe.at(0x16DD0 + 0x4EDE + kind);
    if (t.places != std::array<int, 3>{3, 10, 16}) throw FormatError("EM.EXE: national squad table not found");
    return t;
}

long national_team_grant(const HokFile& h) {
    const int nation = h.state.at(kNation);
    if (nation < 52 || nation >= 64) return 1500000;
    int place = 0;
    for (size_t year = 0; year < 16; year++) {
        const int entry = h.state.at(0x3BD9 + size_t(nation) * 0x50 + year);
        if (!entry) break;
        place = entry;
    }
    if (!place) return 1500000;
    return place <= 8 ? 900000L + (8 - place) * 300000L : 300000L;
}

bool call_up_national_player(HokFile& h, size_t player, const ContractTables& contracts, BorlandRandom& rng) {
    if (player >= h.players.size()) return false;
    HokPlayer& p = h.players[player];
    const int id = player_id(h, player), nation = h.state.at(kNation);
    if (id < 1 || id >= 180 || p.foreigner || nation < 52 || nation >= 64 || p.team == nation) return false;
    int count = 0;
    while (count < kPlaces && h.state.at(kSquad + size_t(count) * kRecord + 15)) count++;
    if (count == kPlaces) return false;
    int slot = 0;
    while (slot < count) {
        const int existing_id = h.state.at(kSquad + size_t(slot) * kRecord + 15);
        const int position = h.state.at(kPlayers + size_t(existing_id - 1) * kPlayerSize + 24);
        const int kind = position == 0 ? 0 : position < 60 ? 1 : 2;
        if (kind > player_kind(p)) break;
        slot++;
    }
    const size_t at = kSquad + size_t(slot) * kRecord;
    std::memmove(h.state.data() + at + kRecord, h.state.data() + at, size_t(count - slot) * kRecord);
    std::memset(h.state.data() + at, 0, kRecord);
    uint8_t* r = h.state.data() + at;
    r[15] = uint8_t(id);
    r[0x13] = uint8_t(rng.range(80, 120));
    r[0x0E] = uint8_t(rng.range(35, 65));
    int traits = rng.range(1, 13);
    traits |= rng.range(1, 3) << 4;
    traits |= rng.range(0, 1) << 7;
    r[0x14] = uint8_t(traits);
    r[0x0B] = 1;
    r[0x0C] = 99;  // ovl09_2cd1's temporary transfer mode
    r[0x03] = uint8_t(p.league_goals);
    r[0x06] = uint8_t(p.league_games);
    r[0x10] = uint8_t(p.skill_a);
    r[0x11] = uint8_t(p.skill_b);
    r[0x12] = uint8_t(p.form);
    ContractPlayer salary_player = contract_player(h, 5, slot);
    salary_player.age = p.age;
    salary_player.position = p.position;
    salary_player.team = p.team;
    const long salary = player_worth(contracts, salary_player, Worth::Salary, rng);
    put32(h.state, at + 0x28, salary * rng.range(90, 115) / 100);
    // ovl07_30ef preserves the club for return; owning manager (+0x1A) is unchanged.
    r[0x0C] = uint8_t(p.team + 128);
    r[0x19] = 0x77;
    r[0x03] = r[0x04] = r[0x05] = 0;
    p.team = nation;
    h.state.at(kPlayers + size_t(id - 1) * kPlayerSize + 0x1D) = uint8_t(nation);
    return true;
}

bool release_national_player(HokFile& h, size_t player) {
    if (player >= h.players.size()) return false;
    const int id = player_id(h, player);
    for (int slot = 0; slot < kPlaces; slot++) {
        const size_t at = kSquad + size_t(slot) * kRecord;
        if (h.state.at(at + 15) != id) continue;
        const int club = h.state.at(at + 0x0C) & 0x7F;
        h.players[player].team = club;
        h.state.at(kPlayers + size_t(id - 1) * kPlayerSize + 0x1D) = uint8_t(club);
        std::memmove(h.state.data() + at, h.state.data() + at + kRecord, size_t(kPlaces - 1 - slot) * kRecord);
        std::memset(h.state.data() + kSquad + size_t(kPlaces - 1) * kRecord, 0, kRecord);
        return true;
    }
    return false;
}

void release_national_squad(HokFile& h) {
    while (const int id = h.state.at(kSquad + 15)) {
        const auto player = std::find_if(h.players.begin(), h.players.end(), [&](const HokPlayer& p) {
            return p.raw == h.state.data() + kPlayers + size_t(id - 1) * kPlayerSize;
        });
        if (player == h.players.end()) throw FormatError("HOK: missing national squad player");
        release_national_player(h, size_t(player - h.players.begin()));
    }
}

void auto_national_squad(HokFile& h, const NationalTeamTables& tables,
                         const ContractTables& contracts, BorlandRandom& rng) {
    release_national_squad(h);
    for (int kind = 0; kind < 3; kind++)
        for (int place = 0; place < tables.places[size_t(kind)]; place++) {
            size_t best = h.players.size();
            int best_strength = 0;
            // Strict improvement retains the earlier native record on a tie.
            for (size_t i = 0; i < h.players.size(); i++) {
                const HokPlayer& p = h.players[i];
                if (player_id(h, i) >= 180 || p.foreigner || player_kind(p) != kind || p.team == h.state.at(kNation)) continue;
                const int strength = p.skill_a + p.skill_b + p.form;
                if (strength > best_strength) { best = i; best_strength = strength; }
            }
            if (best == h.players.size()) break;
            if (!call_up_national_player(h, best, contracts, rng)) break;
        }
    auto_national_lineup(h);
}

void auto_national_lineup(HokFile& h) {
    auto_lineup(h, 5);
}

}  // namespace em
