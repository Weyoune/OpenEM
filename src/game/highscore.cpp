// SPDX-License-Identifier: GPL-3.0-only
#include "game/highscore.hpp"

#include <algorithm>
#include <cstring>

#include "game/contracts.hpp"
#include "game/game.hpp"

namespace em {
namespace {
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;  // see schedule.cpp
constexpr size_t kManagers = 0x0DCD, kManagerSize = 0x1AF, kTeams = 0x17E7, kTeamSize = 0x3D;
constexpr size_t kTeamRecords = 0x3BA5, kTeamRecordSize = 0x50;  // DS:8257
constexpr size_t kShares = 0x30, kPrices = 0xB0;                   // DS:D214, DS:D0F4
constexpr size_t kSquad = 0x696D, kSquadRecord = 0x34, kSquadBlock = 0x618;
constexpr size_t kFileNumber = 0x06AE, kWmOnly = 0x025A, kNationalTeam = 0x068C, kCareerYear = 0xB951;

long s32(const Bytes& b, size_t o) { return long(int32_t(le32(b, o))); }
size_t manager_block(int m) { return kManagers + size_t(m) * kManagerSize; }
std::string cstring(const uint8_t* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len]) len++;
    return std::string(reinterpret_cast<const char*>(p), len);
}
// ovl12_0d27(manager, 0)
int count_players(const Bytes& s, int m) {
    int n = 0;
    for (int i = 0; i < 0x1D; i++)
        if (s.at(kSquad + size_t(m) * kSquadBlock + size_t(i) * kSquadRecord + 0x0F) != 0) n++;
    return n;
}
}  // namespace

HighscoreRecord HighscoreTable::row(int i) const {
    const uint8_t* r = bytes.data() + size_t(i) * kHighscoreRecord;
    HighscoreRecord out;
    out.name = cstring(r, 0x1A);
    out.club = cstring(r + 0x1A, 0x17);
    for (size_t k = 0; k < 3; k++) out.titles[k] = r[0x31 + k];
    out.score = int16_t(r[0x34] | r[0x35] << 8);
    out.game = uint32_t(r[0x36]) | uint32_t(r[0x37]) << 8 | uint32_t(r[0x38]) << 16 | uint32_t(r[0x39]) << 24;
    return out;
}

void HighscoreTable::set_row(int i, const HighscoreRecord& in) {
    uint8_t* r = bytes.data() + size_t(i) * kHighscoreRecord;
    std::fill(r, r + kHighscoreRecord, uint8_t(0));
    std::memcpy(r, in.name.data(), std::min<size_t>(in.name.size(), 0x19));
    std::memcpy(r + 0x1A, in.club.data(), std::min<size_t>(in.club.size(), 0x16));
    for (size_t k = 0; k < 3; k++) r[0x31 + k] = in.titles[k];
    r[0x34] = uint8_t(in.score);
    r[0x35] = uint8_t(in.score >> 8);
    for (int k = 0; k < 4; k++) r[0x36 + k] = uint8_t(in.game >> (8 * k));
}

HighscoreTables load_highscore_tables(const Bytes& exe) {
    HighscoreTables t;
    for (size_t i = 0; i < t.stadium_value.size(); i++) t.stadium_value[i] = le32(exe, kDgroupFile + 0x00C0 + 4 * i);
    for (size_t i = 0; i < t.place_value.size(); i++) t.place_value[i] = le32(exe, kDgroupFile + 0x00E4 + 4 * i);
    for (size_t i = 0; i < t.league_offset.size(); i++) t.league_offset[i] = int8_t(exe.at(kDgroupFile + 0x07C0 + i));
    return t;
}

int highscore_file_number(const HokFile& h) { return int16_t(le16(h.state, kFileNumber)); }
uint32_t highscore_game_identity(const HokFile& h) { return le32(h.header, 0xB4); }
std::filesystem::path highscore_file_name(int number) {
    // DOS keeps three extension characters: the WM-only list 30000 (DS:0256 = 7530h) is H.300.
    return std::filesystem::path("HIGH") / ("H." + std::to_string(number).substr(0, 3));
}

