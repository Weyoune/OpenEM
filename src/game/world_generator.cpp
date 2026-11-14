// SPDX-License-Identifier: GPL-3.0-only
// The new-game world generator and Manager -> Neuer (see world_generator.hpp).
//
// The generator works like the original program: on an image of the program's data segment,
// whose initialised part is copied from EM.EXE, plus two separate buffers for the 150 team
// records (0x3D bytes) and the 200 player records (0x21 bytes). The `ds*` constants are offsets
// into that image (for initialised data, EM.EXE file offset kDgroupFile + offset). At the end the
// image is mapped to the HOK save state through kBlocks, the block table of the save format.
#include "game/world_generator.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "formats/db.hpp"
#include "game/annual_distribution.hpp"
#include "game/annual_players.hpp"
#include "game/competitions.hpp"
#include "game/contracts.hpp"
#include "game/market.hpp"
#include "game/match.hpp"
#include "game/scorers.hpp"
#include "game/lineup.hpp"
#include "game/redistribution.hpp"
#include "game/rules.hpp"
#include "game/schedule.hpp"
#include "game/sponsors.hpp"
#include "game/team_refresh.hpp"
#include "game/transfers.hpp"

namespace em {
namespace {

constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;  // EM.EXE file offset of the data segment (header + segment start)
constexpr size_t kDgroupInit = 0xE7E0;            // size of its initialised part (the rest starts zeroed)
// Record sizes: team, player, manager block, squad block (30 rows), squad row.
constexpr size_t kTeamRec = 0x3D, kPlayerRec = 0x21, kManagerRec = 0x1AF, kSquadBlock = 0x618, kSquadRec = 0x34;
constexpr int kTeams = 150, kPlayerIds = 200;  // team records; player IDs 0..199 (0 unused)

// Offsets into the data image. The start options:
constexpr size_t dsLevel = 0x0118, dsEndChoice = 0x0119, dsSquadSize = 0x011A;  // level, end, start league (+1 strength)
constexpr size_t dsRatingBounds = 0x0125;  // u8[3][2] {low, high} club rating bounds per class
constexpr size_t dsGroupCounter = 0x015C;  // u8[3] free IDs per position group (counted down)
constexpr size_t dsDbSlots = 0x0164;       // u8[4] database slot bounds per group: 0, 2, 12, 25
constexpr size_t dsGroupSizes = 0x0169;    // u8[4][4] squad size per group and setting
constexpr size_t dsFaces = 0x0178;         // u8[4] face per manager
constexpr size_t dsShareTarget = 0x01FD;  // u32[4] share target prices (initial price = target / 50)
constexpr size_t dsNationValue = 0x026C;  // u32[12] friendly price per nation
constexpr size_t dsSession = 0x02C4;      // u32 session id of the new game
constexpr size_t dsIdBound = 0x0381;      // u8[4] position groups: IDs 1..20, 21..96, 97..199
constexpr size_t dsManagerCount = 0x0386, dsCupWinner = 0x0387, dsEuroWinner = 0x0388;  // u8 each
constexpr size_t dsDayCounter = 0x03B7, dsSeasonYear = 0x03BB;  // u32 day counter, u16 season year
constexpr size_t dsGermany = 0x06AC, dsFirstNation = 0x06AD, dsDayStart = 0x06AF;  // nation of the coach, first nation index, start day
constexpr size_t dsMatchday = 0x07A9, dsWmRound = 0x07AE, dsEuroRound = 0x07AF;  // matchday per league, WM / EuroCup round
constexpr size_t dsLeagueSize = 0x07B1, dsLeagueStart = 0x07C0;  // u8[3] teams per league, first team index per league
constexpr size_t dsClassLimit1 = 0x07C1, dsClassLimit2 = 0x07C2;  // first team of the second / third rating class
constexpr size_t dsLevelSalary = 0x0ED8;  // game level for salaries (5 - level option)
constexpr size_t dsYouth = 0xA075;  // youth teams, 0x15 bytes per manager (state 0x06B8)
constexpr size_t dsDistributionCursor = 0x48FB;  // u8[3] player distribution cursors (not saved)
constexpr size_t dsNationSkills = 0x4C4E, dsFormString = 0x4DC7;  // fixed national team skills; "--------"
constexpr size_t dsNameRetry = 0x55EE;  // u16 flag: after a duplicate name the club range widens by 24
constexpr size_t dsEndYear = 0x5A14, dsYear = 0x5A18, dsMonth = 0x5A1C, dsDay = 0x5A20;  // end year, date
constexpr size_t dsLeagueLists = 0x5CCB, dsCupRound = 0x5CF3, dsCupDraw = 0x5CFB;  // u8[3][12] league team lists, cup
constexpr size_t dsSquads = 0x5DC3;  // 6 squad blocks (state 0x696D)
constexpr size_t dsRows = 0x8257;  // 64 x 0x50: titles, table row, curve
constexpr size_t dsManagers = 0x965B;  // 6 manager blocks (state 0x0DCD)
constexpr size_t dsWmFlag = 0x9FAC;
constexpr size_t dsCurrentManager = 0xA70E, dsLastManager = 0xA70D;  // manager being processed / last one
constexpr size_t dsEntrantSlots = 0xA763, dsEntrantCount = 0xA775, dsEntrants = 0xA776;  // European cup entrants
constexpr size_t dsStartFlag = 0xA899;
constexpr size_t dsHeadToHead = 0xAB7B, dsSeries = 0xB57B, dsRecordOpp = 0xBB0F, dsRecords = 0xBD0F;  // result books
constexpr size_t dsCompetition = 0xCFFA, dsCompetitionRound = 0xCFFB;
constexpr size_t dsMode = 0xD0B9, dsWmPhase = 0xD0C1, dsChampion = 0xD0E6;  // game mode, friendlies/WM phase, champion

// One block of the save format: `size` bytes of the data image at `ds` (or of a record buffer,
// heap 1 = teams, 2 = players) are stored at HOK state offset `state`.
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
constexpr size_t kStateSize = 0xBA91;  // size of the HOK save state

// Little-endian stores.
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

// The contract view (value/salary inputs) of squad row `r` and player record `p`.
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
// Stores a little-endian i32 at `r`.
void store32(uint8_t* r, long v) {
    for (int k = 0; k < 4; k++) r[k] = uint8_t(uint32_t(v) >> (8 * k));
}

// The world under construction: the data image, the team and player record buffers, the
// master database and both random streams (Borland and the game's own generator).
struct World {
    Bytes ds = Bytes(0x10000, 0);                                // the 64 KiB data image
    Bytes teams = Bytes(size_t(kTeams) * kTeamRec, 0);          // team records
    Bytes players = Bytes(size_t(kPlayerIds) * kPlayerRec, 0);  // player records, index = ID
    const Bytes& db;                                             // KB\23.vga
    ContractTables contracts;
    GameRules rules;  // version 1.00 or 1.30 of EM.EXE
    BorlandRandom brng;
    GameRandom grng{0};  // the game's generator starts at 0 with the program and is never seeded
    std::vector<std::string> log;  // step markers and draw counts

