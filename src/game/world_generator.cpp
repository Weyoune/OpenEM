// SPDX-License-Identifier: GPL-3.0-only
#include "game/world_generator.hpp"

#include <algorithm>
#include <cstring>

#include "formats/db.hpp"
#include "game/annual_distribution.hpp"
#include "game/annual_players.hpp"
#include "game/competitions.hpp"
#include "game/contracts.hpp"
#include "game/market.hpp"
#include "game/match.hpp"
#include "game/scorers.hpp"
#include "game/transfers.hpp"

namespace em {
namespace {

constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;  // DGROUP in EM.EXE (docs/formats/EXE.md)
constexpr size_t kDgroupInit = 0xE7E0;            // initialised part: up to the FBOV block at 0x255B0
constexpr size_t kTeamRec = 0x3D, kPlayerRec = 0x21, kManagerRec = 0x1AF, kSquadBlock = 0x618, kSquadRec = 0x34;
constexpr int kTeams = 150, kPlayerIds = 200;

// DGROUP offsets
constexpr size_t dsLevel = 0x0118, dsEndChoice = 0x0119, dsSquadSize = 0x011A;
constexpr size_t dsRatingBounds = 0x0125;  // 3 x {low, high}
constexpr size_t dsGroupCounter = 0x015C;  // u8[3] free IDs per position group (counted down)
constexpr size_t dsDbSlots = 0x0164;       // u8[4] database slot bounds per group: 0, 2, 12, 25
constexpr size_t dsGroupSizes = 0x0169;    // u8[4][4] squad size per group and setting
constexpr size_t dsFaces = 0x0178;
constexpr size_t dsShareTarget = 0x01FD;  // u32[4]
constexpr size_t dsNationValue = 0x026C;  // u32[12]
constexpr size_t dsSession = 0x02C4;      // u32
constexpr size_t dsIdBound = 0x0381;      // u8[4] position groups: IDs 1..20, 21..96, 97..199
constexpr size_t dsManagerCount = 0x0386, dsCupWinner = 0x0387, dsEuroWinner = 0x0388;
constexpr size_t dsDayCounter = 0x03B7, dsSeasonYear = 0x03BB;
constexpr size_t dsGermany = 0x06AC, dsFirstNation = 0x06AD, dsDayStart = 0x06AF;
constexpr size_t dsMatchday = 0x07A9, dsWmRound = 0x07AE, dsEuroRound = 0x07AF;
constexpr size_t dsLeagueSize = 0x07B1, dsLeagueStart = 0x07C0;
constexpr size_t dsClassLimit1 = 0x07C1, dsClassLimit2 = 0x07C2;
constexpr size_t dsLevelSalary = 0x0ED8;
constexpr size_t dsYouth = 0xA075;  // youth teams, 0x15 bytes per manager (state 0x06B8)
constexpr size_t dsDistributionCursor = 0x48FB;
constexpr size_t dsNationSkills = 0x4C4E, dsFormString = 0x4DC7;
constexpr size_t dsNameRetry = 0x55EE;  // word_2A5BE: a duplicate name widens the club range by 24
constexpr size_t dsEndYear = 0x5A14, dsYear = 0x5A18, dsMonth = 0x5A1C, dsDay = 0x5A20;
constexpr size_t dsLeagueLists = 0x5CCB, dsCupRound = 0x5CF3, dsCupDraw = 0x5CFB;
constexpr size_t dsSquads = 0x5DC3;
constexpr size_t dsRows = 0x8257;  // 64 x 0x50: titles, table row, curve
constexpr size_t dsManagers = 0x965B;
constexpr size_t dsWmFlag = 0x9FAC;
constexpr size_t dsCurrentManager = 0xA70E, dsLastManager = 0xA70D;
constexpr size_t dsEntrantSlots = 0xA763, dsEntrantCount = 0xA775, dsEntrants = 0xA776;
constexpr size_t dsStartFlag = 0xA899;
constexpr size_t dsHeadToHead = 0xAB7B, dsSeries = 0xB57B, dsRecordOpp = 0xBB0F, dsRecords = 0xBD0F;
constexpr size_t dsCompetition = 0xCFFA, dsCompetitionRound = 0xCFFB;
constexpr size_t dsMode = 0xD0B9, dsWmPhase = 0xD0C1, dsChampion = 0xD0E6;

// The save writer ovl16_11bd: HOK state offset, DGROUP offset, size; heap 1 = teams, 2 = players
// (re/out/hok_layout.csv).
struct Block {
    uint16_t state, ds, size;
    uint8_t heap;
};
constexpr Block kBlocks[] = {
    {0x0000, 0x02A0, 0x0018, 0}, {0x0018, 0x020D, 0x0010, 0}, {0x0028, 0x0118, 0x0004, 0}, {0x002C, 0x0120, 0x0004, 0},
    {0x0030, 0xD214, 0x0080, 0}, {0x00B0, 0xD0F4, 0x0120, 0}, {0x01D0, 0x01FD, 0x0010, 0}, {0x01E0, 0xA89A, 0x0006, 0},
    {0x01E6, 0xA891, 0x0004, 0}, {0x01EA, 0xA895, 0x0004, 0}, {0x01EE, 0x0178, 0x0004, 0}, {0x01F2, 0x017C, 0x0001, 0},
    {0x01F3, 0xA707, 0x0006, 0}, {0x01F9, 0xA701, 0x0006, 0}, {0x01FF, 0xA6FB, 0x0006, 0}, {0x0205, 0xCCB2, 0x0046, 0},
    {0x024B, 0xCCA4, 0x000E, 0}, {0x0259, 0xCCA3, 0x0001, 0}, {0x025A, 0xD0B9, 0x0001, 0}, {0x025B, 0xCC73, 0x0030, 0},
    {0x028B, 0xCC43, 0x0030, 0}, {0x02BB, 0xCD28, 0x0030, 0}, {0x02EB, 0xCCF8, 0x0030, 0}, {0x031B, 0xD074, 0x0001, 0},
    {0x031C, 0xD014, 0x0060, 0}, {0x037C, 0xD075, 0x0020, 0}, {0x039C, 0xCFFC, 0x0018, 0}, {0x03B4, 0xCFF8, 0x0004, 0},
    {0x03B8, 0xCED8, 0x0120, 0}, {0x04D8, 0xCDB8, 0x0120, 0}, {0x05F8, 0xCD88, 0x0030, 0}, {0x0628, 0xCD58, 0x0030, 0},
    {0x0658, 0x026C, 0x0030, 0}, {0x0688, 0xD0BA, 0x0004, 0}, {0x068C, 0x06AC, 0x0001, 0}, {0x068D, 0xD0C1, 0x0001, 0},
    {0x068E, 0xD0DB, 0x0008, 0}, {0x0696, 0xD0D3, 0x0008, 0}, {0x069E, 0xD0CB, 0x0008, 0}, {0x06A6, 0xD0C3, 0x0008, 0},
    {0x06AE, 0x0256, 0x0002, 0}, {0x06B0, 0x02BC, 0x0004, 0}, {0x06B4, 0x02C0, 0x0004, 0}, {0x06B8, 0xA075, 0x0054, 0},
    {0x070C, 0xA8A1, 0x0070, 0}, {0x077C, 0xD0E4, 0x0002, 0}, {0x077E, 0x022D, 0x0006, 0}, {0x0784, 0xA88D, 0x0004, 0},
    {0x0788, 0xA70E, 0x0001, 0}, {0x0789, 0xA70D, 0x0001, 0}, {0x078A, 0x0375, 0x000C, 0}, {0x0796, 0xA0CB, 0x0630, 0},
    {0x0DC6, 0x0386, 0x0001, 0}, {0x0DC7, 0x0387, 0x0001, 0}, {0x0DC8, 0xD0E6, 0x0001, 0}, {0x0DC9, 0x0388, 0x0001, 0},
    {0x0DCA, 0x0389, 0x0001, 0}, {0x0DCB, 0x038A, 0x0001, 0}, {0x0DCC, 0x038B, 0x0001, 0}, {0x0DCD, 0x965B, 0x0A1A, 0},
    {0x17E7, 0x9657, 0x23BE, 1}, {0x3BA5, 0x8257, 0x1400, 0}, {0x4FA5, 0x8253, 0x19C8, 2}, {0x696D, 0x5DC3, 0x2490, 0},
    {0x8DFD, 0x5D7B, 0x0024, 0}, {0x8E21, 0x5A20, 0x0004, 0}, {0x8E25, 0x5A1C, 0x0004, 0}, {0x8E29, 0x5A18, 0x0004, 0},
    {0x8E2D, 0x03B7, 0x0004, 0}, {0x8E31, 0x03BB, 0x0002, 0}, {0x8E33, 0xA776, 0x000A, 0}, {0x8E3D, 0xA763, 0x0012, 0},
    {0x8E4F, 0xA75A, 0x0002, 0}, {0x8E51, 0x5CFB, 0x0080, 0}, {0x8ED1, 0x5CF3, 0x0008, 0}, {0x8ED9, 0x5CF0, 0x0003, 0},
    {0x8EDC, 0x5CCB, 0x0024, 0}, {0x8F00, 0x5C4B, 0x0080, 0}, {0x8F80, 0x07A9, 0x0008, 0}, {0x8F88, 0xAB7B, 0x1594, 0},
    {0xA51C, 0x021D, 0x0010, 0}, {0xA52C, 0xC313, 0x07D0, 0}, {0xACFC, 0xC15B, 0x01B8, 0}, {0xAEB4, 0x0ED8, 0x0001, 0},
    {0xAEB5, 0x5A14, 0x0002, 0}, {0xAEB7, 0xD294, 0x0A98, 0}, {0xB94F, 0x0262, 0x0002, 0}, {0xB951, 0x03BD, 0x0002, 0},
    {0xB953, 0xA969, 0x0002, 0}, {0xB955, 0x0246, 0x0010, 0}, {0xB965, 0xA8A0, 0x0001, 0}, {0xB966, 0x5A50, 0x0005, 0},
    {0xB96B, 0xCAE9, 0x0120, 0}, {0xBA8B, 0xCAE3, 0x0006, 0},
};
constexpr size_t kStateSize = 0xBA91;

void put16(Bytes& b, size_t o, uint16_t v) {
    b.at(o) = uint8_t(v);
    b.at(o + 1) = uint8_t(v >> 8);
}
void put32(Bytes& b, size_t o, uint32_t v) {
    put16(b, o, uint16_t(v));
    put16(b, o + 2, uint16_t(v >> 16));
}

// C string helpers on the raw buffers (bounded by the buffer end)
size_t cstrlen(const Bytes& b, size_t o) {
    size_t n = 0;
    while (o + n < b.size() && b[o + n]) n++;
    return n;
}
bool cstreq(const Bytes& a, size_t ao, const Bytes& b, size_t bo) {
    for (size_t i = 0;; i++) {
        const uint8_t x = ao + i < a.size() ? a[ao + i] : 0, y = bo + i < b.size() ? b[bo + i] : 0;
        if (x != y) return false;
        if (!x) return true;
    }
}
void cstrcpy(Bytes& dst, size_t d, const Bytes& src, size_t s) {
    const size_t n = cstrlen(src, s);
    for (size_t i = 0; i < n && d + i < dst.size(); i++) dst[d + i] = src[s + i];
    if (d + n < dst.size()) dst[d + n] = 0;
}

ContractPlayer contract_from(const uint8_t* r, const uint8_t* p) {
    ContractPlayer c;
    c.bonus_a = r[0x00];
    c.bonus_b = r[0x01];
    c.league_goals = r[0x03];
    c.cup_goals = r[0x04];
    c.other_goals = r[0x05];
    c.flags = r[0x09];
    c.years = r[0x0B];
    c.experience = r[0x0E];
    c.skill1 = r[0x10];
    c.skill2 = r[0x11];
    c.open16 = r[0x16];
    for (size_t i = 0; i < 3; i++) c.games[i] = r[0x1C + 2 * i] | r[0x1D + 2 * i] << 8;
    c.salary = long(int32_t(uint32_t(r[0x28] | r[0x29] << 8 | r[0x2A] << 16 | uint32_t(r[0x2B]) << 24)));
    c.value = long(int32_t(uint32_t(r[0x2C] | r[0x2D] << 8 | r[0x2E] << 16 | uint32_t(r[0x2F]) << 24)));
    c.age = p[0x14];
    c.position = p[0x18];
    c.team = p[0x1D];
    return c;
}
void store32(uint8_t* r, long v) {
    for (int k = 0; k < 4; k++) r[k] = uint8_t(uint32_t(v) >> (8 * k));
}

struct World {
    Bytes ds = Bytes(0x10000, 0);
    Bytes teams = Bytes(size_t(kTeams) * kTeamRec, 0);
    Bytes players = Bytes(size_t(kPlayerIds) * kPlayerRec, 0);
    const Bytes& db;
    ContractTables contracts;
    BorlandRandom brng;
    GameRandom grng{0};  // DS:E017 starts at 0 (dseg:E017 dw 0)
    std::vector<std::string> log;

