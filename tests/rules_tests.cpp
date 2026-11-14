// SPDX-License-Identifier: GPL-3.0-only
// Tests for the rule differences between version 1.00 and 1.30 of EM.EXE (src/game/rules.hpp):
// version detection from the EXE bytes, the form re-roll of the rating routine (1.30 leaves the
// energy of the first defence line alone and draws one number less per team), the competition
// bonus of the attendance (1.30 gives the European cup final round the group-phase bonus), the
// strength boost of a computer club that buys a player (1.30 uses the buyer's own mean), and the
// world generator with a 1.30 EXE. The 1.00 behaviour is checked to stay as it was.
// Arguments: the game data folder (version 1.00), the EM.EXE of version 1.30, and optionally the
// folder with the recorded sale saves.
#include "game/game.hpp"
#include "game/rules.hpp"
#include "game/sale_quote.hpp"
#include "game/season_end.hpp"
#include "game/world_generator.hpp"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
using namespace em;
namespace fs = std::filesystem;
// Throws `why` as a test failure unless `ok`.
static void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }

// Squad row `slot` of block `block` and team record `t` in the HOK state.
static size_t row_at(int block, int slot) { return 0x696D + size_t(block) * 0x618 + size_t(slot) * 52; }
static size_t team_at(int t) { return 0x17E7 + size_t(t) * 61; }

// Version detection and the version text.
static void detection(const Bytes& exe100, const Bytes& exe130) {
    require(exe100.size() == exe130.size(), "both EXEs have the same size");
    require(std::string(exe130.begin() + long(kExeVersionText), exe130.begin() + long(kExeVersionText) + 14) == "V1.30 - Ende: ",
            "1.30 version text at its file offset");
    const GameRules r100 = detect_game_rules(exe100), r130 = detect_game_rules(exe130);
    require(r100.version == GameVersion::V100 && !r100.v130() && std::string(r100.label()) == "V1.00", "1.00 EXE detected as 1.00");
    require(r130.version == GameVersion::V130 && r130.v130() && std::string(r130.label()) == "V1.30", "1.30 EXE detected as 1.30");
    require(!detect_game_rules(Bytes(100, 0)).v130() && !GameRules{}.v130(), "short or unknown files and the default are 1.00");
    Bytes other = exe130;
    other[kExeVersionText + 3] = '2';  // "V1.20" is not a known version
    require(!detect_game_rules(other).v130(), "an unknown version text counts as 1.00");
    std::puts("detection: 1.00 and 1.30 recognised");
}