    World(const Bytes& exe, const Bytes& d) : db(d), contracts(load_contract_tables(exe)), rules(detect_game_rules(exe)) {
        std::copy_n(exe.begin() + long(kDgroupFile), kDgroupInit, ds.begin());
    }
    uint8_t& d(size_t o) { return ds.at(o); }            // byte of the data image
    uint16_t w(size_t o) const { return le16(ds, o); }   // u16 of the data image
    uint8_t* team(size_t t) { return &teams.at(t * kTeamRec); }
    uint8_t* player(size_t id) { return &players.at(id * kPlayerRec); }
    size_t manager(size_t m) const { return dsManagers + m * kManagerRec; }  // manager block m
    uint8_t& club_of(size_t m) { return d(manager(m) + 0x14); }  // manager block +0x14: club team index
    uint8_t* squad(size_t m, size_t slot) { return &ds.at(dsSquads + m * kSquadBlock + slot * kSquadRec); }
    int b(int lo, int hi) { return brng.range(lo, hi); }               // Borland rand(lo, hi)
    int g(long lo, long hi) { return int(grng.range(lo, hi)); }        // game generator rand(lo, hi)

    // ---- team records from KB\23.vga ---------------------------------------------------------
    // For all 150 teams: +0x3B/+0x3C a signed word from the database's word table (file offset
    // 2 * t), +0x17 rand(47, 53), skills +0x18/+0x19 and +0x1A/+0x1B rolled around the database
    // value a (file 0x16C + 3t) as a - rand(0, a/10) + rand(0, a/10), form +0x1C/+0x1D rand(45, 55)
    // (minus 5 beyond team 64), the national teams fixed skills from the EXE table minus 2; the
    // 64 league/EuroCup teams also get +0x34 and the empty form string; then the names and the
    // initial league team lists.
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
                if (int(t) >= first && int(t) < first + 12) {  // the national teams take fixed values from dsNationSkills
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

    // ---- manager reset: clears the head-to-head books, sets the cup phase bytes +0xE8..+0xEA of
    // all six managers to 30, the faces 1..4 and 0xFF history entries of the four human blocks.
    void reset_managers() {
        std::fill_n(ds.begin() + long(dsHeadToHead), 0x1594, uint8_t(0));
        for (size_t m = 0; m < 6; m++)
            for (size_t c = 2; c < 5; c++) d(manager(m) + 0xE6 + c) = 0x1E;
        for (size_t m = 0; m < 4; m++) {
            d(manager(m) + 0x13) = uint8_t(m + 1);
            std::fill_n(ds.begin() + long(dsHeadToHead + m * 0x280), 0x280, uint8_t(0xFF));
        }
    }

    // ---- two teams change places -------------------------------------------------------------
    // All references to a / b2 (player teams, managers' clubs; with mode & 0x7F also the cup,
    // league and EuroCup winners) are swapped. mode & 0x80 swaps only name, +0x16, +0x34 and
    // +0x3B/+0x3C of the records; otherwise the whole records and, for league teams (< 64), also
    // their table rows, series, records and head-to-head entries.
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
        for (size_t i = 0; i < 64 * 8; i++) relabel(d(dsRecordOpp + i));  // opponents stored in the records
        for (size_t m = 0; m < d(dsManagerCount); m++)
            std::swap_ranges(ds.begin() + long(dsHeadToHead + m * 0x280 + size_t(a) * 10),
                             ds.begin() + long(dsHeadToHead + m * 0x280 + size_t(a) * 10 + 10),
                             ds.begin() + long(dsHeadToHead + m * 0x280 + size_t(b2) * 10));
    }

    // ---- the league shuffle (game generator) -------------------------------------------------
    // 20 random swaps among the 12 nations (with their friendly prices and the coach's nation),
    // then 55 random swaps inside each of the three leagues, also swapping them in the league lists.
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

    // ---- European cup entrants: the champion, then the EuroCup winner if it is a league team
    // and not listed yet; the other slots stay 0x80 (empty).
    void euro_entrants() {
        d(dsEntrantCount) = 0;
        std::fill_n(ds.begin() + long(dsEntrantSlots), 18, uint8_t(0x80));
        size_t slot = 0;
        d(dsEntrantSlots + slot++) = d(dsChampion);
        d(dsEntrants + d(dsEntrantCount)++) = d(dsChampion);
        const uint8_t winner = d(dsEuroWinner);
        if (winner < 64) {  // only when not yet in the list
            bool listed = false;
            for (size_t i = 0; i < d(dsEntrantCount); i++) listed |= d(dsEntrants + i) == winner;
            if (!listed) {
                d(dsEntrantSlots + slot) = winner;
                d(dsEntrants + d(dsEntrantCount)++) = winner;
            }
        }
    }