    World(const Bytes& exe, const Bytes& d) : db(d), contracts(load_contract_tables(exe)) {
        std::copy_n(exe.begin() + long(kDgroupFile), kDgroupInit, ds.begin());
    }
    uint8_t& d(size_t o) { return ds.at(o); }
    uint16_t w(size_t o) const { return le16(ds, o); }
    uint8_t* team(size_t t) { return &teams.at(t * kTeamRec); }
    uint8_t* player(size_t id) { return &players.at(id * kPlayerRec); }
    size_t manager(size_t m) const { return dsManagers + m * kManagerRec; }
    uint8_t& club_of(size_t m) { return d(manager(m) + 0x14); }  // DS:966F
    uint8_t* squad(size_t m, size_t slot) { return &ds.at(dsSquads + m * kSquadBlock + slot * kSquadRec); }
    int b(int lo, int hi) { return brng.range(lo, hi); }
    int g(long lo, long hi) { return int(grng.range(lo, hi)); }

    // ---- ovl04_4d3b (ovr028:4D3B): team records from KB\23.vga --------------------------------------
    void team_records() {
        for (size_t t = 0; t < size_t(kTeams); t++) {  // +0x3B = (s8 hi << 8) + s8 lo of the word table at file 0
            const int v = int(int8_t(db.at(2 * t))) * 256 + int(int8_t(db.at(2 * t + 1)));
            team(t)[0x3B] = uint8_t(v);
            team(t)[0x3C] = uint8_t(v >> 8);
        }
        for (size_t t = 0; t < 64; t++) {
            team(t)[0x34] = uint8_t((db.at(0x12C + t) + 0xB0) & 0x3F);
            cstrcpy(ds, 0x825E + t * 0x50, ds, dsFormString);  // "--------" into the form string of the table row
        }
        const int first = int(int8_t(d(dsFirstNation)));
        int nation = 0;
        for (size_t t = 0; t < size_t(kTeams); t++) {
            const size_t src = 0x16C + 3 * t;
            team(t)[0x17] = uint8_t(b(47, 53));
            for (size_t k = 0; k < 2; k++) {
                const int a = db.at(src + k), spread = a / 10;
                int r = b(0, spread);
                team(t)[0x18 + k] = uint8_t(a - r + b(0, spread));
                r = b(0, spread);
                team(t)[0x1A + k] = uint8_t(a - r + b(0, spread));
                team(t)[0x1C + k] = uint8_t(b(45, 55) - (t > 64 ? 5 : 0));
                if (int(t) >= first && int(t) < first + 12) {  // the national teams take fixed values from DS:4C4E
                    team(t)[0x18 + k] = uint8_t(d(dsNationSkills + size_t(nation++)) - 2);
                    team(t)[0x1A + k] = uint8_t(d(dsNationSkills + size_t(nation++)) - 2);
                    put16(ds, dsRows + 0x4E + t * 0x50, 0);
                    put16(ds, dsRows + 0x4C + t * 0x50, uint16_t(int(t) - first));
                }
            }
        }
        size_t name = 0x4AE;  // club names, then the 23-byte names of the European clubs
        for (size_t t = 0; t < size_t(kTeams); t++) {
            cstrcpy(teams, t * kTeamRec, db, name);
            name += 0x17;
            if (t < 64) name += 0x1F4;
        }
        int counter = 0;
        for (size_t l = 0; l < 3; l++)
            for (size_t j = 0; j < d(dsLeagueSize + l); j++) d(dsLeagueLists + l * 12 + j) = uint8_t(counter++);
    }