// The form re-roll at the end of the rating routine, and a whole rating in both modes.
static void rating(const Bytes& exe100) {
    const RatingBounds bounds = load_rating_bounds(exe100);
    HokTeam base;
    base.skill1[0] = 80; base.skill1[1] = 82; base.skill2[0] = 78; base.skill2[1] = 84;
    base.form[0] = 10; base.form[1] = 11;
    for (auto& kind : base.energy) for (int& e : kind) e = 90;
    for (uint32_t seed : {1u, 4711u, 3999999u}) {
        // the draws the routine makes, from a second generator with the same seed
        GameRandom expected(seed);
        const int f0 = int(expected.range(45, 55)), f1 = int(expected.range(45, 55));
        const uint32_t after_two = expected.state();
        const int e = int(expected.range(45, 55));
        const uint32_t after_three = expected.state();

        HokTeam t130 = base; GameRandom g130(seed);
        reroll_form(t130, g130, GameRules{GameVersion::V130});
        require(t130.form[0] == f0 && t130.form[1] == f1 && g130.state() == after_two, "1.30 re-rolls the two form values only");
        require(t130.energy[0][0] == 90 && t130.energy[0][1] == 90 && t130.energy[1][0] == 90, "1.30 keeps the energy of the first defence line");

        HokTeam t100 = base; GameRandom g100(seed);
        reroll_form(t100, g100, GameRules{});
        require(t100.form[0] == f0 && t100.form[1] == f1 && t100.energy[0][0] == e && g100.state() == after_three,
                "1.00 also writes rand(45, 55) into the energy of the first defence line");

        // A whole rating (new season, first division bounds 69..94) by hand: r = rand(0, 6),
        // trend = rand(0, 2r + 6) - 6; skill 1 += rand(trend - 1, trend + 1); skill 2: rand(0, 2)
        // (ignored), += rand(trend - 1, trend + 1); then the form re-roll.
        GameRandom hand(seed);
        const long r = hand.range(0, 6), trend = hand.range(0, 2 * r + 6) - 6;
        int s1[2], s2[2];
        for (int k = 0; k < 2; k++) s1[k] = std::clamp(int(base.skill1[k] + hand.range(trend - 1, trend + 1)), 69, 94);
        for (int k = 0; k < 2; k++) { (void)hand.range(0, 2); s2[k] = std::clamp(int(base.skill2[k] + hand.range(trend - 1, trend + 1)), 69, 94); }
        const int h0 = int(hand.range(45, 55)), h1 = int(hand.range(45, 55));
        const uint32_t hand_state = hand.state();
        HokTeam season = base; GameRandom gs(seed);
        rerate_team(season, 0, bounds, gs, 3, GameRules{GameVersion::V130});
        require(season.skill1[0] == s1[0] && season.skill1[1] == s1[1] && season.skill2[0] == s2[0] && season.skill2[1] == s2[1] &&
                    season.form[0] == h0 && season.form[1] == h1 && season.energy[0][0] == 90 && gs.state() == hand_state,
                "1.30 new-season rating by hand");
        HokTeam season100 = base; GameRandom gs100(seed);
        rerate_team(season100, 0, bounds, gs100, 3);
        require(season100.skill1[0] == s1[0] && season100.skill2[1] == s2[1] && season100.form[1] == h1 && season100.energy[0][0] != 90 &&
                    season100.energy[0][0] >= 45 && season100.energy[0][0] <= 55,
                "1.00 new-season rating keeps writing the energy byte");
    }
    std::puts("rating: 1.30 form re-roll without the energy byte");
}

// Competition bonus of the attendance in both versions, and its effect on one home game.
static void attendance(const Bytes& exe100) {
    const GameRules r100{}, r130{GameVersion::V130};
    require(attendance_kind(GateCompetition::League, r100) == 0 && attendance_kind(GateCompetition::Cup, r100) == 1 &&
                attendance_kind(GateCompetition::Playoffs, r100) == 1 && attendance_kind(GateCompetition::EuroGroup, r100) == 2 &&
                attendance_kind(GateCompetition::EuroFinal, r100) == 0 && attendance_kind(GateCompetition::WorldChampionship, r100) == 0,
            "1.00 attendance kinds");
    require(attendance_kind(GateCompetition::League, r130) == 0 && attendance_kind(GateCompetition::Cup, r130) == 1 &&
                attendance_kind(GateCompetition::Playoffs, r130) == 1 && attendance_kind(GateCompetition::EuroGroup, r130) == 2 &&
                attendance_kind(GateCompetition::EuroFinal, r130) == 2 && attendance_kind(GateCompetition::WorldChampionship, r130) == 0,
            "1.30 attendance kinds: the European cup final round as the group phase");
    // A European cup final round home game of a first-division club (no stadium extensions, ticket
    // price 0 so that the price effect is neutral): with a large hall the competition bonus is
    // visible in the spectators.
    const FinanceTables tables = load_finance_tables(exe100);
    ManagerBudget budget;
    budget.places = {60000, 0};
    budget.ticket_price = 0;
    AttendanceMatch match;
    match.home_strength = match.away_strength = 70;
    match.home_rank = 0; match.away_rank = 1;
    match.league_statistics = false;
    long spectators[2];
    for (int v = 0; v < 2; v++) {
        match.kind = attendance_kind(GateCompetition::EuroFinal, v ? r130 : r100);
        ManagerBudget b = budget; BorlandRandom rng(12345);
        spectators[v] = home_attendance(tables, b, match, rng);
    }
    match.kind = attendance_kind(GateCompetition::EuroGroup, r100);
    ManagerBudget bg = budget; BorlandRandom rg(12345);
    const long group = home_attendance(tables, bg, match, rg);
    std::printf("attendance: European cup final round 1.00 %ld, 1.30 %ld, group phase %ld\n", spectators[0], spectators[1], group);
    require(spectators[1] == group && spectators[0] < group, "1.30 final round draws as many spectators as the group phase, 1.00 fewer");
}