    // ---- the database offset of player name `slot` of the club named like team `t` ------------
    // The database holds 64 clubs of 0x20B bytes from file offset 0x4AE: a 23-byte name, then
    // 20-byte player names. A team without a matching club yields the entry past the last club.
    size_t db_entry(size_t t, int slot) const {
        size_t s = 0x4AE;
        for (int k = 0; k < 64; k++, s += 0x20B)
            if (cstreq(teams, t * kTeamRec, db, s)) break;
        return s + 0x17 + size_t(20 * slot);
    }

    // ---- a random player name from a computer club ---------------------------------------------
    // Draws a database slot of position group `group` and a first-division club no human manager
    // runs (after a duplicate name the range widens by 24 clubs) until the name is non-empty and
    // not used by any player yet. With `target` != 0xFF that player's team becomes the club.
    // Returns the database offset of the name.
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

    // ---- sign player `id` into the squad of the current manager ------------------------------
    // The row goes after the players of the same or a lower position class (position / 25, at
    // most 2), at most at `last`; later rows shift down. The new row gets exhaustion rand(80,
    // 120), experience rand(35, 65), the packed trend/direction/high bit, `years`, `mode` (+0x0C),
    // goals, games, skills and form from the player record and the valued salary (mode 99:
    // times rand(90, 115) %). The player then belongs to the manager's club (not for the pool,
    // block 4). Returns the slot.
    int sign_player(int id, int years, int mode, int last) {
        const size_t m = d(dsCurrentManager);
        uint8_t* p = player(size_t(id));
        auto pclass = [&](int x) { return std::min(2, player(size_t(x))[0x18] / 25); };  // position class
        const int cls = pclass(id);
        int at = 0;
        while (squad(m, size_t(at))[0x0F] && pclass(squad(m, size_t(at))[0x0F]) <= cls && at < last) at++;
        uint8_t* r = squad(m, size_t(at));
        if (r[0x0F] && at != last) std::memmove(r + kSquadRec, r, size_t(last - at) * kSquadRec);
        std::memset(r, 0, kSquadRec);
        r[0x0F] = uint8_t(id);
        r[0x13] = uint8_t(b(80, 120));
        r[0x0E] = 0;
        r[0x0E] = uint8_t(b(35, 65));  // experience
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
        long salary = player_worth(t, contract_from(r, p), Worth::Salary, brng);  // the salary valuation
        if (mode == 99) salary = salary * b(90, 115) / 100;
        store32(r + 0x28, salary);
        if (m != 4 && p[0x1D] != club_of(m)) p[0x1D] = club_of(m);
        return at;
    }

    // ---- players and squads (Borland) -------------------------------------------------------
    // Rolls all 199 player records (age 18..33, skills around rand(30, 89) +-10 with a second
    // chance below 45, form 45..55, owner 6 = nobody), then fills the squads of the human
    // managers group by group (goalkeepers, defence, attack; sizes from dsGroupSizes) with players
    // named after their club's database entries (or random names), signed for two years, aged
    // 19..31, with the +0x19 nibbles. The remaining players get random names, a random +0x12 and
    // (outside "Nur WM", when the game ends after 2000) no team; finally every player gets the
    // position value of his ID range.
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

    // ---- club values of the season start (game generator) -----------------------------------
    // Every club before the nations gets a trend rand(0, 2s + 6) - 6 with s = rand(0, 6) and
    // moves both skill pairs by trend +-1, clamped to the bounds of its rating class; all teams up
    // to the last nation get fresh form values rand(45, 55) (see rerate_team in season_end.hpp).
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
            // version 1.00 runs the loop over three bytes: the third step overwrites the energy of the first
            // defence line (+0x1E); version 1.30 re-rolls the two form values only
            for (size_t k = 0; k < (rules.v130() ? 2u : 3u); k++) r[0x1C + k] = uint8_t(g(45, 55));
        }
    }
};

// The HOK state offset of a data-image offset (throws when that byte is not saved).
size_t state_of(size_t ds_offset) {
    for (const Block& bl : kBlocks)
        if (!bl.heap && ds_offset >= bl.ds && ds_offset < size_t(bl.ds) + bl.size) return bl.state + (ds_offset - bl.ds);
    throw FormatError("world generator: DGROUP offset outside the save");
}

// The saved blocks of the world as a HOK state, and back.
Bytes world_state(const World& w) {
    Bytes st(kStateSize, 0);
    for (const Block& bl : kBlocks) {
        const Bytes& src = bl.heap == 1 ? w.teams : bl.heap == 2 ? w.players : w.ds;
        const size_t from = bl.heap ? 0 : bl.ds;
        std::copy_n(src.begin() + long(from), bl.size, st.begin() + long(bl.state));
    }
    return st;
}
void load_world_state(World& w, const Bytes& st) {
    for (const Block& bl : kBlocks) {
        Bytes& dst = bl.heap == 1 ? w.teams : bl.heap == 2 ? w.players : w.ds;
        const size_t to = bl.heap ? 0 : bl.ds;
        std::copy_n(st.begin() + long(bl.state), bl.size, dst.begin() + long(to));
    }
}

// A HOK file around a state, with the header the game writes: tag "KRON_0.99", +0x0A u32 year
// (1993 while the state still holds 0), +0x0E manager count, +0x0F first-division matchday, per
// manager at +0x10 + 40m the league (club / 12), name and club name of the load preview, and
// "EH0" at +0xB0. The managers are those of the state (count at 0x0DC6, names in the manager blocks).
HokFile make_hok(Bytes st) {
    HokFile h;
    h.header.assign(kHokHeaderSize, 0);
    const char tag[] = "KRON_0.99";
    std::copy(tag, tag + sizeof tag, h.header.begin());
    const uint32_t year = le32(st, 0x8E29);  // the year of the date, 0 during the generator
    put32(h.header, 0x0A, year ? year : 1993);
    const size_t managers = std::min<size_t>(st.at(0x0DC6), 4);
    h.header[0x0E] = uint8_t(managers);
    h.header[0x0F] = st.at(0x8F80);
    for (size_t m = 0; m < managers; m++) {
        const size_t o = 0x10 + 40 * m, block = 0x0DCD + m * kManagerRec;
        const size_t club = 0x17E7 + size_t(st.at(block + 0x14)) * kTeamRec;
        h.header[o] = uint8_t(st.at(block + 0x14) / 12);
        for (size_t k = 0; k < 19 && st.at(block + k); k++) h.header[o + 2 + k] = st.at(block + k);
        for (size_t k = 0; k < 18 && st.at(club + k); k++) h.header[o + 21 + k] = st.at(club + k);
    }
    std::copy_n("EH0", 4, h.header.begin() + 0xB0);
    h.key0 = 0;
    h.state = std::move(st);
    return decode_hok(encode_hok(h));
}