    // ---- ovl10_05c4 (ovr034:05C4) --------------------------------------------------------------
    void reset_managers() {
        std::fill_n(ds.begin() + long(dsHeadToHead), 0x1594, uint8_t(0));
        for (size_t m = 0; m < 6; m++)
            for (size_t c = 2; c < 5; c++) d(manager(m) + 0xE6 + c) = 0x1E;
        for (size_t m = 0; m < 4; m++) {
            d(manager(m) + 0x13) = uint8_t(m + 1);
            std::fill_n(ds.begin() + long(dsHeadToHead + m * 0x280), 0x280, uint8_t(0xFF));
        }
    }

    // ---- ovl06_0c29 (ovr030:0C29): two teams change places --------------------------------------
    void swap_teams(int a, int b2, int mode) {
        auto relabel = [&](uint8_t& v) {
            if (v == a) v = uint8_t(b2);
            else if (v == b2) v = uint8_t(a);
        };
        for (size_t id = 1; id < size_t(kPlayerIds); id++) relabel(player(id)[0x1D]);
        for (size_t m = 0; m < d(dsManagerCount); m++) relabel(club_of(m));
        if (mode & 0x7F) {
            relabel(d(dsCupWinner));
            relabel(d(dsChampion));
            relabel(d(dsEuroWinner));
        }
        uint8_t* ra = team(size_t(a));
        uint8_t* rb = team(size_t(b2));
        if (mode & 0x80) {
            std::swap_ranges(ra, ra + 0x16, rb);
            std::swap(ra[0x16], rb[0x16]);
            std::swap(ra[0x34], rb[0x34]);
            std::swap(ra[0x3B], rb[0x3B]);
            std::swap(ra[0x3C], rb[0x3C]);
            return;
        }
        std::swap_ranges(ra, ra + kTeamRec, rb);
        if (a >= 64 || b2 >= 64) return;
        auto swap_ds = [&](size_t base, size_t size) {
            std::swap_ranges(ds.begin() + long(base + size_t(a) * size), ds.begin() + long(base + size_t(a) * size + size),
                             ds.begin() + long(base + size_t(b2) * size));
        };
        swap_ds(dsRows, 0x50);
        swap_ds(dsSeries, 0x15);
        swap_ds(dsRecords, 0x10);
        swap_ds(dsRecordOpp, 8);
        for (size_t i = 0; i < 64 * 8; i++) relabel(d(dsRecordOpp + i));  // ovl06_11ed
        for (size_t m = 0; m < d(dsManagerCount); m++)
            std::swap_ranges(ds.begin() + long(dsHeadToHead + m * 0x280 + size_t(a) * 10),
                             ds.begin() + long(dsHeadToHead + m * 0x280 + size_t(a) * 10 + 10),
                             ds.begin() + long(dsHeadToHead + m * 0x280 + size_t(b2) * 10));
    }

