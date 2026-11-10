// SPDX-License-Identifier: GPL-3.0-only
// Invariants of the new-game world generator (game/world_generator.hpp, docs/logic/WELTGENERATOR.md).
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>

#include "formats/db.hpp"
#include "formats/hok.hpp"
#include "game/world_generator.hpp"

using namespace em;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        failures++;
    }
}
constexpr size_t kTeams = 0x17E7, kTeamRec = 0x3D, kPlayers = 0x4FA5, kPlayerRec = 0x21, kSquads = 0x696D;
const uint8_t* player(const HokFile& h, int id) { return h.state.data() + kPlayers + size_t(id) * kPlayerRec; }
const uint8_t* team(const HokFile& h, int t) { return h.state.data() + kTeams + size_t(t) * kTeamRec; }
const uint8_t* squad(const HokFile& h, int m, int slot) { return h.state.data() + kSquads + size_t(m) * 0x618 + size_t(slot) * 0x34; }
std::string raw_name(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && p[i]; i++) s.push_back(char(p[i]));
    return s;
}
std::string db_raw(const Bytes& db, size_t at, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && db.at(at + i); i++) s.push_back(char(db[at + i]));
    return s;
}
// raw native name of database club c (0..63), slot (0..24)
std::string db_club(const Bytes& db, int c) { return db_raw(db, 0x4AE + size_t(c) * 0x20B, 22); }
std::string db_player(const Bytes& db, int c, int slot) { return db_raw(db, 0x4AE + size_t(c) * 0x20B + 0x17 + size_t(slot) * 20, 20); }