long highscore_base(const HighscoreTables& t, const HokFile& h, int m, int image, const std::vector<long>& values,
                    int managers) {
    const Bytes& s = h.state;
    const size_t M = manager_block(m);
    long v = long(image) * 100;
    for (size_t i = 0; i < 9; i++) v += long(t.stadium_value[i] / 1000) * long(s.at(M + 0x116 + 2 * i));
    for (size_t i = 0; i < 2; i++)
        v += long(uint32_t(uint32_t(t.place_value[i]) * uint32_t(le16(s, M + 0x133 + 4 * i))) / 1000000u);
    long w = s32(s, M + 0x18F);
    for (size_t k = 0; k < 4; k++)
        w += long(int32_t(le32(s, kShares + 4 + size_t(m) * 0x20 + k * 8) * uint32_t(le16(s, kPrices + 0x46 + k * 0x48))));
    for (int j = 0; j < 5; j++) {
        if (j == m) continue;
        const long claim = s32(s, manager_block(j) + 0x19B + 4 * size_t(m));
        if (claim < 0) w -= claim;
    }
    long own = 0;  // ovr039:02E8(0, 99)
    for (int j = 0; j < 5; j++) {
        if (j == m || (j >= managers && j != 4)) continue;
        const long claim = s32(s, M + 0x19B + 4 * size_t(j));
        if (claim < 0) own -= claim;
    }
    w -= own;
    for (long value : values) w += value / 2;
    w = std::min(w, 50000000L);
    v += w / 2000;
    const int team = s.at(M + 0x14), league = s.at(M + 0xEB);
    const int place = s.at(kTeamRecords + size_t(team) * kTeamRecordSize + 0x31);
    const int offset = league >= 0 && league < 3 ? t.league_offset[size_t(league)] : 0;
    const int rank = 36 - (place + offset + 1);
    v += 25L * std::max(rank, 0);
    for (size_t i = 0; i < 4; i++) {
        const int b = s.at(M + 0xE6 + i);
        if (b > 0 && b < 7) v += 150L * b;
    }
    const int rows = std::min<int>(s.at(kCareerYear), 50);
    long history = 0;
    for (int i = 0; i < rows; i++) {
        const size_t e = M + 0x18 + 4 * size_t(i);
        if (s.at(e) != 0xFF) history += long(int16_t((36 - s.at(e)) * 7));
        const uint8_t b2 = s.at(e + 2);
        if (b2 & 0x80) {
            history += 80;
            const uint8_t b3 = s.at(e + 3);
            history += (b3 & 0x80) ? 130 : (b3 & 7) * 20;
        } else {
            history += (b2 & 7) * 10;
        }
    }
    if (rows) history /= rows + 1;
    v += history;
    return v / 20;
}

HighscoreRecord highscore_record(const HighscoreTables& t, const HokFile& h, int m, int image,
                                 const std::vector<long>& values, int managers) {
    const Bytes& s = h.state;
    const size_t M = manager_block(m);
    HighscoreRecord r;
    r.name = cstring(s.data() + M, 0x1A);
    if (s.at(kWmOnly) == 1) {
        const int team = s.at(kNationalTeam);
        r.club = cstring(s.data() + kTeams + size_t(team) * kTeamSize, 0x16);
        int score = 0;
        for (size_t cl = 0; cl < kTeamRecordSize - 0x34; cl++) {
            const int place = s.at(kTeamRecords + size_t(team) * kTeamRecordSize + 0x34 + cl);
            if (!place) break;
            score += 9 - place;
        }
        r.score = int16_t(score);
    } else {
        r.club = cstring(s.data() + kTeams + size_t(s.at(M + 0x14)) * kTeamSize, 0x16);
        for (size_t k = 0; k < 3; k++) r.titles[k] = s.at(M + 0x15 + k);
        int score = int16_t(highscore_base(t, h, m, image, values, managers));
        score = int16_t(score + r.titles[0] * 8 + r.titles[1] * 6 + r.titles[2] * 10);
        r.score = score;
    }
    if (r.score < 1) r.score = 1;
    return r;
}

void merge_highscore(HighscoreTable& table, const HighscoreRecord& record, uint32_t game) {
    int own = -1;
    for (int i = 0; i < kHighscoreRows; i++) {
        const auto r = table.row(i);
        if (r.name == record.name && r.club == record.club) { own = i; break; }
    }
    HighscoreRecord entry = record;
    entry.game = game;
    for (int i = 0; i < kHighscoreRows; i++) {
        if (table.row(i).score >= record.score) continue;
        if (own >= 0) i = own;
        else
            for (int j = kHighscoreRows - 1; j > i; j--)
                std::copy_n(table.bytes.begin() + long(j - 1) * long(kHighscoreRecord), kHighscoreRecord,
                            table.bytes.begin() + long(j) * long(kHighscoreRecord));
        table.set_row(i, entry);
        break;
    }
    for (int i = 0; i < kHighscoreRows - 1; i++)
        for (int j = i + 1; j < kHighscoreRows; j++)
            if (table.row(i).score < table.row(j).score) {
                std::array<uint8_t, kHighscoreRecord> tmp{};
                auto a = table.bytes.begin() + long(i) * long(kHighscoreRecord), b = table.bytes.begin() + long(j) * long(kHighscoreRecord);
                std::copy_n(a, kHighscoreRecord, tmp.begin());
                std::copy_n(b, kHighscoreRecord, a);
                std::copy_n(tmp.begin(), kHighscoreRecord, b);
            }
}

HighscoreTable read_highscore_table(const Bytes& bytes) {
    HighscoreTable t;
    std::copy_n(bytes.begin(), std::min(bytes.size(), t.bytes.size()), t.bytes.begin());
    return t;
}

// Native page entry for manager m: the image (ovl08_2893) and the market values draw random numbers in this order.
HighscoreRecord Game::highscore_record(int manager, const HighscoreTables& tables) {
    image_ = current_club_image();
    const HokFile h = decode_hok(save_bytes());
    std::vector<long> values;
    const int n = count_players(h.state, manager);
    for (int slot = 0; slot < n; slot++)
        values.push_back(player_worth(contracts_, contract_player(h, manager, slot), Worth::Value, brng_));
    return em::highscore_record(tables, h, manager, image_, values, 1);
}

}  // namespace em