    // ---- ovl06_099d (ovr030:099D): the league shuffle ---------------------------------------
    void shuffle_leagues() {
        const int first = int(int8_t(d(dsFirstNation)));
        for (int i = 0; i < 20; i++) {
            const int a = g(first, first + 11) & 0xFF, b2 = g(first, first + 11) & 0xFF;
            std::swap_ranges(ds.begin() + long(dsNationValue + size_t(a - first) * 4),
                             ds.begin() + long(dsNationValue + size_t(a - first) * 4 + 4),
                             ds.begin() + long(dsNationValue + size_t(b2 - first) * 4));
            if (b2 == d(dsGermany)) d(dsGermany) = uint8_t(a);
            else if (a == d(dsGermany)) d(dsGermany) = uint8_t(b2);
            swap_teams(a, b2, 0);
        }
        for (size_t l = 0; l < 3; l++) {
            const int lo = d(dsLeagueStart + l), hi = (lo + d(dsLeagueSize + l) - 1) & 0xFF;
            const size_t size = d(dsLeagueSize + l);
            for (int i = 0; i < 55; i++) {
                const int a = g(lo, hi) & 0xFF, b2 = g(lo, hi) & 0xFF;
                swap_teams(a, b2, 1);
                uint8_t* list = &d(dsLeagueLists + l * 12);
                for (size_t x = 0; x < size; x++) {
                    if (list[x] != a) continue;
                    for (size_t y = 0; y < size; y++)
                        if (list[y] == b2) {
                            std::swap(list[x], list[y]);
                            x = size;
                            break;
                        }
                }
            }
        }
    }

    // ---- ovl05_51a5 (ovr029:51A5): European cup entrants ----------------------------------------
    void euro_entrants() {
        d(dsEntrantCount) = 0;
        std::fill_n(ds.begin() + long(dsEntrantSlots), 18, uint8_t(0x80));
        size_t slot = 0;
        d(dsEntrantSlots + slot++) = d(dsChampion);
        d(dsEntrants + d(dsEntrantCount)++) = d(dsChampion);
        const uint8_t winner = d(dsEuroWinner);
        if (winner < 64) {  // ovl05_5362: not yet in the list
            bool listed = false;
            for (size_t i = 0; i < d(dsEntrantCount); i++) listed |= d(dsEntrants + i) == winner;
            if (!listed) {
                d(dsEntrantSlots + slot) = winner;
                d(dsEntrants + d(dsEntrantCount)++) = winner;
            }
        }
    }

    // ---- ovl16_0214: the database entry of a team by its name, slot `slot` -------------------
    size_t db_entry(size_t t, int slot) const {
        size_t s = 0x4AE;
        for (int k = 0; k < 64; k++, s += 0x20B)
            if (cstreq(teams, t * kTeamRec, db, s)) break;
        return s + 0x17 + size_t(20 * slot);
    }

    // ---- ovl16_0280: a random name of a computer club (and its club for `target`) --------------
    size_t random_name(int group, int target) {
        for (;;) {
            const int slot = b(d(dsDbSlots + size_t(group)), d(dsDbSlots + size_t(group) + 1) - 1);
            int top = int(int8_t(uint8_t(d(dsLeagueSize) - 1)));
            if (w(dsNameRetry) != 0) top += 0x18;
            int club;
            for (;;) {
                club = b(0, top) & 0xFF;
                bool human = false;
                for (size_t m = 0; m < d(dsManagerCount); m++) human |= club_of(m) == club;
                if (!human) break;
            }
            const size_t entry = db_entry(size_t(club), slot);
            Bytes s1(20, 0);
            for (size_t i = 0; i < 20; i++) s1[i] = entry + i < db.size() ? db[entry + i] : 0;
            s1[0x12] = 0;
            if (!s1[0]) continue;
            bool duplicate = false;
            for (size_t id = 1; id < size_t(kPlayerIds) && !duplicate; id++)
                if (cstreq(s1, 0, players, id * kPlayerRec)) {
                    put16(ds, dsNameRetry, 1);
                    duplicate = true;
                }
            if (duplicate) continue;
            if (target != 0xFF) player(size_t(target))[0x1D] = uint8_t(club);
            put16(ds, dsNameRetry, 0);
            return entry;
        }
    }