// The strength boost of a computer club that buys a player, with the recorded sale saves.
static void sale(const Bytes& exe100, const fs::path& native) {
    const ContractTables tables = load_contract_tables(exe100);
    const GameRules r130{GameVersion::V130};
    // Club row: squad row 6 of manager 0, offer of team 3. The original takes the buyer and the
    // skills from the row that moves up into the slot (row 7 before the sale).
    {
        HokFile h = decode_hok(read_file(native / "native-club-sale-before.hok"));
        BorlandRandom random(1941922524u);
        SaleQuote quote;
        require(begin_native_sale(h, tables, random, 0, 6, 51, quote) && random.state() == 3826679693u, "club quote as in 1.00");
        const size_t next = row_at(0, 7);
        const int buyer = h.state.at(next + 22);
        const int strength = (int(h.state.at(next + 16)) + h.state.at(next + 17) + h.state.at(next + 18)) / 3;
        const size_t t = team_at(buyer);
        const int before[4] = {h.state.at(t + 24), h.state.at(t + 25), h.state.at(t + 26), h.state.at(t + 27)};
        const int mean = (before[0] + before[1] + before[2] + before[3]) / 4;
        const int delta = std::clamp((strength - mean + 10) / 3, 1, 10);
        BorlandRandom draws(random.state());
        const int kind = native_sale_range(draws, 0, 1);
        const int add1 = native_sale_range(draws, delta - 1, delta + 1), add2 = native_sale_range(draws, delta - 1, delta + 1);
        int actual = -1;
        require(finish_native_sale(h, random, quote, true, &actual, r130) && actual == buyer && random.state() == draws.state(),
                "1.30 club sale: three buyer draws");
        std::printf("sale (club): buyer %d skills %d %d %d %d mean %d strength %d delta %d kind %d +%d +%d\n", buyer, before[0], before[1],
                    before[2], before[3], mean, strength, delta, kind, add1, add2);
        // by hand: (85 + 80 + 86 + 84) / 4 = 83, (68 - 83 + 10) / 3 = -1, clamped to 1
        require(buyer == 0 && before[0] == 85 && before[1] == 80 && before[2] == 86 && before[3] == 84 && mean == 83 && strength == 68 && delta == 1,
                "club sale inputs as recorded");
        require(h.state.at(t + 24 + size_t(kind)) == uint8_t(before[kind] + add1) && h.state.at(t + 26 + size_t(kind)) == uint8_t(before[2 + kind] + add2) &&
                    h.state.at(t + 25 - size_t(kind)) == before[1 - kind] && h.state.at(t + 27 - size_t(kind)) == before[3 - kind],
                "1.30 club sale boosts the buyer from its own mean");
    }
    // Own player on the transfer list: an offset far outside the team records makes 1.00 refuse
    // the sale; 1.30 does not read through it and sells.
    {
        const Bytes pool = read_file(native / "native-pool-sale-before.hok");
        HokFile h = decode_hok(pool);
        BorlandRandom random(797554904u);
        SaleQuote quote;
        require(begin_native_sale(h, tables, random, 4, 0, 32767, quote) && quote.bidder == 3, "pool quote");
        const Bytes untouched = h.state;
        bool refused = false;
        BorlandRandom r100 = random;
        try { (void)finish_native_sale(h, r100, quote, true); } catch (const FormatError&) { refused = true; }
        require(refused && h.state == untouched, "1.00 still refuses the unavailable buyer offset");
        const size_t row = row_at(4, 0), t = team_at(quote.bidder);
        const int strength = (int(h.state.at(row + 16)) + h.state.at(row + 17) + h.state.at(row + 18)) / 3;
        const int before[4] = {h.state.at(t + 24), h.state.at(t + 25), h.state.at(t + 26), h.state.at(t + 27)};
        const int mean = (before[0] + before[1] + before[2] + before[3]) / 4;
        const int delta = std::clamp((strength - mean + 10) / 3, 1, 10);
        BorlandRandom draws(random.state());
        const int kind = native_sale_range(draws, 0, 1);
        const int add1 = native_sale_range(draws, delta - 1, delta + 1), add2 = native_sale_range(draws, delta - 1, delta + 1);
        int actual = -1;
        require(finish_native_sale(h, random, quote, true, &actual, r130) && actual == 3 && random.state() == draws.state(),
                "1.30 pool sale ignores the leftover offset");
        std::printf("sale (pool): buyer 3 skills %d %d %d %d mean %d strength %d delta %d kind %d +%d +%d\n", before[0], before[1], before[2],
                    before[3], mean, strength, delta, kind, add1, add2);
        // by hand: (88 + 90 + 94 + 84) / 4 = 89, (70 - 89 + 10) / 3 = -3, clamped to 1
        require(before[0] == 88 && before[1] == 90 && before[2] == 94 && before[3] == 84 && mean == 89 && strength == 70 && delta == 1,
                "pool sale inputs as recorded");
        require(h.state.at(t + 24 + size_t(kind)) == uint8_t(before[kind] + add1) && h.state.at(t + 26 + size_t(kind)) == uint8_t(before[2 + kind] + add2),
                "1.30 pool sale boosts the buyer from its own mean");
    }
}