void check_world(const Bytes& exe, const Bytes& db, const WorldSettings& s, const WorldSeeds& seeds, const std::string& tag) {
    const WorldResult r = generate_world(exe, db, s, seeds);
    const HokFile& h = r.save;
    check(h.state.size() == 0xBA91, tag + ": state size");
    check(h.header[0x0E] == s.managers.size() && h.state.at(0x0DC6) == s.managers.size(), tag + ": manager count");
    check(h.date.year == 1993 && h.date.day_index == 1 && h.date.day == 10 && h.date.month == 9, tag + ": season start date");
    // teams: each division keeps its database clubs, the regional pool and the European clubs stay in place
    for (int l = 0; l < 3; l++) {
        std::set<std::string> want, have;
        for (int t = l * 12; t < l * 12 + 12; t++) {
            want.insert(db_club(db, t));
            have.insert(raw_name(team(h, t), 22));
        }
        check(want == have, tag + ": division " + std::to_string(l) + " holds its clubs");
        std::set<int> order;
        for (int i = 0; i < 12; i++) order.insert(h.state.at(0x8EDC + size_t(l * 12 + i)));
        check(order.size() == 12 && *order.begin() == l * 12 && *order.rbegin() == l * 12 + 11, tag + ": league list DS:5CCB");
    }
    for (int t = 36; t < 52; t++) check(raw_name(team(h, t), 22) == db_club(db, t), tag + ": regional pool / LEER in place");
    std::set<std::string> nations_want, nations_have;
    for (int t = 52; t < 64; t++) {
        nations_want.insert(db_club(db, t));
        nations_have.insert(raw_name(team(h, t), 22));
    }
    check(nations_want == nations_have, tag + ": national teams permuted among themselves");
    for (int t = 64; t < 150; t++) check(raw_name(team(h, t), 22) == db_raw(db, 0x876E + size_t(t - 64) * 0x17, 0x17), tag + ": European club " + std::to_string(t));
    for (int t = 0; t < 150; t++) {
        const uint8_t* rec = team(h, t);
        check(rec[0x17] >= 47 && rec[0x17] <= 53, tag + ": team bonus +0x17");
        check(rec[0x1C] >= 40 && rec[0x1C] <= 55 && rec[0x1D] >= 40 && rec[0x1D] <= 55, tag + ": team form");
    }
    for (size_t m = 0; m < s.managers.size(); m++) check(team(h, s.managers[m].team)[0x16] == m + 1, tag + ": human club +0x16");
    // ovr027:1B27..1DE8 for every manager of a new game; the original's fresh states (BACKUP.HOK: 50 50 50 / 5 5 5 / 0 0 0,
    // +0x18B = 99999; its two-manager game HOT2 at level 3: balance 1500000) agree
    for (size_t m = 0; m < s.managers.size(); m++) {
        const size_t youth = 0x06B8 + m * 0x15, block = 0x0DCD + m * 0x1AF;
        for (size_t i = 0; i < 3; i++)
            check(h.state.at(youth + i) == 0x34 + 14 * (2 - s.squad_size) && h.state.at(youth + 3 + i) == 5 && h.state.at(youth + 6 + i) == s.squad_size,
                  tag + ": youth teams of manager " + std::to_string(m));
        check(int32_t(le32(h.state, block + 0x18F)) == (5 - s.level) * 150000 + 1200000, tag + ": balance (DS:0ED8 * 150000 + 1200000)");
        check(le32(h.state, block + 0x18B) == 99999, tag + ": +0x18B = 99999");
        check((team(h, s.managers[m].team)[0x34] >> 6) == 2, tag + ": the manager's club is reported on a board (mode 2)");
    }
    for (size_t m = s.managers.size(); m < 4; m++) check(h.state.at(0x06B8 + m * 0x15) == 0, tag + ": no youth teams for unused managers");
    // players
    std::set<std::string> names;
    for (int id = 1; id < 200; id++) {
        const uint8_t* p = player(h, id);
        const std::string n = raw_name(p, 0x11);
        check(!n.empty(), tag + ": player " + std::to_string(id) + " named");
        names.insert(n);
        check(p[0x14] >= 18 && p[0x14] <= 33, tag + ": age of player " + std::to_string(id));
        check(p[0x15] >= 10 && p[0x15] <= 99 && p[0x16] >= 10 && p[0x16] <= 99, tag + ": skills of player " + std::to_string(id));
        check(p[0x17] >= 45 && p[0x17] <= 55, tag + ": form of player " + std::to_string(id));
        const int pos = p[0x18];
        const bool pos_ok = (id <= 20 || (id >= 180 && id <= 183)) ? pos == 0
                            : ((id >= 21 && id <= 96) || (id >= 184 && id <= 190)) ? pos >= 25 && pos <= 41
                                                                                   : pos >= 75 && pos <= 91;
        check(pos_ok, tag + ": position value of player " + std::to_string(id));
        check(p[0x1A] <= 4 || p[0x1A] == 6, tag + ": owner of player " + std::to_string(id));
        check(p[0x1D] < 150 || p[0x1D] == 0xFF, tag + ": club of player " + std::to_string(id));
    }
    // human squads: ID groups from DS:0382 - DS:015C + 1, names of the club's database entry in slot order
    static const int sizes[3][3] = {{2, 10, 13}, {2, 9, 11}, {2, 8, 10}};
    static const int bound[3] = {21, 97, 200};
    int counter[3] = {20, 76, 103};
    for (size_t m = 0; m < s.managers.size(); m++) {
        const int club = s.managers[m].team;
        const std::string club_name = raw_name(team(h, club), 22);
        int dbc = -1;
        for (int c = 0; c < 64; c++)
            if (db_club(db, c) == club_name) dbc = c;
        check(dbc >= 0, tag + ": human club in the database");
        int slot = 0;
        for (int g = 0; g < 3; g++)
            for (int n = 0; n < sizes[s.squad_size][g]; n++, slot++) {
                const int id = bound[g] - counter[g]-- + 1;
                const uint8_t* p = player(h, id);
                check(squad(h, int(m), slot)[0x0F] == id, tag + ": squad slot " + std::to_string(slot) + " holds ID " + std::to_string(id));
                check(squad(h, int(m), slot)[0x0B] == 2, tag + ": contract years");
                check(p[0x1A] == m && p[0x1D] == club, tag + ": human player owner and club");
                check(p[0x14] >= 19 && p[0x14] <= 31, tag + ": human player age 19..31");
                const std::string want = dbc >= 0 ? db_player(db, dbc, slot) : "";
                if (!want.empty()) check(raw_name(p, 0x11) == want.substr(0, 0x11), tag + ": human player " + std::to_string(id) + " named " + want);
                const int nib = squad(h, int(m), slot)[0x19];
                check((nib >> 4) >= 3 && (nib >> 4) <= 12 && (nib & 15) >= 3 && (nib & 15) <= 12, tag + ": nibbles +0x19");
            }
        check(squad(h, int(m), slot)[0x0F] == 0, tag + ": squad ends after its size");
    }
    // DEB cup draw: 16 pairs of distinct clubs 0..35 with every human club
    std::set<int> cup;
    for (int i = 0; i < 32; i++) cup.insert(h.state.at(0x8E51 + size_t(i)));
    check(cup.size() == 32 && h.state.at(0x8ED1) == 1, tag + ": cup draw");
    for (int t : cup) {
        bool human = false;
        for (const auto& m : s.managers) human |= m.team == t;
        check(t <= 35 || human, tag + ": cup club from 0..35 or human");
    }
    for (const auto& m : s.managers) check(cup.count(m.team) == 1, tag + ": human club in the cup");
    // transfer list: ten draws of a slot rand(0, 9); repeated slots replace their player, so the list holds
    // 1..10 players from slot 0 on, all owned by block 4
    int listed = 0;
    while (listed < 10 && squad(h, 4, listed)[0x0F]) listed++;
    check(listed >= 1, tag + ": transfer list filled");
    for (int slot = 0; slot < 11; slot++) {
        const int id = squad(h, 4, slot)[0x0F];
        if (slot < listed) check(id < 200 && player(h, id)[0x1A] == 4, tag + ": transfer list slot " + std::to_string(slot));
        else check(id == 0, tag + ": transfer list ends");
    }
    int owned4 = 0;
    for (int id = 1; id < 200; id++) owned4 += player(h, id)[0x1A] == 4;
    check(owned4 == listed, tag + ": block-4 owners match the list");
    // share prices: the history ends in a positive current price
    for (int c = 0; c < 4; c++) check(h.finance.share_price[c][35] > 0, tag + ": share price");
    // determinism
    const WorldResult again = generate_world(exe, db, s, seeds);
    check(again.save.state == h.state && again.save.header == h.header, tag + ": deterministic for the same seeds");
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("world_generator_tests DATA [REFERENCE]");
        const std::filesystem::path data = argv[1];
        const Bytes exe = read_file(data / "EM.EXE");
        const Bytes db = read_file(data / "KB" / "23.vga");

        WorldSettings one;
        one.managers = {{"AGENT", 3, 5}};
        check_world(exe, db, one, {1234, 5678, 4321}, "one manager");
        WorldSettings four;
        four.managers = {{"EINS", 0, 0}, {"ZWEI", 1, 13}, {"DREI", 2, 30}, {"VIER", 3, 40}};
        four.squad_size = 1;
        four.end_choice = 3;
        four.start_mode = 2;
        check_world(exe, db, four, {7, 8, 9}, "four managers");
        WorldSettings small = one;
        small.squad_size = 2;
        small.level = 0;
        check_world(exe, db, small, {0xFFFF, 0, 1}, "small squad");

        // other seeds give another world; the post-menu seed alone changes only the later parts
        const auto a = generate_world(exe, db, one, {1234, 5678, 4321});
        const auto b = generate_world(exe, db, one, {1235, 5678, 4321});
        const auto c = generate_world(exe, db, one, {1234, 5678, 4322});
        check(a.save.state != b.save.state, "start seed changes the world");
        check(a.save.state != c.save.state, "after seed changes the world");
        bool same_teams = true;
        for (int t = 0; t < 150; t++)
            for (size_t k = 0; k < 0x16; k++) same_teams &= team(a.save, t)[k] == team(c.save, t)[k];
        check(same_teams, "after seed keeps the league order");
        check(a.game_random == c.game_random, "GameRandom does not depend on the after seed");

        // the shipped start state (BACKUP.HOK, written by the original) has the same fixed parts
        const auto backup_file = data / "BACKUP.HOK";
        if (std::filesystem::exists(backup_file)) {
            const HokFile backup = decode_hok(read_file(backup_file));
            for (int t = 36; t < 150; t++)
                if (t < 52 || t >= 64) check(raw_name(team(backup, t), 22) == raw_name(team(a.save, t), 22), "BACKUP.HOK: fixed team name " + std::to_string(t));
            std::vector<int> ids;
            for (int slot = 0; slot < 30; slot++)
                if (squad(backup, 0, slot)[0x0F]) ids.push_back(squad(backup, 0, slot)[0x0F]);
            std::vector<int> want{2, 3};
            for (int id = 22; id <= 31; id++) want.push_back(id);
            for (int id = 98; id <= 110; id++) want.push_back(id);
            std::sort(ids.begin(), ids.end());
            check(ids == want, "BACKUP.HOK: squad IDs of manager 0 follow DS:0382 - DS:015C + 1");
            for (int id = 1; id < 200; id++) {
                const int pos = player(backup, id)[0x18];
                const bool pos_ok = (id <= 20 || (id >= 180 && id <= 183)) ? pos == 0
                                    : ((id >= 21 && id <= 96) || (id >= 184 && id <= 190)) ? pos >= 25 && pos <= 41
                                                                                           : pos >= 75 && pos <= 91;
                check(pos_ok, "BACKUP.HOK: position value of player " + std::to_string(id));
            }
            for (int t = 52; t < 64; t++) {  // nations: fixed skills from DS:4C4E (- 2), whatever order
                bool found = false;
                for (int u = 52; u < 64; u++)
                    found |= raw_name(team(backup, t), 22) == raw_name(team(a.save, u), 22) &&
                             std::equal(team(backup, t) + 0x18, team(backup, t) + 0x1C, team(a.save, u) + 0x18);
                check(found, "BACKUP.HOK: skills of national team " + std::to_string(t));
            }
        }
        if (failures) {
            std::printf("%d failures\n", failures);
            return 1;
        }
        std::printf("world generator: ok\n");
        return 0;
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
}