    // ---- ovl09_2cd1 (ovr033:2CD1) with (player, 2, 0, 15000, 0x1D): sign a player ----------------
    int sign_player(int id, int years, int mode, int last) {
        const size_t m = d(dsCurrentManager);
        uint8_t* p = player(size_t(id));
        auto pclass = [&](int x) { return std::min(2, player(size_t(x))[0x18] / 25); };  // 1238:0000
        const int cls = pclass(id);
        int at = 0;
        while (squad(m, size_t(at))[0x0F] && pclass(squad(m, size_t(at))[0x0F]) <= cls && at < last) at++;
        uint8_t* r = squad(m, size_t(at));
        if (r[0x0F] && at != last) std::memmove(r + kSquadRec, r, size_t(last - at) * kSquadRec);
        std::memset(r, 0, kSquadRec);
        r[0x0F] = uint8_t(id);
        r[0x13] = uint8_t(b(80, 120));
        r[0x0E] = 0;
        r[0x0E] = uint8_t(b(35, 65));  // ovl11_307a
        r[0x14] = uint8_t(b(1, 13));
        r[0x14] |= uint8_t(b(1, 3) << 4);
        r[0x14] |= uint8_t(b(0, 1) << 7);
        r[0x0B] = uint8_t(years);
        r[0x0C] = uint8_t(mode);
        r[0x03] = p[0x1B];
        r[0x06] = p[0x1C];
        r[0x10] = p[0x15];
        r[0x11] = p[0x16];
        r[0x12] = p[0x17];
        ContractTables t = contracts;
        t.level = d(dsLevelSalary);
        long salary = player_worth(t, contract_from(r, p), Worth::Salary, brng);  // ovl11_3d36(record, 1)
        if (mode == 99) salary = salary * b(90, 115) / 100;
        store32(r + 0x28, salary);
        if (m != 4 && p[0x1D] != club_of(m)) p[0x1D] = club_of(m);
        return at;
    }

    // ---- ovl16_0730 (ovr040:0730): players and squads -------------------------------------------
    void generate_players() {
        for (size_t m = 0; m < 6; m++)
            for (size_t slot = 0; slot < 30; slot++) squad(m, slot)[0x0F] = 0;
        for (size_t id = 1; id < size_t(kPlayerIds); id++) {
            uint8_t* p = player(id);
            p[0x14] = uint8_t(b(18, 33));
            int v = b(30, 89);
            if (v < 45 && b(0, 1) != 0) v = b(40, 89);
            p[0x16] = uint8_t(v + 10 - b(0, 20));
            p[0x17] = uint8_t(b(45, 55));
            p[0x15] = uint8_t(v + 10 - b(0, 20));
            p[0x19] = 0;
            p[0x1A] = 6;
        }
        const int setting = int(int8_t(d(dsSquadSize)));
        for (d(dsCurrentManager) = 0; d(dsCurrentManager) < d(dsManagerCount); d(dsCurrentManager)++) {
            const size_t m = d(dsCurrentManager);
            d(dsLastManager) = uint8_t(m);
            int slot = 0;
            for (int group = 0; group < 3; group++) {
                for (int n = 0; d(dsGroupSizes + size_t(4 * setting + group)) != n; n++) {
                    const int id = d(dsIdBound + 1 + size_t(group)) - d(dsGroupCounter + size_t(group)) + 1;
                    d(dsGroupCounter + size_t(group))--;
                    const size_t entry = db_entry(club_of(m), int(int8_t(uint8_t(slot))));
                    for (size_t i = 0; i < 20; i++) players.at(size_t(id) * kPlayerRec + i) = entry + i < db.size() ? db[entry + i] : 0;
                    player(size_t(id))[0x11] = 0;
                    if (!(entry < db.size() && db[entry])) cstrcpy(players, size_t(id) * kPlayerRec, db, random_name(group, 0xFF));
                    slot++;
                    const int at = sign_player(id, 2, 0, 0x1D);
                    player(size_t(id))[0x1A] = uint8_t(m);
                    player(size_t(id))[0x14] = uint8_t(b(19, 31));
                    uint8_t* r = squad(m, size_t(at));
                    r[0x19] = uint8_t(b(3, 12) << 4);
                    r[0x19] |= uint8_t(b(3, 12));
                    player(size_t(id))[0x1D] = club_of(m);
                }
            }
        }
        int group = 0;
        for (int id = 1; id < kPlayerIds; id++) {
            if (d(dsIdBound + 1 + size_t(group)) <= id) group++;
            uint8_t* p = player(size_t(id));
            p[0x12] = uint8_t(b(0, 9));
            if (p[0]) continue;
            cstrcpy(players, size_t(id) * kPlayerRec, db, random_name(group, id));
            p[0x11] = 0;
            if (d(dsMode) != 1 && w(dsEndYear) > 2000) p[0x1D] = 0xFF;
        }
        for (int id = 1; id < kPlayerIds; id++) {
            int v = 0;
            if ((id >= 21 && id <= 96) || (id >= 184 && id <= 190)) v = b(0, 16) + 25;
            else if ((id >= 97 && id <= 179) || id >= 191) v = b(0, 16) + 75;
            player(size_t(id))[0x18] = uint8_t(v);
        }
    }