// Game picks up the version of its EXE; the world generator re-rolls without the energy byte.
static void game_and_world(const Bytes& exe100, const Bytes& exe130, const fs::path& data) {
    const Bytes db = read_file(data / "KB" / "23.vga");
    WorldSettings s;
    s.managers = {{"Tester", 0, 5}};
    WorldSeeds seeds;
    const WorldResult w100 = generate_world(exe100, db, s, seeds), w130 = generate_world(exe130, db, s, seeds);
    Game g100(exe100, encode_hok(w100.save)), g130(exe130, encode_hok(w130.save));
    require(!g100.rules().v130() && g130.rules().v130() && std::string(g130.rules().label()) == "V1.30", "Game reports the version of its EXE");
    require(g100.settings().version == "V1.00" && g130.settings().version == "V1.30", "settings page version text");
    // In 1.00 every team of the routine (clubs and nations, 0..63) leaves the generator with the
    // energy byte of its first defence line in 45..55; in 1.30 it keeps its previous value.
    int rolled100 = 0, rolled130 = 0;
    for (int t = 0; t < 64; t++) {
        const int e100 = w100.save.state.at(team_at(t) + 0x1E), e130 = w130.save.state.at(team_at(t) + 0x1E);
        rolled100 += e100 >= 45 && e100 <= 55;
        rolled130 += e130 >= 45 && e130 <= 55;
    }
    std::printf("world: energy byte in 45..55 for %d teams (1.00) and %d teams (1.30)\n", rolled100, rolled130);
    require(rolled100 == 64, "1.00 world: the rating writes the energy byte of all 64 teams");
    require(rolled130 < 64, "1.30 world: the rating leaves the energy byte alone");
}

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 4) throw std::runtime_error("rules_tests DATA EXE130 [SALE_SAVES]");
        const fs::path data = argv[1];
        const Bytes exe100 = read_file(data / "EM.EXE"), exe130 = read_file(argv[2]);
        detection(exe100, exe130);
        rating(exe100);
        attendance(exe100);
        if (argc == 4 && fs::exists(fs::path(argv[3]) / "native-club-sale-before.hok")) sale(exe100, argv[3]);
        game_and_world(exe100, exe130, data);
        std::puts("rules tests passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED: %s\n", e.what());
        return 1;
    }
}