// The transfer list refresh (squad block 4, slots 0-9), as Game::refresh_transfer_list does it
// in the day loop: listed players the pool owns (+0x1A = 4) leave, then free slots are refilled
// with random unowned players (+0x1A = 6) with fresh form, skills re-rolled from their club's
// values, a new squad row in position order, value and salary. All draws use `brng`.
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
    auto pclass = [&](int id) {  // position class: goalkeeper 0, defence (< 60) 1, attack 2
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
        if (club < kTeams && st.at(kTeamsState + size_t(club) * kTeamRec + 0x16) == 0) {  // computer club: skills from its values
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

// One pass of the start menu's manager loop for manager m (see generate_world). `new_game`: the
// game had no managers before (the one-manager branch for an end in 1994 needs it).
static void manager_loop_step(World& w, size_t m, bool new_game, const SponsorTables& sponsor_tables) {
    {  // the default training plan, data image 0xA8A1 + 0x1C * m (7 days x 4 units)
        uint8_t* plan = &w.d(0xA8A1 + 0x1C * m);
        if (w.d(dsLevelSalary) == 0) std::fill_n(plan, 0x1C, uint8_t(1));
        else if (std::all_of(plan, plan + 0x1C, [](uint8_t v) { return v == 0; })) {
            int unit = 2, spare = 2;
            for (int day = 0; day < 7; day++)
                for (int part = 0; part < 4; part++) {
                    uint8_t& entry = plan[day * 4 + part];
                    entry = uint8_t(unit++);
                    if (unit == 3 && spare > 0) {
                        spare--;
                        unit++;
                    }
                    if (unit == 9) unit = 2;
                    for (size_t k = 0; k < 16; k += 2)
                        if (w.d(0x48EB + k) == day && w.d(0x48EC + k) == part) entry = 1;
                }
        }
    }
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
    if (std::getenv("EM_OLD_WORLDLOOP")) return;

    // the club draw
    auto in_euro = [&](int t) {  // one of the eight EuroCup group teams (data image 0x5D1B)
        for (size_t i = 0; i < 8; i++)
            if (w.d(0x5D1B + i) == t) return true;
        return false;
    };
    auto taken = [&](int t) {
        for (size_t j = 0; j < m; j++)
            if (w.club_of(j) == t) return true;
        return false;
    };
    auto team_class = [&](int t) {  // the league class of a team (0, 1, 2)
        if (t < w.d(dsClassLimit1)) return 0;
        if (t < w.d(dsClassLimit2)) return 1;
        if (t <= w.d(dsFirstNation)) return 2;
        return 0;
    };
    const int v = setting;
    if (v == 0 || team_class(w.club_of(m)) != v || w.club_of(m) > w.d(0x07CB)) {
        const int lo = w.d(dsLeagueStart + size_t(v)), hi = lo + w.d(dsLeagueSize + size_t(v)) - 1;
        int drawn = 0;
        do drawn = w.b(lo, hi);
        while (taken(drawn) || in_euro(drawn));
        w.swap_teams(w.club_of(m), drawn, 0x80);
    }
    if (new_game && w.w(dsEndYear) == 0x7CA && w.d(dsManagerCount) == 1) {  // one manager, one season: a EuroCup club
        w.d(w.manager(m) + 0xE9) = 1;
        const int lo = w.d(dsLeagueStart), hi = lo + w.d(dsLeagueSize) - 1;
        int drawn = 0;
        do drawn = w.b(lo, hi);
        while (taken(drawn) || !in_euro(drawn));
        w.swap_teams(w.club_of(m), drawn, 0x80);
    }
    // the strengths of the new squad: skills base +-2 from the start-strength table (data image
    // 0x4B42 + 3 * start league + strength), form 40..60
    const int base = w.d(0x4B42 + 3 * size_t(v) + w.d(dsSquadSize + 1));
    int occupied = 0;
    for (size_t si = 0; si < 0x1D; si++) occupied += w.squad(m, si)[0x0F] != 0;
    for (size_t si = 0; si < size_t(occupied); si++) {
        uint8_t* rec = w.squad(m, si);
        uint8_t* pl = w.player(rec[0x0F]);
        for (size_t k = 0; k < 3; k++) {
            const int skill = w.b(base - 2, base + 2);
            if (k == 2) {
                const int form = w.b(40, 60);
                rec[0x12] = uint8_t(form);
                pl[0x17] = uint8_t(form);
            } else {
                rec[0x10 + k] = uint8_t(skill);
                pl[0x15 + k] = uint8_t(skill);
            }
        }
    }
    // the manager fields: league +0xEB, +0xE4, stadium places +0x133 / +0x137 (more for a
    // one-season game), stadium facilities +0x116, +0xE5 = 0x10
    const size_t mb = w.manager(m);
    w.d(mb + 0xEB) = uint8_t(v);
    w.d(mb + 0xE4) = uint8_t(int32_t((20 - 2 * v) * 2 * int32_t(le32(w.ds, 0x07A1))) / int32_t(le32(w.ds, 0x079D)));
    put16(w.ds, mb + 0x133, uint16_t(3000 - 500 * v));
    put16(w.ds, mb + 0x137, uint16_t(4500 - 1000 * v));
    if (w.w(dsEndYear) == 0x7CA) {
        put16(w.ds, mb + 0x133, uint16_t(w.w(mb + 0x133) + 1200));
        put16(w.ds, mb + 0x137, uint16_t(w.w(mb + 0x137) + 2500));
    }
    const int stadium[9] = {2 - v, v == 0, v == 0, 0, 2, v == 0, v < 2, 0, 0};
    for (size_t i = 0; i < 9; i++)
        if (i != 3) w.d(mb + 0x116 + 2 * i) = uint8_t(stadium[i]);
    w.d(mb + 0xE5) = 0x10;

    // the steps on the HOK state: values and salaries, image, sponsor offers, lineup, team refresh
    HokFile h = make_hok(world_state(w));
    ContractTables worth = w.contracts;
    worth.level = w.d(dsLevelSalary);
    for (int si = 0; si < occupied; si++) {
        const ContractPlayer c = contract_player(h, int(m), si);
        const size_t r = 0x696D + m * kSquadBlock + size_t(si) * kSquadRec;
        put32(h.state, r + 0x2C, uint32_t(int32_t(player_worth(worth, c, Worth::Value, w.brng))));
        put32(h.state, r + 0x28, uint32_t(int32_t(player_worth(worth, contract_player(h, int(m), si), Worth::Salary, w.brng))));
    }
    {
        ImageInputs in;
        const auto competition = image_competition_factors(h, int(m));
        in.in_competition = competition.playoffs;
        in.cup = competition.cup;
        in.euro = competition.euro;
        in.league = v;
        in.matchday = w.d(dsMatchday + size_t(v));
        std::array<int, 9> st9{};
        for (size_t i = 0; i < 9; i++) st9[i] = h.state.at(0x0DCD + m * kManagerRec + 0x116 + 2 * i);
        const std::array<int, 2> places{le16(h.state, 0x0DCD + m * kManagerRec + 0x133),
                                        le16(h.state, 0x0DCD + m * kManagerRec + 0x137)};
        in.stadium = stadium_score(sponsor_tables, st9, places);
        std::vector<ContractPlayer> squad;
        for (int si = 0; si < kHokSquadSlots; si++) {
            int p = -1;
            ContractPlayer c = contract_player(h, int(m), si, &p);
            if (p >= 0) squad.push_back(c);
        }
        const int image = club_image(in, squad, worth, w.brng);
        put16(h.state, 0x0DCD + m * kManagerRec + 0x173, uint16_t(image));
        SponsorOffers offers = sponsor_offers_from_save(h.state, int(m));
        generate_sponsor_offers(sponsor_tables, image, w.w(dsEndYear) == 0x7CA, offers, w.brng);
        store_sponsor_offers(h.state, offers, int(m));
    }
    auto_lineup(h, int(m));
    refresh_menu_team(h, int(m));
    put32(h.state, 0x0DCD + m * kManagerRec + 0x193, 0);
    load_world_state(w, h.state);
}

// See world_generator.hpp. The steps follow the original order; each is marked in the log with
// the state of both random streams.
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
        w.log.push_back(std::string(step) + ": borland " + std::to_string(w.brng.state()) + ", game " + std::to_string(w.grng.state()) +
                        (g_borland_log ? ", borland draws " + std::to_string(g_borland_log->size()) : std::string()));
    };
    // ---- program start, before the start menu
    w.brng = BorlandRandom(seeds.start);  // first seed
    (void)w.b(0, 2);                      // intro or not
    for (int hi : seeds.intro_draws) (void)w.b(0, hi);  // the draws of the intro animation (see WorldSeeds)
    w.team_records();
    mark("team records");
    w.brng = BorlandRandom(seeds.menu);  // second seed
    const int warm = w.b(100, 300);      // warm-up of the game generator: rand(100, 300) draws
    for (int i = 0; i < warm; i++) (void)w.g(0, 1);
    w.d(dsManagerCount) = 0;
    w.reset_managers();
    w.d(dsLeagueLists) = 2;  // three start values the program sets before the shuffle
    w.d(dsLeagueLists + 2) = 0;
    w.d(dsChampion) = 2;
    w.shuffle_leagues();
    w.euro_entrants();
    for (size_t gr = 0; gr < 3; gr++) std::fill_n(w.ds.begin() + long(0xD014 + gr * 32), 32, uint8_t(0));  // cleared competition tables
    std::fill_n(w.ds.begin() + long(0x5C4B), 0x80, uint8_t(0));
    (void)w.b(0, 0x8FFF);  // drawn for the high word of the session id, which the original then shifts out of range
    out.session_id = uint32_t(w.b(0, 0x8FFF) + 1);
    put32(w.ds, dsSession, out.session_id);
    mark("before start menu");

    // ---- start menu "Los jetzt!": managers, clubs, faces and settings
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
    w.d(dsSquadSize) = uint8_t(settings.squad_size);
    w.d(dsSquadSize + 1) = uint8_t(settings.squad_strength);  // the "Start-Stärke" option
    const int mode = settings.start_mode;
    w.d(dsMode) = uint8_t((mode == 2 ? 1 : 0) + (mode == 1 ? 2 : 0));
    if (mode == 3) w.d(dsMode) = 4;
    put16(w.ds, dsEndYear, uint16_t(settings.end_choice == 3 ? 0x56CE : 0x7CA + 2 * settings.end_choice));
    w.d(dsStartFlag) = 1;
    put16(w.ds, dsSeasonYear, 0x7C9);
    w.d(dsLevelSalary) = uint8_t(5 - settings.level);
    w.generate_players();
    mark("players and squads");
    w.d(dsWmRound) = 0;  // WM reset (its schedule is written to the state further below)
    w.d(dsWmPhase) = 0;
    w.d(dsWmFlag) = 1;
    w.d(dsCompetitionRound) = 0;
    w.d(dsEuroRound) = 0;  // EuroCup reset (its schedule is written to the state further below)
    w.d(dsCompetitionRound) = 0;
    w.d(dsCompetition) = 0xFF;
    // The EuroCup groups are drawn here with Borland, the same rule as at the season change
    // (competitions.cpp draw_euro_groups): eight foreign picks rand(64, 149), where a team of
    // strength below 70 is redrawn unless rand(0, 3) == 0, then the German entrants placed into
    // random slots rand(0, 7) that still hold a foreign team; all six manager phase bytes become 30.
    std::array<std::array<int, 4>, 2> euro_groups{};
    if (!std::getenv("EM_OLD_WORLDEURO")) {
        auto strength = [&](int t) {
            int sum = 0;
            for (size_t k = 0; k < 2; k++) sum += w.team(size_t(t))[0x18 + k] + w.team(size_t(t))[0x1A + k] + w.team(size_t(t))[0x1C + k];
            return sum / 6;
        };
        std::array<int, 8> slot{};
        for (size_t i = 0; i < slot.size(); i++)
            for (;;) {
                const int t = w.b(64, 149);
                if (strength(t) < 70 && w.b(0, 3) != 0) continue;
                if (std::find(slot.begin(), slot.begin() + long(i), t) != slot.begin() + long(i)) continue;
                slot[i] = t;
                break;
            }
        for (size_t i = 0; i < w.d(dsEntrantCount); i++)
            for (;;) {
                const int s = w.b(0, 7);
                if (slot[size_t(s)] < 64) continue;
                slot[size_t(s)] = w.d(dsEntrants + i);
                break;
            }
        euro_groups = {{{slot[0], slot[1], slot[2], slot[3]}, {slot[4], slot[5], slot[6], slot[7]}}};
        for (size_t manager = 0; manager < 6; manager++) w.d(w.manager(manager) + 0xE7) = 30;
    }
    // After the players and squads a new game runs the manager loop (the one used for added managers) over every
    // manager, then the distribution of the free national players. Each manager: the training plan, the youth teams
    // (quality 0x34 + 14 * (2 - start league), rank 5, league = start league), the balance +0x18F = salary level *
    // 150000 + 1200000, reporting mode 2 for the club (the fourth club in mode 2 demotes the first computer club of
    // mode 2 to mode 1), +0x18B = 99999; the Borland draw of a club of the start league, swapped into the manager's
    // place (only when the start league is 0, the club is in another league or beyond the limit at data image 0x07CB;
    // never a club of an earlier manager nor a EuroCup group team) and the one-manager branch (end 1994: a
    // first-division club that IS in a EuroCup group, +0xE9 = 1); the strengths of the squad, values and salaries,
    // the league +0xEB, +0xE4, the stadium +0x116 and places +0x133/+0x137, +0xE5 = 0x10, the image +0x173, the
    // sponsor offers, the lineup, the team refresh and +0x193 = 0. All draws use the Borland stream, which is
    // re-seeded afterwards.
    if (!settings.menu_order_only && !std::getenv("EM_OLD_WORLDEURO"))  // the EuroCup groups (state 0x8E71)
        for (size_t g = 0; g < 2; g++)
            for (size_t t = 0; t < 4; t++) w.d(0x5D1B + g * 4 + t) = uint8_t(euro_groups[g][t]);
    const SponsorTables sponsor_tables = load_sponsor_tables(exe);
    for (size_t m = 0; m < managers.size() && !settings.menu_order_only; m++) {
        manager_loop_step(w, m, true, sponsor_tables);
    }
    if (!settings.menu_order_only && !std::getenv("EM_OLD_WORLDLOOP")) {  // the free national players
        HokFile h = make_hok(world_state(w));
        const Database db_decoded = decode_db(db);
        const int renamed =
            redistribute_free_players(h, db_decoded, w.brng, 0, [&](int team) {
                return team >= 0 && size_t(team) < h.teams.size() && h.teams[size_t(team)].raw && h.teams[size_t(team)].raw[0x16] != 0;
            });
        for (const HokPlayer& pl : h.players)  // the new teams (the struct field; store_hok writes it in a running game)
            if (pl.raw) h.state.at(size_t(pl.raw - h.state.data()) + 0x1D) = pl.team < 0 ? 0xFF : uint8_t(pl.team);
        w.log.push_back("free national players: " + std::to_string(renamed) + " national players, " + std::to_string(db_decoded.clubs.size()) +
                        " database clubs, " + std::to_string(h.players.size()) + " players");
        load_world_state(w, h.state);
    }
    if (!settings.menu_order_only && !std::getenv("EM_OLD_WORLDLOOP"))  // the league-season copy of the form (row +9, see
        for (size_t t = 0; t < 36; t++)                                   // hok.cpp write_table_row_copies) of the 36 rows
            std::copy_n(w.ds.begin() + long(dsFormString), 8, w.ds.begin() + long(0x825E + 9 + t * 0x50));
    mark("manager loop");

    // ---- program start, after the start menu
    w.rerate_clubs();
    mark("club values");
    out.game_random = w.grng.state();
    w.brng = BorlandRandom(seeds.after);  // third seed
    w.d(dsMatchday) = 1;

    Bytes st = world_state(w);
    if (!std::getenv("EM_OLD_WORLDCONST")) {
        // bytes that every start state of the original holds and that no ported step writes: the clubs of the pool
        // party and the national coach (+0x14 = 0xFD), +0xEB of the national coach (50), the owner of player record 0
        // (+0x1A = 6) and one flag byte (data image 0xD0E5)
        st.at(0x0DCD + 4 * kManagerRec + 0x14) = 0xFD;
        st.at(0x0DCD + 5 * kManagerRec + 0x14) = 0xFD;
        st.at(0x0DCD + 5 * kManagerRec + 0xEB) = 50;
        st.at(0x4FA5 + 0x1A) = 6;
        st.at(state_of(0xD0E5)) = 1;
        // the league pairings of the first matchday (state 0x8DFD) and the free WM slots (state 0x8EB1)
        const ScheduleTables schedule = load_schedule_tables(exe);
        const auto pairs = round_robin_pairings(schedule.teams12, 0);
        for (size_t l = 0; l < 3; l++)
            for (size_t k = 0; k < pairs.size() && k < 6; k++) {
                st.at(0x8DFD + l * 12 + k * 2) = uint8_t(12 * l + size_t(pairs[k].home));
                st.at(0x8DFD + l * 12 + k * 2 + 1) = uint8_t(12 * l + size_t(pairs[k].away));
            }
        std::fill_n(st.begin() + 0x8EB1, 32, uint8_t(0xFF));
        // further bytes of every start state: two bytes next to the cup round (1 and 3), a settings byte (1), the
        // event cursor (1) and the month counter (8)
        st.at(state_of(0x5CF4)) = 1;
        st.at(state_of(0x5CF6)) = 3;
        st.at(state_of(0x022A)) = 1;
        st.at(state_of(0xA969)) = 1;
        st.at(state_of(0xA8A0)) = 8;
    }
    // the first day step rates the managers' clubs from their squads again (team refresh, no draws; the original's start
    // state already holds it); the WM-only mode skips the club days, its clubs keep the season-start club values
    if (!settings.menu_order_only && !std::getenv("EM_OLD_WORLDLOOP") && w.d(dsMode) != 1) {
        HokFile h = make_hok(std::move(st));
        for (size_t m = 0; m < managers.size(); m++) refresh_menu_team(h, int(m));
        st = std::move(h.state);
    }

    // the player distribution: skills, random club changes, quotas, then the clubs of the free players
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
    mark("player distribution");
    // the first DEB cup round
    st.at(state_of(0xD0E4)) = 1;
    st.at(state_of(dsCupRound)) = 0;
    st.at(state_of(0xD074)) = 0;
    std::fill_n(st.begin() + long(state_of(0xD075)), 32, uint8_t(0));
    std::vector<int> humans;
    for (size_t m = 0; m < managers.size(); m++) humans.push_back(st.at(0x0DCD + m * kManagerRec + 0x14));
    if (!std::getenv("EM_OLD_WORLDLOOP"))  // the cup phase byte +0xE6: 30 for all six managers, 1 for the humans in the draw
        for (size_t m = 0; m < 6; m++) st.at(0x0DCD + m * kManagerRec + 0xE6) = m < managers.size() ? 1 : 30;
    const auto cup = draw_initial_cup(humans, w.brng, &out.cup_places);
    // the live draw (answer "Aber klar" to the cup question): 32 places uncovered in Borland order
    if (settings.cup_live) out.cup_reveal = cup_reveal_order(int(out.cup_places.size()), w.brng);
    if (cup.size() * 2 < 32) st.at(state_of(dsCupDraw) + cup.size() * 2) = 0xFF;
    for (size_t i = 0; i < cup.size(); i++) {
        st.at(state_of(dsCupDraw) + 2 * i) = uint8_t(cup[i].home);
        st.at(state_of(dsCupDraw) + 2 * i + 1) = uint8_t(cup[i].away);
    }
    st.at(state_of(dsCupRound)) = 1;
    if (!std::getenv("EM_OLD_WORLDEURO")) {  // the EuroCup groups drawn above and their schedule (kind 2)
        const ScheduleTables schedule = load_schedule_tables(exe);
        for (size_t g = 0; g < 2; g++) {
            for (size_t t = 0; t < 4; t++) st.at(0x03A4 + g * 4 + t) = st.at(0x8E71 + g * 4 + t) = uint8_t(euro_groups[g][t]);
            for (int r = 0; r < 6; r++) {
                const auto pairs = round_robin_pairings(schedule.teams4, r);
                const size_t row = 0x03B8 + size_t(2 * 0x48 + g * 0x24 + size_t(r) * 6);
                for (size_t p = 0; p < pairs.size(); p++) {
                    st.at(row + p * 2) = uint8_t(euro_groups[g][size_t(pairs[p].home)]);
                    st.at(row + p * 2 + 1) = uint8_t(euro_groups[g][size_t(pairs[p].away)]);
                }
                // the third pair of every row reads (61, 63) in every state of the original; the season change
                // keeps it (Game::store_group_schedule writes two pairs)
                st.at(row + 4) = 61;
                st.at(row + 5) = 63;
            }
            for (size_t i = 0; i < 6; i++) st.at(0x05F8 + 2 * 12 + g * 6 + i) = uint8_t(i);  // the group order (kind 2)
        }
    }
    if (!std::getenv("EM_OLD_WORLDWM")) {  // the WM schedule: groups 52..57 and 58..63, five rounds (kind 0)
        const ScheduleTables schedule = load_schedule_tables(exe);
        for (size_t g = 0; g < 2; g++) {
            for (int r = 0; r < 5; r++) {
                const auto pairs = round_robin_pairings(schedule.teams6, r);
                for (size_t p = 0; p < pairs.size() && p < 3; p++) {
                    const size_t at = 0x03B8 + g * 0x24 + size_t(r) * 6 + p * 2;
                    st.at(at) = uint8_t(52 + 6 * g + size_t(pairs[p].home));
                    st.at(at + 1) = uint8_t(52 + 6 * g + size_t(pairs[p].away));
                }
            }
            for (size_t i = 0; i < 6; i++) st.at(0x05F8 + g * 6 + i) = uint8_t(i);  // the group order (kind 0)
        }
    }
    mark("cup draw");
    put32(st, state_of(dsDayCounter), uint32_t(int32_t(int8_t(w.d(dsDayStart)))));  // the day counter starts at the start day
    {
        const uint8_t current = st.at(state_of(dsCurrentManager));
        st.at(state_of(dsCurrentManager)) = 4;
        refresh_transfer_list(st, w.contracts, w.brng);
        st.at(state_of(dsCurrentManager)) = std::getenv("EM_OLD_WORLDCONST") ? current : 0;  // the first manager's turn
    }
    mark("transfer list");
    for (size_t c = 0; c < 4; c++)  // current share price (last history entry) = target price / 50
        put16(st, 0x00B0 + c * 0x48 + 0x46, uint16_t(le32(st, 0x01D0 + 4 * c) / 50));
    for (int day = 0; day < 90; day++) market_day(st, w.brng);  // 90 market days fill the price history
    mark("market");
    // date of the first day (season day 1, Sunday 10.09.1993), as in the shipped start state
    put32(st, state_of(dsDay), 10);
    put32(st, state_of(dsMonth), 8);
    put32(st, state_of(dsYear), 1993);
    put32(st, state_of(dsDayCounter), 1);

    out.save = make_hok(std::move(st));
    out.distribution_cursors = cursors;
    out.borland = w.brng.state();
    out.log = std::move(w.log);
    return out;
}