    // ---- 162c:1c5b(10) = seg003:1C5B: club values of the season start ---------------------------
    void rerate_clubs() {
        const int first = int(int8_t(d(dsFirstNation)));
        for (int t = 0; t < first + 12; t++) {
            uint8_t* r = team(size_t(t));
            if (t < first) {
                const int cls = t < d(dsClassLimit1) ? 0 : t < d(dsClassLimit2) ? 1 : t <= first ? 2 : 0;
                const int lo = d(dsRatingBounds + 2 * size_t(cls)), hi = d(dsRatingBounds + 2 * size_t(cls) + 1);
                const int step = g(0, 6);
                const int trend = g(0, 2 * step + 6) - 6;
                for (size_t k = 0; k < 2; k++) r[0x18 + k] = uint8_t(std::clamp(r[0x18 + k] + g(trend - 1, trend + 1), lo, hi));
                for (size_t k = 0; k < 2; k++) {
                    int v = r[0x1A + k];
                    (void)g(0, 2);  // mode 3 adds in any case
                    v += g(trend - 1, trend + 1);
                    r[0x1A + k] = uint8_t(std::clamp(v, lo, hi));
                }
            }
            for (size_t k = 0; k < 3; k++) r[0x1C + k] = uint8_t(g(45, 55));  // the third step is +0x1E
        }
    }
};

size_t state_of(size_t ds_offset) {
    for (const Block& bl : kBlocks)
        if (!bl.heap && ds_offset >= bl.ds && ds_offset < size_t(bl.ds) + bl.size) return bl.state + (ds_offset - bl.ds);
    throw FormatError("world generator: DGROUP offset outside the save");
}

// ovl11_34ef: the transfer list (manager block 4, slots 0-9), see Game::refresh_transfer_list.
void refresh_transfer_list(Bytes& st, ContractTables contracts, BorlandRandom& brng) {
    constexpr size_t kPlayers = 0x4FA5, kTeamsState = 0x17E7, kBlock = 0x696D + 4 * kSquadBlock;
    constexpr int kSlots = kTransferRefreshSlots;
    contracts.level = st.at(0xAEB4);
    auto rec = [&](int slot) { return &st.at(kBlock + size_t(slot) * kSquadRec); };
    auto pl = [&](int id) { return &st.at(kPlayers + size_t(id) * kPlayerRec); };
    auto remove = [&](int slot) {
        std::memmove(rec(slot), rec(slot + 1), size_t(kSlots - slot) * kSquadRec);
        rec(kSlots)[0x0F] = 0;
    };
    auto pclass = [&](int id) {  // 1000:0180
        const int pos = pl(id)[0x18];
        return pos == 0 ? 0 : pos < 60 ? 1 : 2;
    };
    int kept = 0;
    for (int slot = 0; slot < kSlots; slot++) {
        const int p = rec(slot)[0x0F];
        if (!p) continue;
        if (pl(p)[0x1A] == 4) {
            remove(slot);
            pl(p)[0x1A] = 6;
        } else {
            kept++;
        }
    }
    for (int k = 0; k < kSlots - kept; k++) {
        const int s = brng.range(0, kSlots - 1);
        const int old = rec(s)[0x0F];
        if (old && pl(old)[0x1A] != 4) continue;
        if (old) {
            remove(s);
            pl(old)[0x1A] = 6;
        }
        const int p = pick_transfer_candidate(brng, [&](int q) { return pl(q)[0x1A] == 6; });
        if (p < 0) break;
        uint8_t* pr = pl(p);
        pr[0x17] = uint8_t(brng.range(45, 55));
        const int club = pr[0x1D];
        if (club < kTeams && st.at(kTeamsState + size_t(club) * kTeamRec + 0x16) == 0) {  // ovl11_381e
            const uint8_t* tm = &st.at(kTeamsState + size_t(club) * kTeamRec);
            const int lo = club >= 52 && club < 64 ? 10 : 0;
            const int a = std::clamp(tm[0x19] + brng.range(lo, 14) - 10, 10, 99);
            int b2 = tm[0x1B] + brng.range(lo, 14) - 10;
            if (brng.range(0, 4) == 0) b2 += brng.range(12, 20);
            pr[0x15] = uint8_t(a);
            pr[0x16] = uint8_t(std::clamp(b2, 10, 99));
        }
        int at = 0;
        while (at < kSlots && rec(at)[0x0F] && pclass(rec(at)[0x0F]) <= pclass(p)) at++;
        if (rec(kSlots)[0x0F]) continue;
        if (rec(at)[0x0F] && at < kSlots) std::memmove(rec(at + 1), rec(at), size_t(kSlots - at) * kSquadRec);
        uint8_t* r = rec(at);
        std::memset(r, 0, kSquadRec);
        r[0x0F] = uint8_t(p);
        r[0x13] = uint8_t(brng.range(80, 120));
        r[0x0E] = uint8_t(brng.range(35, 65));
        int n = brng.range(1, 13);
        n |= brng.range(1, 3) << 4;
        n |= brng.range(0, 1) << 7;
        r[0x14] = uint8_t(n);
        r[0x03] = pr[0x1B];
        r[0x06] = pr[0x1C];
        r[0x10] = pr[0x15];
        r[0x11] = pr[0x16];
        r[0x12] = pr[0x17];
        pr[0x1A] = 4;
        const long value = player_worth(contracts, contract_from(r, pr), Worth::Value, brng);
        const long salary = player_worth(contracts, contract_from(r, pr), Worth::Salary, brng);
        store32(r + 0x2C, value);
        store32(r + 0x28, salary);
        const int hi = brng.range(3, 12);
        r[0x19] = uint8_t(hi << 4 | brng.range(3, 12));
    }
}

}  // namespace

WorldResult generate_world(const Bytes& exe, const Bytes& db, const WorldSettings& settings, const WorldSeeds& seeds) {
    if (exe.size() < kDgroupFile + kDgroupInit) throw FormatError("world generator: EM.EXE too short");
    if (db.size() != kDbSize) throw FormatError("world generator: KB\\23.vga must be 36648 bytes");
    const auto& managers = settings.managers;
    if (managers.empty() || managers.size() > 4) throw FormatError("world generator: 1 to 4 managers");
    for (size_t i = 0; i < managers.size(); i++) {
        const WorldManager& m = managers[i];
        if (m.name.empty() || m.name.size() > 18) throw FormatError("world generator: manager name of 1 to 18 characters");
        if (m.face < 0 || m.face > 11) throw FormatError("world generator: invalid face");
        if (m.team < 0 || m.team > 47) throw FormatError("world generator: invalid club");
        for (size_t j = 0; j < i; j++)
            if (managers[j].team == m.team) throw FormatError("world generator: two managers with the same club");
    }
    if (settings.level < 0 || settings.level > 5 || settings.end_choice < 0 || settings.end_choice > 3 ||
        settings.squad_size < 0 || settings.squad_size > 2 || settings.start_mode < 0 || settings.start_mode > 3)
        throw FormatError("world generator: invalid start menu setting");

    World w(exe, db);
    WorldResult out;
    auto mark = [&](const char* step) {
        w.log.push_back(std::string(step) + ": borland " + std::to_string(w.brng.state()) + ", game " + std::to_string(w.grng.state()));
    };
    // ---- _main_0 before the start menu
    w.brng = BorlandRandom(seeds.start);  // ovr034:0708
    (void)w.b(0, 2);                      // ovr034:0714: intro or not (the intro itself draws nothing traced)
    w.team_records();                     // ovr034:086B
    mark("team records");
    w.brng = BorlandRandom(seeds.menu);  // ovr034:08A5
    const int warm = w.b(100, 300);      // ovl00_0539
    for (int i = 0; i < warm; i++) (void)w.g(0, 1);
    w.d(dsManagerCount) = 0;
    w.reset_managers();
    w.d(dsLeagueLists) = 2;  // ovr034:08B8..08C2 (byte_2AC9B, byte_2AC9D, byte_320B6)
    w.d(dsLeagueLists + 2) = 0;
    w.d(dsChampion) = 2;
    w.shuffle_leagues();
    w.euro_entrants();
    for (size_t gr = 0; gr < 3; gr++) std::fill_n(w.ds.begin() + long(0xD014 + gr * 32), 32, uint8_t(0));  // ovl05_5a4d
    std::fill_n(w.ds.begin() + long(0x5C4B), 0x80, uint8_t(0));
    (void)w.b(0, 0x8FFF);  // shifted out by "shl ax, 16" (ovr034:08E4)
    out.session_id = uint32_t(w.b(0, 0x8FFF) + 1);
    put32(w.ds, dsSession, out.session_id);
    mark("before start menu");

    // ---- start menu ovr027:15A5 .. 186B
    w.d(dsManagerCount) = 0;
    for (const WorldManager& m : managers) {
        const size_t i = w.d(dsManagerCount);
        std::fill_n(w.ds.begin() + long(w.manager(i)), 0x13, uint8_t(0));
        for (size_t k = 0; k < m.name.size(); k++) w.d(w.manager(i) + k) = uint8_t(m.name[k]);
        w.d(w.manager(i) + 0x13) = uint8_t(m.face);  // set by the face choice of the menu
        w.club_of(i) = uint8_t(m.team);
        w.team(size_t(m.team))[0x16] = uint8_t(i + 1);
        w.d(dsFaces + i) = uint8_t(m.face);
        w.d(dsManagerCount)++;
    }
    w.d(dsLevel) = uint8_t(settings.level);
    w.d(dsEndChoice) = uint8_t(settings.end_choice);
    put16(w.ds, dsSquadSize, uint16_t(settings.squad_size));
    const int mode = settings.start_mode;
    w.d(dsMode) = uint8_t((mode == 2 ? 1 : 0) + (mode == 1 ? 2 : 0));
    if (mode == 3) w.d(dsMode) = 4;
    put16(w.ds, dsEndYear, uint16_t(settings.end_choice == 3 ? 0x56CE : 0x7CA + 2 * settings.end_choice));
    w.d(dsStartFlag) = 1;
    put16(w.ds, dsSeasonYear, 0x7C9);
    w.d(dsLevelSalary) = uint8_t(5 - settings.level);
    w.generate_players();
    mark("ovl16_0730");
    w.d(dsWmRound) = 0;  // ovl05_194b(0); its ovl05_0ef7(0) is not ported
    w.d(dsWmPhase) = 0;
    w.d(dsWmFlag) = 1;
    w.d(dsCompetitionRound) = 0;
    w.d(dsEuroRound) = 0;  // ovl05_1e6f(0); its ovl05_0ef7(2) is not ported
    w.d(dsCompetitionRound) = 0;
    w.d(dsCompetition) = 0xFF;
    // ovr027:1B27..1DE8: after ovl16_0730 a new game (var_64 = old manager count = 0) runs the loop for added managers
    // over every manager (Amiga FUN_001075e8 line 6309). Ported: the youth teams DS:A075 + 0x15 * m (quality
    // 0x34 + 14 * (2 - DS:011A), grant 5, DS:011A), the balance +0x18F = DS:0ED8 * 150000 + 1200000, the reporting mode 2
    // of the club (sub_1902B(club, 1, 2) = Amiga FUN_001072a8: the fourth club in mode 2 demotes the first computer club
    // of mode 2 to mode 1) and +0x18B = 99999. Not ported: the Borland draw of a club of the start league with the swap
    // ovl06_0c29(club, drawn, 0x80) (ovr027:1C0B..1CC7, when DS:011A = 0, the club is in another league or beyond DS:07CB)
    // and the one-manager branch from ovr027:1CCA; both use the Borland stream that ovr034:0929 re-seeds afterwards.
    for (size_t m = 0; m < managers.size(); m++) {
        const int setting = w.d(dsSquadSize);
        for (size_t i = 0; i < 3; i++) {
            w.d(dsYouth + 0x15 * m + i) = uint8_t(0x34 + 14 * (2 - setting));
            w.d(dsYouth + 0x15 * m + i + 3) = 5;
            w.d(dsYouth + 0x15 * m + i + 6) = uint8_t(setting);
        }
        put32(w.ds, w.manager(m) + 0x18F, uint32_t(int32_t(w.d(dsLevelSalary)) * 150000 + 1200000));
        const size_t club = w.club_of(m);
        const size_t lo = club >= 52 && club < 64 ? 52 : 0, hi = club >= 52 && club < 64 ? 64 : 150;
        int count = 0;
        for (size_t t = lo; t < hi; t++) {
            int next = count;
            if ((w.team(t)[0x34] >> 6) == 2 && ++next == 4)
                for (size_t u = lo; u < t; u++)
                    if ((w.team(u)[0x34] >> 6) == 2 && w.team(u)[0x16] == 0) {
                        w.team(u)[0x34] = uint8_t((w.team(u)[0x34] & 0x3F) + 0x40);
                        next = count;
                        break;
                    }
            count = next;
        }
        w.team(club)[0x34] = uint8_t(0x80 + (w.team(club)[0x34] & 0x3F));
        put32(w.ds, w.manager(m) + 0x18B, 99999);
    }

    // ---- _main_0 after the start menu
    w.rerate_clubs();  // ovr034:0923, sub_17F1B(10)
    mark("162c:1c5b");
    out.game_random = w.grng.state();
    w.brng = BorlandRandom(seeds.after);  // ovr034:0929
    w.d(dsMatchday) = 1;                  // ovr034:092E

    Bytes st(kStateSize, 0);
    for (const Block& bl : kBlocks) {
        const Bytes& src = bl.heap == 1 ? w.teams : bl.heap == 2 ? w.players : w.ds;
        const size_t from = bl.heap ? 0 : bl.ds;
        std::copy_n(src.begin() + long(from), bl.size, st.begin() + long(bl.state));
    }

    // 1238:1663: skills, random club changes, quotas, then the clubs of the free players
    const ContractRandomDraw draw = [&](int lo, int hi) { return w.brng.range(lo, hi); };
    std::array<uint8_t, 3> cursors{w.d(dsDistributionCursor), w.d(dsDistributionCursor + 1), w.d(dsDistributionCursor + 2)};
    annual_global_skills(st, draw);
    annual_random_club_changes(st, draw);
    auto quota = annual_distribution_quotas(st);
    for (size_t id = 1; id < 0xB3; id++) {
        const size_t p = 0x4FA5 + id * kPlayerRec;
        const int club = st.at(p + 0x1D);
        const bool free_club = club == 0xFF || st.at(0x17E7 + size_t(club) * kTeamRec + 0x16) == 0;
        if (!free_club || (club != 0xFF && le32(st, state_of(dsYear)) == 0)) continue;
        for (int gr = 0; gr < 3; gr++)
            if (quota[size_t(gr)]) {
                st.at(p + 0x1D) = uint8_t(annual_cyclic_target(st, gr, cursors, draw));
                quota[size_t(gr)]--;
            }
    }
    mark("1238:1663");
    // ovl05_4c7b(0, 0): the first DEB cup round
    st.at(state_of(0xD0E4)) = 1;
    st.at(state_of(dsCupRound)) = 0;
    st.at(state_of(0xD074)) = 0;
    std::fill_n(st.begin() + long(state_of(0xD075)), 32, uint8_t(0));
    std::vector<int> humans;
    for (size_t m = 0; m < managers.size(); m++) humans.push_back(managers[m].team);
    const auto cup = draw_initial_cup(humans, w.brng);
    if (cup.size() * 2 < 32) st.at(state_of(dsCupDraw) + cup.size() * 2) = 0xFF;
    for (size_t i = 0; i < cup.size(); i++) {
        st.at(state_of(dsCupDraw) + 2 * i) = uint8_t(cup[i].home);
        st.at(state_of(dsCupDraw) + 2 * i + 1) = uint8_t(cup[i].away);
    }
    st.at(state_of(dsCupRound)) = 1;
    mark("ovl05_4c7b");
    put32(st, state_of(dsDayCounter), uint32_t(int32_t(int8_t(w.d(dsDayStart)))));  // ovr034:0943
    {
        const uint8_t current = st.at(state_of(dsCurrentManager));
        st.at(state_of(dsCurrentManager)) = 4;
        refresh_transfer_list(st, w.contracts, w.brng);  // ovl11_34ef
        st.at(state_of(dsCurrentManager)) = current;
    }
    mark("ovl11_34ef");
    for (size_t c = 0; c < 4; c++)
        put16(st, 0x00B0 + c * 0x48 + 0x46, uint16_t(le32(st, 0x01D0 + 4 * c) / 50));  // DS:D13A + c * 0x48
    for (int day = 0; day < 90; day++) market_day(st, w.brng);  // 90 x ovl39_03ca
    mark("market");
    // date of the first day (season day 1, Sunday 10.09.1993), as in the shipped start state
    put32(st, state_of(dsDay), 10);
    put32(st, state_of(dsMonth), 8);
    put32(st, state_of(dsYear), 1993);
    put32(st, state_of(dsDayCounter), 1);

    // header (ovl16_11bd): tag, year, managers, matchday, previews, "EH0"
    HokFile h;
    h.header.assign(kHokHeaderSize, 0);
    const char tag[] = "KRON_0.99";
    std::copy(tag, tag + sizeof tag, h.header.begin());
    put32(h.header, 0x0A, 1993);
    h.header[0x0E] = uint8_t(managers.size());
    h.header[0x0F] = st.at(0x8F80);
    for (size_t m = 0; m < managers.size(); m++) {
        const size_t o = 0x10 + 40 * m;
        h.header[o] = uint8_t(managers[m].team / 12);
        std::copy(managers[m].name.begin(), managers[m].name.end(), h.header.begin() + long(o + 2));
        const size_t club = 0x17E7 + size_t(managers[m].team) * kTeamRec;
        for (size_t k = 0; k < 18 && st.at(club + k); k++) h.header[o + 21 + k] = st.at(club + k);
    }
    std::copy_n("EH0", 4, h.header.begin() + 0xB0);
    h.key0 = 0;
    h.state = std::move(st);
    out.save = decode_hok(encode_hok(h));
    out.distribution_cursors = cursors;
    out.borland = w.brng.state();
    out.log = std::move(w.log);
    return out;
}

}  // namespace em