// See world_generator.hpp. Throws FormatError (with a German message for the UI) when the save
// is not a game state, four managers already play, the name is not 2..18 characters long or the
// club is not free.
HokFile add_manager_to_game(const Bytes& exe, const Bytes& db, const HokFile& save, const WorldManager& manager,
                            BorlandRandom& rng) {
    if (save.state.size() != kStateSize) throw FormatError("Neuer Manager: unbekannter Spielstand");
    World w(exe, db);
    load_world_state(w, save.state);
    w.brng = rng;
    const size_t m = w.d(dsManagerCount);
    if (m >= 4) throw FormatError("Neuer Manager: schon vier Manager");
    if (manager.name.size() < 2 || manager.name.size() > 18) throw FormatError("Neuer Manager: Name mit 2 bis 18 Buchstaben");
    if (manager.team < 0 || manager.team > 47 || w.team(size_t(manager.team))[0x16] != 0)
        throw FormatError("Neuer Manager: Verein nicht frei");
    const uint8_t current = w.d(dsCurrentManager);
    // as in the start menu: name, face and club of the new row
    std::fill_n(w.ds.begin() + long(w.manager(m)), 0x13, uint8_t(0));
    for (size_t k = 0; k < manager.name.size(); k++) w.d(w.manager(m) + k) = uint8_t(manager.name[k]);
    w.d(w.manager(m) + 0x13) = uint8_t(manager.face);
    w.club_of(m) = uint8_t(manager.team);
    w.team(size_t(manager.team))[0x16] = uint8_t(m + 1);
    w.d(dsFaces + m) = uint8_t(manager.face);
    w.d(dsManagerCount)++;
    // reporting mode, phase bytes, history, the pool draft
    {
        const size_t club = w.club_of(m);
        const size_t lo = club >= 52 && club < 64 ? 52 : 0, hi = club >= 52 && club < 64 ? 64 : 150;
        int count = 0;
        for (size_t t = lo; t < hi; t++) {  // reporting mode 2 for the club, as in manager_loop_step
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
    }
    for (size_t c = 0; c < 5; c++) w.d(w.manager(m) + 0xE6 + c) = 0x1E;
    for (size_t k = 0; k < 50; k++) w.d(w.manager(m) + 0x18 + 4 * k) = 0xFF;
    const int setting = w.d(dsSquadSize);
    w.d(dsCurrentManager) = uint8_t(m);
    for (size_t g = 0; g < 3; g++)
        for (int n = 0; n < w.d(dsGroupSizes + 4 * size_t(setting) + g); n++) {
            int id = 0;
            for (int tries = 0; tries < 1000;) {
                id = w.b(w.d(dsIdBound + g), w.d(dsIdBound + 1 + g) - 1);
                tries++;
                if (std::getenv("EM_DEBUG_DRAFT")) std::fprintf(stderr, "draft g%zu id %d owner %d club %d\n", g, id, id < 200 ? w.player(size_t(id))[0x1A] : -1, id < 200 ? w.player(size_t(id))[0x1D] : -1);
                if (id < 0xB4 && w.player(size_t(id))[0x1A] == 6) break;
            }
            const int at = w.sign_player(id, 1, 0, 0x1D);  // a one-season contract
            w.player(size_t(id))[0x1A] = uint8_t(m);
            uint8_t* r = w.squad(m, size_t(at));
            r[0x19] = uint8_t(w.b(3, 12) << 4);
            r[0x19] |= uint8_t(w.b(3, 12));
            w.player(size_t(id))[0x1D] = w.club_of(m);
            w.player(size_t(id))[0x14] = uint8_t(w.b(0x13, 0x1F));
        }
    // the transfer list (block 4): a row whose player is now the new manager's, or plays for a club of the same name,
    // leaves the list (the rows behind move up, row 10 is emptied); the search starts again each time
    auto same_name = [&](int a, int b) {
        if (a < 0 || a >= kTeams || b < 0 || b >= kTeams) return false;
        return std::equal(w.team(size_t(a)), w.team(size_t(a)) + 0x16, w.team(size_t(b)));
    };
    for (bool removed = true; removed;) {
        removed = false;
        int listed = 0;
        for (size_t slot = 0; slot < 0x1D - 0x11; slot++) listed += w.squad(4, slot)[0x0F] != 0;
        for (int row = 0; row < listed && !removed; row++) {
            const int id = w.squad(4, size_t(row))[0x0F];
            if (w.player(size_t(id))[0x1A] != m && !same_name(w.club_of(m), w.player(size_t(id))[0x1D])) continue;
            if (row != 10) std::memmove(w.squad(4, size_t(row)), w.squad(4, size_t(row) + 1), size_t(10 - row) * kSquadRec);
            w.squad(4, 10)[0x0F] = 0;
            removed = true;
        }
    }
    // the manager loop for the new manager (not a new game: no one-manager branch, no distribution of the free
    // national players afterwards)
    w.d(dsCurrentManager) = current;
    manager_loop_step(w, m, false, load_sponsor_tables(exe));
    rng = w.brng;
    HokFile out = make_hok(world_state(w));
    out.header = save.header;  // date and save identity stay; the manager count and the new preview row change
    out.header[0x0E] = uint8_t(m + 1);
    const HokFile previews = make_hok(world_state(w));
    std::copy_n(previews.header.begin() + long(0x10 + 40 * m), 40, out.header.begin() + long(0x10 + 40 * m));
    out.key0 = save.key0;
    return decode_hok(encode_hok(out));
}

}  // namespace em
