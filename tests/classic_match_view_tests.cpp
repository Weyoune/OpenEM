// SPDX-License-Identifier: GPL-3.0-only
// Match display of the classic UI (ovl14 boards, minute panel, "Spiele laufen") against DOSBox-X captures of the original
// (work/exp/match_20261108), and the observer of the minute loop: the display must not change the day.
#include "ui/classic_match_view.hpp"
#include "ui/classic_league_fixtures.hpp"
#include "game/schedule.hpp"
#include "ui/classic_app.hpp"
#include <SDL.h>
#include "ui/session.hpp"
#include "ui/classic_cup.hpp"
#include "game/calendar.hpp"
#include "game/lineup.hpp"
#include "game/match.hpp"
#include "game/scorers.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// 2115:019E units (a third of a BIOS tick) in ms, as classic_board_message_steps counts them.
static int units(int n) { return n * 54925 / 3000; }

// Compares with a raw 320 x 240 RGB golden; on a difference writes the render and a difference map next to OUTPUT.
static size_t differences(const ClassicCanvas& canvas, const fs::path& golden, const fs::path& output, const std::string& name) {
    const auto expected = read_file(golden);
    require(expected.size() == 320 * 240 * 3, "golden size " + name);
    const auto actual = canvas.argb();
    size_t count = 0;
    int x0 = 320, y0 = 240, x1 = -1, y1 = -1;
    Bytes map(320 * 240 * 3, 0), mine(320 * 240 * 3, 0);
    for (size_t p = 0; p < actual.size(); p++) {
        const uint32_t rgb = uint32_t(expected[p * 3]) << 16 | uint32_t(expected[p * 3 + 1]) << 8 | expected[p * 3 + 2];
        mine[p * 3] = uint8_t(actual[p] >> 16); mine[p * 3 + 1] = uint8_t(actual[p] >> 8); mine[p * 3 + 2] = uint8_t(actual[p]);
        if ((actual[p] & 0xFFFFFF) != rgb) {
            count++;
            const int x = int(p % 320), y = int(p / 320);
            x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
            map[p * 3] = 255;
        } else {
            map[p * 3] = map[p * 3 + 1] = map[p * 3 + 2] = uint8_t((expected[p * 3] + expected[p * 3 + 1] + expected[p * 3 + 2]) / 9);
        }
    }
    if (count) {
        fs::create_directories(output);
        std::FILE* f = std::fopen((output / (name + ".mine.rgb")).string().c_str(), "wb");
        if (f) { std::fwrite(mine.data(), 1, mine.size(), f); std::fclose(f); }
        f = std::fopen((output / (name + ".diff.rgb")).string().c_str(), "wb");
        if (f) { std::fwrite(map.data(), 1, map.size(), f); std::fclose(f); }
        std::fprintf(stderr, "%s: %zu pixels differ in %d,%d..%d,%d\n", name.c_str(), count, x0, y0, x1, y1);
    }
    return count;
}

// Compares the rows y0..239 with the golden (the goal flash captures show a board whose live values are not modelled here).
static size_t differences_below(const ClassicCanvas& canvas, const fs::path& golden, int y0, const std::string& name) {
    const auto expected = read_file(golden);
    require(expected.size() == 320 * 240 * 3, "golden size " + name);
    const auto actual = canvas.argb();
    size_t count = 0;
    int x0 = 320, y1 = -1, x1 = -1, top = 240;
    for (size_t p = size_t(y0) * 320; p < actual.size(); p++) {
        const uint32_t rgb = uint32_t(expected[p * 3]) << 16 | uint32_t(expected[p * 3 + 1]) << 8 | expected[p * 3 + 2];
        if ((actual[p] & 0xFFFFFF) == rgb) continue;
        count++;
        const int x = int(p % 320), y = int(p / 320);
        x0 = std::min(x0, x); x1 = std::max(x1, x); top = std::min(top, y); y1 = std::max(y1, y);
    }
    if (count) std::fprintf(stderr, "%s: %zu pixels differ in %d,%d..%d,%d\n", name.c_str(), count, x0, top, x1, y1);
    return count;
}

// Development aid: EM_MATCH_DUMP="name x y w h" prints the golden (G) and the render (M) as palette indices.
static void dump(const ClassicCanvas& canvas, const fs::path& goldens, const std::string& name) {
    const char* spec = std::getenv("EM_MATCH_DUMP");
    if (!spec) return;
    char which[64] = {};
    int x = 0, y = 0, w = 0, h = 0;
    if (std::sscanf(spec, "%63s %d %d %d %d", which, &x, &y, &w, &h) != 5 || name != which) return;
    const auto expected = read_file(goldens / (name + ".rgb"));
    const auto& pal = canvas.palette();
    auto index = [&](size_t p) {
        for (int i = 0; i < 256; i++)
            if (pal.rgb[size_t(i)][0] == expected[p * 3] && pal.rgb[size_t(i)][1] == expected[p * 3 + 1] && pal.rgb[size_t(i)][2] == expected[p * 3 + 2]) return i;
        return -1;
    };
    for (int row = y; row < y + h; row++) {
        std::printf("%3d G", row);
        for (int col = x; col < x + w; col++) std::printf(" %02x", index(size_t(row) * 320 + size_t(col)) & 0xFF);
        std::printf("\n    M");
        for (int col = x; col < x + w; col++) std::printf(" %02x", canvas.pixels().pixels[size_t(row) * 320 + size_t(col)]);
        std::printf("\n");
    }
}

// Development aid: EM_MATCH_FIT="name boards" prints the bar values a golden shows (energy, effort, offence, lamps).
static void fit(const ClassicCanvas& canvas, const fs::path& goldens, const std::string& name) {
    const char* spec = std::getenv("EM_MATCH_FIT");
    if (!spec) return;
    char which[64] = {};
    int boards = 1;
    if (std::sscanf(spec, "%63s %d", which, &boards) != 2 || name != which) return;
    const auto expected = read_file(goldens / (name + ".rgb"));
    const auto& pal = canvas.palette();
    auto index = [&](int x, int y) {
        const size_t p = size_t(y) * 320 + size_t(x);
        for (int i = 0; i < 256; i++)
            if (pal.rgb[size_t(i)][0] == expected[p * 3] && pal.rgb[size_t(i)][1] == expected[p * 3 + 1] && pal.rgb[size_t(i)][2] == expected[p * 3 + 2]) return i;
        return -1;
    };
    for (int i = 0; i < boards; i++) {
        const int y = i * 45 + 0xF;
        for (int side = 0; side < 2; side++) {
            const int x = 0xD + side * 0xE3;
            std::printf("board %d side %d:", i, side);
            for (int kind = 0; kind < 2; kind++) {
                int n = 0;
                for (int yy = y; yy <= y + 25; yy++) n += index(x + kind * 0x18 + 2, yy) == 0x18;
                int lit = -1;
                for (int k = 0; k < 4; k++) if (index(x + kind * 8 + 8 + 2, y + k * 7 - 1 + 3) != 0x10) lit = k;
                std::printf(" kind%d energy-bar %d line %d;", kind, n, lit);
            }
            for (int row = 0; row < 2; row++) {
                int n = 0;
                for (int xx = x + 0x27; xx <= x + 0x27 + 25; xx++) n += index(xx, y + 9 + row * 7) == 0x18;
                std::printf(" hbar%d %d", row, n - 1);
            }
            std::printf("\n");
        }
    }
}

static ClassicBoard board(const char* home, int home_crest, const char* away, int away_crest) {
    ClassicBoard b;
    b.team[0].name = home; b.team[0].crest = home_crest;
    b.team[1].name = away; b.team[1].crest = away_crest;
    return b;
}
// period goals home/away for the three periods; the total follows
static void set_score(ClassicBoard& b, std::array<int, 6> periods) {
    for (int p = 0; p < 3; p++) for (int s = 0; s < 2; s++) { b.period_goals[p][s] = periods[size_t(p * 2 + s)]; b.goals[s] += b.period_goals[p][s]; }
}
static void set_team(ClassicBoardTeam& t, std::array<int, 3> chances, int penalty_minutes, int line0, int line1, int energy0, int energy1,
                     int effort, int offence, int skaters = 5, int penalty_code = 0) {
    for (int p = 0; p < 3; p++) t.chances[p] = chances[size_t(p)];
    t.penalty_minutes = penalty_minutes;
    t.line[0] = line0; t.line[1] = line1;
    t.energy[0] = energy0; t.energy[1] = energy1;
    t.effort = effort; t.offence = offence;
    t.skaters = skaters; t.penalty_code = penalty_code;
}

// Development aid: EM_SCENE_SEARCH="capture.rgb[;arena]" finds the TORE\ script and frame of a goal scene capture (rows
// 0..199, START colours of EHC Freiburg / Kölner EC) and prints the best candidates.
static void scene_search(const fs::path& data) {
    const char* env = std::getenv("EM_SCENE_SEARCH");
    if (!env) return;
    std::string arg = env;
    int arena = 2;
    std::string only;
    if (auto bar = arg.find('|'); bar != std::string::npos) { only = arg.substr(bar + 1); arg = arg.substr(0, bar); }
    if (auto semi = arg.find(';'); semi != std::string::npos) { arena = std::stoi(arg.substr(semi + 1)); arg = arg.substr(0, semi); }
    const auto expected = read_file(arg);
    ClassicCanvas canvas(data);
    struct Hit { size_t diff; std::string script; int frame; };
    std::vector<Hit> hits;
    for (const auto& e : fs::directory_iterator(data / "TORE")) {
        const std::string name = e.path().filename().string();
        if (name == "anzahl" || (!only.empty() && name != only)) continue;
        const int n = classic_goal_scene_length(data, name);
        for (int k = 0; k <= n; k++) {
            ClassicGoalScene s;
            s.script = name; s.arena = arena; s.colour = {0x0EFF, 0x0F00}; s.frame = k;
            draw_classic_goal_scene(canvas, s);
            const auto argb = canvas.argb();
            size_t diff = 0;
            for (size_t p = 0; p < 320 * 200; p++) {
                const uint32_t rgb = uint32_t(expected[p * 3]) << 16 | uint32_t(expected[p * 3 + 1]) << 8 | expected[p * 3 + 2];
                if ((argb[p] & 0xFFFFFF) != rgb) diff++;
            }
            hits.push_back({diff, name, k});
        }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.diff < b.diff; });
    for (size_t i = 0; i < 6 && i < hits.size(); i++) std::printf("%zu %s %d\n", hits[i].diff, hits[i].script.c_str(), hits[i].frame);
    std::exit(0);
}

int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("classic_match_view_tests DATA REFERENCE GOLDENS OUTPUT");
        const fs::path data = path_from_text(argv[1]), references = path_from_text(argv[2]), goldens = path_from_text(argv[3]),
                       output = path_from_text(argv[4]);
        scene_search(data);
        int failures = 0;
        auto check = [&](ClassicCanvas& canvas, const std::string& name) {
            if (!fs::exists(goldens / (name + ".rgb"))) { std::fprintf(stderr, "missing golden %s\n", name.c_str()); failures++; return; }
            dump(canvas, goldens, name);
            fit(canvas, goldens, name);
            if (differences(canvas, goldens / (name + ".rgb"), output, name)) failures++;
        };
        {
            // START, Saturday 11.09.1993: EHC Freiburg - Kölner EC after minute 24 (DOSBox capture dosbox/board-a.png)
            ClassicCanvas canvas(data);
            ClassicMatchFrame f;
            f.minute = 24;
            ClassicBoard b = board("EHC FREIBURG", 86, "KÖLNER EC", 92);
            set_score(b, {2, 2, 0, 0, 0, 0});
            set_team(b.team[0], {3, 1, 0}, 4, 2, 2, 80, 86, 80, 4);
            set_team(b.team[1], {5, 0, 0}, 10, 2, 2, 86, 86, 80, 4);
            f.boards.push_back(b);
            draw_classic_match_view(canvas, f);
            canvas.cursor(319, 238);
            check(canvas, "board-a");
        }
        {
            ClassicCanvas canvas(data);
            ClassicMatchFrame f;
            // CAM.HOK = START with reporting mode 2 for EC Hedos München, Düsseldorfer EG and EC Kassel: four boards after minute 7
            f.minute = 7;
            ClassicBoard b0 = board("EC HEDOS MÜNCHEN", 89, "MANNHEIMER ERC", 93);
            set_score(b0, {0, 1, 0, 0, 0, 0});
            set_team(b0.team[0], {0, 0, 0}, 4, 1, 1, 78, 82, 88, 7, 4, 1);
            set_team(b0.team[1], {1, 0, 0}, 4, 1, 1, 82, 80, 60, 6);
            ClassicBoard b1 = board("SB ROSENHEIM", 106, "DÜSSELDORFER EG", 80);
            set_team(b1.team[0], {0, 0, 0}, 4, 1, 1, 82, 82, 52, 4);
            set_team(b1.team[1], {0, 0, 0}, 7, 1, 1, 76, 84, 52, 6, 4, 1);
            ClassicBoard b2 = board("EHC FREIBURG", 86, "KÖLNER EC", 92);
            set_score(b2, {1, 0, 0, 0, 0, 0});
            set_team(b2.team[0], {3, 0, 0}, 2, 1, 1, 78, 82, 80, 4, 4, 2);
            set_team(b2.team[1], {2, 0, 0}, 2, 1, 1, 82, 80, 80, 4, 4, 1);
            ClassicBoard b3 = board("ERC SCHWENNINGEN", 94, "EC KASSEL", 119);
            set_score(b3, {2, 0, 0, 0, 0, 0});
            set_team(b3.team[0], {2, 0, 0}, 2, 1, 1, 84, 82, 60, 5);
            set_team(b3.team[1], {1, 0, 0}, 0, 1, 1, 86, 88, 60, 4);
            f.boards = {b0, b1, b2, b3};
            draw_classic_match_view(canvas, f);
            canvas.cursor(319, 238);
            check(canvas, "boards-4");
        }
        {
            // CUPSHRP continued: Kölner EC - Düsseldorfer EG on 04.10.1993 after minute 35, sold out (7.500, colour 0x10)
            ClassicCanvas canvas(data);
            ClassicMatchFrame f;
            f.minute = 35;
            ClassicBoard b = board("KÖLNER EC", 92, "DÜSSELDORFER EG", 80);
            set_score(b, {1, 0, 1, 0, 0, 0});
            set_team(b.team[0], {5, 4, 0}, 4, 1, 2, 78, 70, 60, 6);
            set_team(b.team[1], {1, 1, 0}, 7, 2, 1, 64, 64, 60, 7);
            b.attendance = 7500;
            b.sold_out = true;
            f.boards.push_back(b);
            draw_classic_match_view(canvas, f);
            canvas.cursor(319, 238);
            check(canvas, "board-home");
        }
        {
            // 20.10.1993, DEB-Pokal Achtelfinale without the human club and without a reported club: "Spiele laufen", minute 14
            ClassicCanvas canvas(data);
            ClassicMatchFrame f;
            f.minute = 14;
            draw_classic_match_view(canvas, f);
            canvas.cursor(319, 238);
            check(canvas, "games-running");
        }
        {
            // ovl07_0eba(7, 0, 20): START with the switch Drittelpause of the Bundesliga, the Paarungen page after minute 20
            // (dosbox-automation capture 10.11.2026, dosbox/live-period20.png)
            const HokFile start = decode_hok(read_file(references / "START.HOK"));
            const SeasonCalendar calendar = load_season_calendar(read_file(data / "EM.EXE"));
            const ScheduleTables tables = load_schedule_tables(read_file(data / "EM.EXE"));
            const ClassicLeagueFixtures view = classic_league_fixtures(start, tables, calendar, 0);
            const int goals[6][2] = {{1, 0}, {0, 2}, {1, 0}, {0, 3}, {2, 0}, {1, 0}};
            std::vector<std::array<int, 4>> scores;
            for (size_t i = 0; i < 6; i++) scores.push_back({view.rows[i].home, view.rows[i].away, goals[i][0], goals[i][1]});
            ClassicCanvas canvas(data);
            draw_classic_league_live(canvas, start, tables, calendar, 0, 20, scores);
            canvas.cursor(319, 239);
            check(canvas, "live-period20");
        }
        {
            // the cup table after minute 60 and 65 of the same day (CUPSHRP holds the draw of the Achtelfinale, game 1)
            const HokFile before = decode_hok(read_file(references / "CUPSHRP.HOK"));
            const SeasonCalendar calendar = load_season_calendar(read_file(data / "EM.EXE"));
            const ClassicCupView view = classic_cup(before, calendar);
            const std::vector<std::pair<std::string, std::array<int, 2>>> results{
                {"EC HEDOS", {8, 0}}, {"SC MEMMINGEN", {1, 3}}, {"ECD SAUERLAND", {1, 5}}, {"EHC 80", {3, 8}}, {"EC HANNOVER", {1, 1}},
                {"EA KEMPTEN", {1, 8}}, {"ERC SCHWENNINGEN", {7, 2}}, {"AUGSBURGER EV", {2, 6}}};
            std::vector<ClassicCupLive> live;
            for (const auto& group : view.groups)
                for (const auto& row : group.rows)
                    for (const auto& [name, goals] : results)
                        if (row.native_home.rfind(name, 0) == 0) live.push_back({row.home, row.away, {goals[0], goals[1]}, 0});
            require(live.size() == 8, "eight cup pairs");
            ClassicCanvas canvas(data);
            draw_classic_cup_minute(canvas, before, calendar, 60, live);
            canvas.cursor(319, 238);
            check(canvas, "cup-minute-60");
            for (auto& m : live) if (m.goals[0] == 1 && m.goals[1] == 1) { m.goals[0] = 2; m.markers = 1; }
            ClassicCanvas later(data);
            draw_classic_cup_minute(later, before, calendar, 65, live);
            later.cursor(160, 44);
            check(later, "cup-minute-65");
        }
        {
            // Goal flash of FUN_162c_23c4 (dosbox-automation capture of a new game, 2. Spieltag 13.09.1993, minute 1: the own
            // EC Hedos München - EHC Freiburg on a board, every other club with reporting mode 1): EV Landsberg - Augsburger EV
            // 0:1, first the names (goal-flash-teams), after delay(50) with the score (goal-flash-score). Compared from the
            // message panel row downwards; the board above holds live values of the capture.
            for (const bool with_score : {false, true}) {
                ClassicCanvas canvas(data);
                ClassicMatchFrame f;
                f.minute = 1;
                f.boards.push_back(board("EC HEDOS MÜNCHEN", 89, "EHC FREIBURG", 86));
                f.flash = ClassicGoalFlash{"EV LANDSBERG", "AUGSBURGER EV", {0, 1}, with_score};
                draw_classic_match_view(canvas, f);
                const std::string name = with_score ? "goal-flash-score" : "goal-flash-teams";
                dump(canvas, goldens, name);
                if (!fs::exists(goldens / (name + ".rgb"))) { std::fprintf(stderr, "missing golden %s\n", name.c_str()); failures++; continue; }
                if (differences_below(canvas, goldens / (name + ".rgb"), 0xB4, name)) failures++;
            }
            require(classic_match_frame_ms(ClassicMatchFrame{}, 70) == classic_match_minute_ms(70), "a minute frame lasts a minute");
            ClassicMatchFrame flash;
            flash.flash = ClassicGoalFlash{};
            require(classic_match_frame_ms(flash, 0) == 878, "delay(50): 16 BIOS ticks with the names");
            flash.flash->score = true;
            require(classic_match_frame_ms(flash, 70) == 1263, "delay(70): 23 BIOS ticks with the score");
        }
        {
            // Goal scenes of ovl14_4938 (dosbox-automation capture 09.11.2026: START with Torszenen on, EHC Freiburg - Kölner
            // EC, arena 2 = KB243). Script and frame of each capture were found by search (EM_SCENE_SEARCH); the whole screen
            // is compared: rink window, objects, palette KB247 with the shirt colours, panel KB246 with the texts.
            struct Case { const char* name; const char* script; int frame, minute, home, away; };
            const std::vector<Case> cases{
                {"scene-5at-08", "5.at", 8, 16, 3, 2},   {"scene-5at-38", "5.at", 38, 16, 3, 2},
                {"scene-5at-80", "5.at", 80, 16, 3, 2},  {"scene-5at-final", "5.at", 80, 16, 3, 3},
                {"scene-4ht-55", "4.ht", 55, 13, 2, 2},  {"scene-6ht-60", "6.ht", 60, 19, 3, 3},
                {"scene-2hv-104", "2.hv", 104, 22, 4, 3},
            };
            for (const Case& k : cases) {
                ClassicCanvas canvas(data);
                ClassicMatchFrame f;
                f.scene = ClassicGoalScene{};
                ClassicGoalScene& s = *f.scene;
                s.script = k.script; s.arena = 2; s.colour = {0x0EFF, 0x0F00};
                s.names = {"EHC FREIBURG", "KÖLNER EC"}; s.goals = {k.home, k.away}; s.info = "AUSWÄRTS";
                s.minute = k.minute; s.frame = k.frame;
                draw_classic_match_view(canvas, f);
                check(canvas, k.name);
            }
            // Stufe 2 "Eisfeld bleibt an" (same capture session, Torszenen level 2): the rink opened at camera (0, 0) with an
            // empty attendance line in minute 11, and the view of 7.ht kept after the scene in minute 14 at 1:0.
            for (int k = 0; k < 2; k++) {
                ClassicCanvas canvas(data);
                ClassicMatchFrame f;
                f.scene = ClassicGoalScene{};
                ClassicGoalScene& s = *f.scene;
                s.arena = 2; s.colour = {0x0EFF, 0x0F00}; s.names = {"EHC FREIBURG", "KÖLNER EC"};
                if (k == 0) { s.minute = 11; }
                else { s.script = "7.ht"; s.frame = 120; s.goals = {1, 0}; s.info = "AUSWÄRTS"; s.minute = 14; }
                draw_classic_match_view(canvas, f);
                check(canvas, k == 0 ? "scene-level2-start" : "scene-level2-rest");
            }
        }
        {
            // Board messages of ovl14_0690 (dosbox-automation capture 09.11.2026: START, speed 60 written to DS:0262, EHC
            // Freiburg - Kölner EC on the own board). Compared from the message panel row downwards like the goal flash.
            using Step = ClassicBoardMessage::Step;
            struct Case { const char* name; int minute; ClassicBoardMessage m; };
            const auto msg = [](int kind, const char* team, const char* player, const char* second, Step step, bool text, int lines) {
                ClassicBoardMessage m;
                m.kind = kind; m.team = team; m.player = player; m.second = second; m.step = step; m.text = text; m.lines = lines;
                return m;
            };
            const std::vector<Case> cases{
                {"board-msg-chance", 12, msg(1, "KÖLNER EC", "BRANDL", "SCHAEDLER", Step::Caption, true, 1)},
                {"board-msg-team", 12, msg(1, "KÖLNER EC", "BRANDL", "SCHAEDLER", Step::Team, true, 1)},
                {"board-msg-goal", 12, msg(1, "KÖLNER EC", "BRANDL", "SCHAEDLER", Step::Final, true, 2)},
                {"board-msg-missed", 13, msg(0, "EHC FREIBURG", "", "", Step::Result, true, 1)},
                {"board-msg-goal-cpu", 14, msg(1, "EHC FREIBURG", "", "", Step::Result, true, 1)},
                {"board-msg-goal-scorer", 30, msg(1, "KÖLNER EC", "DOBRYZINSKI", "STEFAN", Step::Final, true, 1)},
                {"board-msg-missed-names", 45, msg(0, "KÖLNER EC", "H.STEIGER", "HOHENADL", Step::Final, true, 2)},
            };
            for (const Case& k : cases) {
                ClassicCanvas canvas(data);
                ClassicMatchFrame f;
                f.minute = k.minute;
                f.boards.push_back(board("EHC FREIBURG", 86, "KÖLNER EC", 84));
                f.message = k.m;
                draw_classic_match_view(canvas, f);
                dump(canvas, goldens, k.name);
                if (!fs::exists(goldens / (std::string(k.name) + ".rgb"))) { std::fprintf(stderr, "missing golden %s\n", k.name); failures++; continue; }
                if (differences_below(canvas, goldens / (std::string(k.name) + ".rgb"), 0xB4, k.name)) failures++;
            }

            // The steps and their times (capture: "Chance:" 312 ms, with the club 459 ms, final lines 646 ms; computer club
            // "Vergeben" 607 ms).
            ClassicMatchFrame base;
            base.boards.push_back(board("A", 80, "B", 81));
            const auto goal = msg(1, "KÖLNER EC", "BRANDL", "SCHAEDLER", Step::Caption, true, 1);
            auto steps = classic_board_message_steps(base, goal, 60, false);
            require(steps.size() == 2 + 1 + 1 + 2, "speed 60: two caption blinks, club, one result, two final lines");
            require(!steps[0].message->text && steps[1].message->text, "the caption blinks in after a cleared step");
            require(steps[1].hold_ms == units(2) + units(20), "caption: delay(2) and delay(80 - speed)");
            require(steps[2].message->step == Step::Team && steps[2].hold_ms == units(25), "the club name stays delay(25)");
            require(steps[3].message->step == Step::Result && steps[3].message->text && steps[3].hold_ms == units(1), "TOR! once (its first lip-sync entry is a vowel)");
            require(steps[4].message->lines == 1 && steps[5].message->lines == 2 && steps[5].hold_ms == units(35), "final lines delay(35)");
            auto cpu = goal; cpu.player.clear(); cpu.second.clear(); cpu.kind = 0;
            steps = classic_board_message_steps(base, cpu, 60, false);
            require(steps.size() == 4 && steps.back().message->step == Step::Result && steps.back().hold_ms == units(1) + units(35),
                    "a computer club ends with the font 1 result");
            steps = classic_board_message_steps(base, goal, 50, false);
            require(steps.size() == 4 + 1 + 4 + 2 && !steps[8].message->text, "speed 50: four blinks each, the result ends cleared");
            require(steps[0].hold_ms == units(5 + 2) && steps[3].hold_ms == units(5 + 2) + units(4) + units(30), "speed 50 waits 5 more");
            steps = classic_board_message_steps(base, goal, 60, true);
            require(steps.size() == 2 && steps[0].message->step == Step::Final, "with goal scenes only the final lines");
            require(classic_board_message_steps(base, goal, 70, false).empty(), "no message at speed 70");
            require(classic_match_frame_ms(steps[0], 60) == steps[0].hold_ms, "a message step keeps its time");

            // classic_board_messages: chances from the counters, goals from the score, names from the squad counters.
            auto minute = [](int m, int chances_home, int goals_home, int chances_away, int goals_away, int brandl_goals, int pen) {
                ClassicMatchFrame f;
                f.minute = std::max(1, m);
                ClassicBoard b = board("HOME", 80, "AWAY", 81);
                b.team[0].chances[0] = chances_home; b.goals[0] = goals_home;
                b.team[1].chances[0] = chances_away; b.goals[1] = goals_away;
                b.team[1].penalty_minutes = pen;
                b.team[0].scorers = {{"BRANDL", brandl_goals, 0}, {"SCHAEDLER", 0, brandl_goals}};
                f.boards.push_back(b);
                return f;
            };
            std::vector<ClassicMatchFrame> frames{minute(0, 0, 0, 0, 0, 0, 0), minute(1, 1, 1, 1, 0, 1, 2), minute(2, 1, 1, 1, 0, 1, 2)};
            const auto shown = classic_board_messages(frames, 60, false);
            std::vector<ClassicBoardMessage> finals;
            for (const auto& f : shown) if (f.message && f.message->step == Step::Caption && f.message->text) finals.push_back(*f.message);
            require(finals.size() == 3, "goal, miss and penalty in minute 1");
            require(finals[0].kind == 1 && finals[0].team == "HOME" && finals[0].player == "BRANDL" && finals[0].second == "SCHAEDLER",
                    "the home goal names scorer and assist");
            require(finals[1].kind == 2 && finals[1].team == "AWAY" && finals[2].kind == 0, "the away penalty, then the away chance");
            require(shown[1].minute == 1 && shown[1].message && !shown[1].boards.empty(), "the steps show minute 1");
            require(!shown.back().message && shown.back().minute == 2, "minute 2 without events");
            require(classic_board_messages(frames, 70, false).size() == frames.size(), "no messages at speed 70");
            {
                // Goal scenes with the same minute frames: every chance of the human board plays its scene before the
                // message, the score of the scene runs on within the minute, a goal adds one in the final frame.
                std::vector<ClassicMatchFrame> human = frames;
                for (auto& f : human) { f.boards[0].team[0].human = true; f.boards[0].team[0].colour = 0x0EFF; f.boards[0].team[1].colour = 0x0EFE; }
                const auto played = classic_board_messages(human, 70, true, data);
                std::vector<const ClassicGoalScene*> finals_of_scenes;
                for (size_t i = 0; i < played.size(); i++)
                    if (played[i].scene && played[i].hold_ms == units(6))
                        finals_of_scenes.push_back(&*played[i].scene);
                require(finals_of_scenes.size() == 2, "a scene for the home goal and the away chance");
                const ClassicGoalScene& g = *finals_of_scenes[0];
                require(g.script.find(".ht") != std::string::npos && g.goals[0] == 1 && g.goals[1] == 0, "home goal: .ht, 1:0 at its end");
                require(g.colour[1] == (0x0EFF ^ 0xFFF), "similar shirts: the away colour is inverted");
                require(g.info == "0 ZUSCHAUER", "the manager's home game shows the attendance");
                const ClassicGoalScene& a = *finals_of_scenes[1];
                require(a.script.find(".av") != std::string::npos && a.goals[0] == 1 && a.goals[1] == 0, "away miss: .av, still 1:0");
                const int n = classic_goal_scene_length(data, g.script);
                size_t first = 0;
                while (!played[first].scene) first++;
                require(played[first].scene->frame == 1 && played[first].hold_ms == 54 && played[first + size_t(n)].hold_ms == units(6),
                        "one tick per script frame, delay(6) at the end");
                require(played[first].scene->goals[0] == 0, "the scene starts with the score before the goal");
                // the chance is followed by delay(20) on the board; r / w replays it with the score before it, three
                // plays at most (ovl14_1dc7)
                size_t after = 0;
                while (!played[after].after_scene) after++;
                require(!played[after].scene && played[after].hold_ms == units(20) && !played[after].boards.empty(),
                        "delay(20) on the board after the scene");
                ClassicMatchFrame replay_point = played[after];
                for (int play = 2; play <= 3; play++) {
                    const auto replay = classic_goal_scene_replay(data, replay_point);
                    require(!replay.empty() && replay.front().scene->play == play && replay.front().scene->goals[0] == 0 &&
                                replay.front().scene->script != replay_point.after_scene->script,
                            "a replay starts again with the score before the goal and another scene");
                    replay_point = replay.back();
                }
                require(classic_goal_scene_replay(data, replay_point).empty(), "no fourth play");
                // Stufe 2: the rink stays; minute frames show the last scene view with the new minute, no boards
                const auto level2 = classic_board_messages(human, 60, 2, data);
                require(std::all_of(level2.begin(), level2.end(), [](const ClassicMatchFrame& f) { return f.scene && f.boards.empty() && !f.message; }),
                        "Stufe 2 shows only the rink");
                require(level2.front().scene->script.empty() && level2.back().minute == 2 && level2.back().scene->minute == 2 &&
                            level2.back().scene->goals[0] == 1 && level2.back().scene->final == false && !level2.back().scene->script.empty(),
                        "the rink starts at (0, 0) and keeps the last scene with the running minute and score");
                // j / e variants of ovl15_0143
                require(classic_goal_scene_script(7, 0, true, false, true) == "2.htj" && classic_goal_scene_script(7, 1, false, true, true) == "2.ave" &&
                            classic_goal_scene_script(7, 1, false, false, false) == "7.av",
                        "scene file names");
                require(classic_board_messages(human, 70, false, data).size() == human.size(), "no scenes when switched off");

                // ovl06_3856: clicks on the board of the manager's club (home of board 0) and the quick switch
                {
                    ClassicMatchFrame f = human[1];
                    f.boards[0].team[0].id = 7;
                    const auto effort = classic_board_click(f, 0x34 + 12, 0x18);
                    require(effort && effort->team == 7 && effort->effort == 48 && effort->offence < 0, "effort bar: (x - 0x34) * 100 / 25");
                    const auto offence = classic_board_click(f, 0x4D, 0x1F);
                    require(offence && offence->offence == 7 && offence->effort < 0, "offence bar: (x - 0x34) * 7 / 25");
                    const auto lamp = classic_board_click(f, 0x1E, 30);
                    require(lamp && lamp->line[1] == 2 && lamp->line[0] < 0, "attack lamp of line 3");
                    require(!classic_board_click(f, 0x1E, 0x0E + 21 + 2), "the fourth line only when it is staffed");
                    f.boards[0].team[0].fourth_line[1] = true;
                    require(classic_board_click(f, 0x1E, 0x0E + 21 + 2)->line[1] == 3, "the staffed fourth line");
                    require(!classic_board_click(f, 0x120, 0x18), "the computer club does not react");
                    const auto quick = classic_board_click(f, 10, 0xE8);
                    require(quick && quick->quick && quick->team < 0, "the quick switch bottom left");
                    require(classic_board_click(f, 0x70, 20)->squad, "the middle of the board opens the lineup");
                }

                // The scenes the simulation drew (MatchProgress::scenes) decide the files; the shots of a shoot-out
                // follow the chances with variant e at minute 120 and run the score on.
                std::vector<ClassicMatchFrame> drawn = human;
                drawn[1].boards[0].scenes = {{1, false, 9, 0, false}, {0, true, 2, 'j', false}, {0, true, 2, 'e', true}, {1, false, 2, 'e', true}};
                const auto with_drawn = classic_board_messages(drawn, 70, true, data);
                std::vector<const ClassicGoalScene*> drawn_finals;
                for (const auto& f : with_drawn) if (f.scene && f.hold_ms == units(6)) drawn_finals.push_back(&*f.scene);
                require(drawn_finals.size() == 4 && drawn_finals[0]->script == "2.htj" && drawn_finals[1]->script == "9.av" &&
                            drawn_finals[2]->script == "2.hte" && drawn_finals[3]->script == "2.ave",
                        "drawn scenes: home goal, away chance, then the shoot-out shots");
                require(drawn_finals[2]->minute == 120 && drawn_finals[2]->goals[0] == 2 && drawn_finals[3]->goals[0] == 2 &&
                            drawn_finals[3]->goals[1] == 0,
                        "shoot-out scenes at minute 120 with the running score");
            }
        }
        {
            // classic_goal_flashes: two stages per goal before the minute of the goal, home goals first, drawn over the previous
            // minute with the new minute; no flashes without a board ("Spiele laufen"); cup tables stay untouched.
            auto minute = [](int m, int home, int away, bool with_board) {
                ClassicMatchFrame f;
                f.minute = std::max(1, m);
                if (with_board) f.boards.push_back(board("A", 80, "B", 81));
                ClassicReportedMatch r;
                r.home = 5; r.away = 7; r.names[0] = "HOME"; r.names[1] = "AWAY"; r.goals[0] = home; r.goals[1] = away;
                f.reported.push_back(r);
                return f;
            };
            std::vector<ClassicMatchFrame> frames{minute(0, 0, 0, true), minute(1, 0, 0, true), minute(2, 1, 1, true), minute(3, 1, 1, true)};
            ClassicMatchFrame table = frames.back();
            table.cup_table = 60;
            frames.push_back(table);
            frames.push_back(minute(4, 1, 2, true));
            const auto shown = classic_goal_flashes(frames);
            require(shown.size() == 6 + 6, "three goals give six flash frames");
            require(!shown[1].flash && shown[1].minute == 1, "minute 1 unchanged");
            require(shown[2].flash && !shown[2].flash->score && shown[2].minute == 2 && shown[2].flash->home == "HOME", "names first, minute 2 shown");
            require(shown[2].flash->goals[0] == 1 && shown[2].flash->goals[1] == 0, "the home goal comes first");
            require(shown[3].flash && shown[3].flash->score, "then the score");
            require(shown[4].flash->goals[0] == 1 && shown[4].flash->goals[1] == 1 && shown[5].flash->score, "then the away goal");
            require(!shown[6].flash && shown[6].minute == 2, "the minute frame follows");
            require(shown[8].cup_table == 60, "the cup table stays");
            require(shown[9].flash && shown[9].minute == 4 && shown[9].flash->goals[1] == 2 && !shown[11].flash, "a flash after the cup table");
            std::vector<ClassicMatchFrame> running{minute(0, 0, 0, false), minute(1, 2, 0, false)};
            require(classic_goal_flashes(running).size() == 2, "no flash with \"Spiele laufen\"");
        }
        {
            // The frames list the reported matches without a board: START with every other club on mode 1 (headphones).
            Session s(data);
            s.load(references / "START.HOK", 7);
            for (int team = 0; team < 48; team++)
                if (team != s.game().human_team()) s.apply([&](Game& g) { g.set_team_reporting(team, 1); return ActionResult{}; });
            std::vector<ClassicMatchFrame> frames;
            s.set_match_observer([&](const Game& g, int minute, const std::vector<LiveMatch>& m) {
                frames.push_back(classic_match_frame(g, minute, m, g.match_attendance(), g.match_sold_out()));
            });
            for (int day = 0; day < 6 && frames.empty(); day++) require(s.advance_day(), "days advance");
            require(!frames.empty() && frames.back().boards.size() == 1, "the own match on the board");
            size_t goals = 0;
            for (const auto& r : frames.back().reported) goals += size_t(r.goals[0] + r.goals[1]);
            const auto shown = classic_goal_flashes(frames);
            require(goals > 0 && shown.size() == frames.size() + 2 * goals, "every reported goal flashes once");
        }
        {
            // The display only observes: the same days with and without the observer give byte-identical games (CAM.HOK-like
            // cameras are set in the copy so that several boards are built), and the observed frames follow the minute loop.
            Session plain(data), watched(data);
            plain.load(references / "START.HOK", 7);
            watched.load(references / "START.HOK", 7);
            for (int team : {0, 3, 13}) {
                plain.apply([&](Game& g) { g.set_team_reporting(team, 2); return ActionResult{}; });
                watched.apply([&](Game& g) { g.set_team_reporting(team, 2); return ActionResult{}; });
            }
            std::vector<ClassicMatchFrame> frames;
            int calls = 0, last_minute = -1;
            watched.set_match_observer([&](const Game& g, int minute, const std::vector<LiveMatch>& m) {
                calls++;
                require(minute == last_minute + 1, "minutes in order");
                last_minute = minute;
                frames.push_back(classic_match_frame(g, minute, m, g.match_attendance(), g.match_sold_out()));
            });
            for (int day = 0; day < 6; day++) {
                last_minute = -1;
                require(plain.advance_day() && watched.advance_day(), "days advance");
                require(plain.game().checkpoint_bytes() == watched.game().checkpoint_bytes(), "the observer leaves the day unchanged");
            }
            require(calls > 0 && !frames.empty(), "the league days were observed");
            size_t four = 0;
            for (const auto& f : frames) four += f.boards.size() == 4;
            require(four > 0, "four reported matches give four boards");
            const auto& end = frames.back();
            require(calls == 122 && end.minute == 60 && end.boards.size() == 3, "two league days of 61 observations; on the second two reported clubs meet");
            for (const auto& b : end.boards)
                for (int s = 0; s < 2; s++) {
                    int goals = 0, chances = 0;
                    for (int p = 0; p < 3; p++) { goals += b.period_goals[p][s]; chances += b.team[size_t(s)].chances[p]; }
                    require(goals == b.goals[s] && chances >= goals, "period goals add up and every goal was a chance");
                }
        }
        {
            // In the running program: the crest starts the day; after the banner Session::advance_day plays it and the
            // observed minutes are shown with the board of the own match (START: Kölner EC is reported, mode 2).
            ClassicApplication app(data, output / "app", references / "START.HOK");
            const int day = app.session().game().season_day();
            app.motion(292, 49); app.click(292, 49, false);
            app.tick();
            SDL_Delay(1000); app.tick();
            require(app.session().game().season_day() > day, "the day was played by advance_day");
            ClassicCanvas reference(data);
            ClassicMatchFrame f;
            f.boards.push_back(board("EHC FREIBURG", 86, "KÖLNER EC", 92));
            draw_classic_match_view(reference, f);
            app.draw();
            auto same = [&](int x, int y) { return app.canvas().pixels().pixels[size_t(y) * 320 + size_t(x)] == reference.pixels().pixels[size_t(y) * 320 + size_t(x)]; };
            require(same(4, 2) && same(150, 210) && same(0x60 + 19, 4 + 19), "the match display shows the board of the own match");
            const auto during = app.canvas().argb();
            SDL_Delay(200); app.tick(); app.draw();
            require(app.canvas().argb() != during, "the minutes advance");
            app.click(10, 10, true);  // a right click ends the replay
            app.tick(); app.draw();
            require(!same(4, 2) || !same(150, 210), "after the replay the menu returns");
        }
        {
            // MatchRun::idle_next_minute (space key, ovl06_2522): an idle minute counts but plays nothing and draws nothing
            const HokFile start = decode_hok(read_file(references / "START.HOK"));
            GameRandom rng(12345);
            MatchRun run(start.teams[6], start.teams[7], rng);
            const uint32_t before = rng.state();
            run.idle_next_minute();
            require(run.step(), "more minutes follow the idle one");
            const MatchProgress idle = run.progress();
            require(idle.minute == 1, "the idle minute counts");
            require(rng.state() == before && idle.score.home == 0 && idle.score.away == 0, "no draws and no goals in the idle minute");
            while (run.step()) {}
            require(run.progress().minute == 60, "the idle minute is one of the 60");
            // MatchRun::take_lineup without a manager squad (or without a change) does nothing and draws nothing
            GameRandom rng2(12345);
            MatchRun plain(start.teams[6], start.teams[7], rng2);
            plain.take_lineup();
            require(rng2.state() == 12345 && plain.progress().minute == 0, "take_lineup without a change is a no-op");
        }
        {
            // ovl06_161d: with the switch Tabelle of the Bundesliga (DS:021F, state 0xA51E) its table follows the match day
            // and waits for a right click; then the menu returns
            HokFile start = decode_hok(read_file(references / "START.HOK"));
            start.state.at(0xA51C + 2) = 1;
            fs::create_directories(output / "app-table");
            const fs::path save = output / "app-table" / "TABSTART.HOK";
            fs::remove(save);
            write_new_save(save, encode_hok(start));
            ClassicApplication app(data, output / "app-table", save);
            app.motion(292, 49); app.click(292, 49, false);
            app.tick();
            SDL_Delay(1000); app.tick();
            app.click(10, 10, true);  // ends the minutes
            app.tick(); app.tick();
            require(app.screen() == ClassicScreen::Table, "the table follows the match day");
            app.tick();
            require(app.screen() == ClassicScreen::Table, "the table waits");
            app.click(10, 10, true);
            app.tick();
            require(app.screen() == ClassicScreen::Main, "a right click returns to the menu (screen " + std::to_string(int(app.screen())) + ")");
        }
        {
            // Package 2 (11.11.2026): EuroCup / WM observer, message draws below speed 70, goal scene replays, lineup mode 5,
            // board messages for penalties and shoot-out shots
            const HokFile start = decode_hok(read_file(references / "START.HOK"));
            const HokTeam& home = start.teams[6];  // EHC Freiburg
            const HokTeam& away = start.teams[7];  // Kölner EC (reporting mode 2: a board)
            {
                // observed_league_match plays exactly the minutes of league_match; the observer sees 0..60
                GameRandom r1(777), r2(777);
                const MatchResult a = league_match(home, away, r1);
                int calls = 0, last = -1;
                const MatchMinuteObserver observer = [&](int minute, const std::vector<LiveMatch>& m) {
                    require(m.size() == 1 && m[0].home == 6 && m[0].away == 7 && minute == calls, "observer of the single match");
                    calls++; last = minute;
                };
                const MatchResult b = observed_league_match(home, away, r2, 0, nullptr, nullptr, nullptr, {}, &observer, 6, 7);
                require(a.home == b.home && a.away == b.away && r1.state() == r2.state(), "the observed match draws the same");
                require(calls == 61 && last == 60, "observer before minute 1 and after every minute");
            }
            {
                // ovl14_0690 below speed 70: every chance of a board match draws rand(0, 1) twice; at 70 nothing changes
                const auto play = [&](int speed, bool messages) {
                    g_match_speed = speed;
                    GameRandom r(4242);
                    MatchRun run(home, away, r);
                    run.set_messages(messages);
                    const MatchResult res = run.finish();
                    g_match_speed = 70;
                    return std::pair<uint32_t, int>{r.state(), res.home * 100 + res.away};
                };
                require(play(70, true) == play(70, false), "speed 70: no message draws");
                require(play(60, false) == play(70, false), "without DS:5A56 no message draws");
                require(play(60, true).first != play(70, false).first, "speed 60 with a board: the messages draw");
                GameRandom r(9);
                g_match_speed = 60;
                require(reporter_minute_draw(r, false) == -1 && r.state() == 9, "no reporter draw without display");
                const int value = reporter_minute_draw(r, true);
                require(value >= 0 && value <= 4 && r.state() != 9, "reporter draw rand(0, 4) with display");
                g_match_speed = 70;
                require(reporter_minute_draw(r, true) == -1, "no reporter draw at speed 70");
            }
            {
                // g_scene_replay: a replay draws the next scene from the Borland stream before the scorer pick
                g_goal_scenes = 1;
                const auto play = [&](bool replay, int* calls) {
                    BorlandRandom b(99);
                    GameRandom r(5);
                    r.borland = &b;
                    if (replay)
                        g_scene_replay = [calls](const SceneCall& c, const GoalScene&, int n) {
                            require(c.home && c.away && c.minute >= 1, "scene call with the teams");
                            (*calls)++;
                            return n == 1 ? 1 : 0;  // every chance is played twice
                        };
                    MatchRun run(home, away, r);
                    std::vector<GoalScene> seen;
                    for (bool more = true; more;) {  // step() returns false on the last minute it plays
                        more = run.step();
                        for (const GoalScene& s : run.progress().scenes) seen.push_back(s);
                    }
                    g_scene_replay = nullptr;
                    return std::pair<uint32_t, std::vector<GoalScene>>{b.state(), seen};
                };
                int calls = 0;
                const auto plain = play(false, nullptr);
                const auto replayed = play(true, &calls);
                g_goal_scenes = 0;
                require(!plain.second.empty() && calls == int(replayed.second.size()), "the hook sees every play");
                require(replayed.second.size() == 2 * plain.second.size() || plain.first != replayed.first,
                        "a replay draws a new scene");
                require(std::all_of(replayed.second.begin(), replayed.second.end(), [](const GoalScene& s) { return s.shown; }) &&
                            std::none_of(plain.second.begin(), plain.second.end(), [](const GoalScene& s) { return s.shown; }),
                        "scenes shown by the hook are marked");
            }
            {
                // lineup mode 5 (ovr033 arg_0 != 0): a line player taken out keeps his role, the reserve toggle does nothing,
                // a player without a role cannot come in
                HokFile h = decode_hok(read_file(references / "START.HOK"));
                const size_t base = 0x696D;
                int line_slot = -1, free_slot = -1;
                for (int s = 0; s < 30; s++) {
                    const uint8_t* r = h.state.data() + base + size_t(s) * 52;
                    if (!r[0x0F] || (r[9] & 3)) continue;
                    if (r[0x0A] && r[0x15] && (r[0x15] & 0x7F) <= 3 && line_slot < 0) line_slot = s;
                    if (!r[0x0A] && free_slot < 0) free_slot = s;
                }
                require(line_slot >= 0, "START has a line player");
                const auto at = [&](int s, int field) { return h.state.at(base + size_t(s) * 52 + size_t(field)); };
                manual_lineup(h, 0, size_t(line_slot), 0, ManualLineupAction::ReserveToggle, nullptr, nullptr, true);
                require(at(line_slot, 0x0A) && at(line_slot, 0x15), "mode 5: the reserve toggle leaves a line player");
                manual_lineup(h, 0, size_t(line_slot), 0, ManualLineupAction::Select, nullptr, nullptr, true);
                require(at(line_slot, 0x0A) == 1 && at(line_slot, 0x15) == 0, "mode 5: out of the line, the role stays");
                if (free_slot >= 0) {
                    manual_lineup(h, 0, size_t(free_slot), 1, ManualLineupAction::Select, nullptr, nullptr, true);
                    require(at(free_slot, 0x0A) == 0 && at(free_slot, 0x15) == 0, "mode 5: no new player");
                }
            }
            {
                // board messages: a two-minute penalty names the player and "wegen " + DS:05CC[reason]; a shoot-out shot of
                // the manager's club names its shooter (kind 6)
                ClassicMatchFrame f0;
                f0.minute = 5;
                f0.boards.push_back(board("EHC FREIBURG", 86, "KÖLNER EC", 92));
                ClassicMatchFrame f1 = f0;
                f1.minute = 6;
                ClassicBoardTeam& t = f1.boards[0].team[1];
                t.penalty_minutes += 2; t.penalized = "MEIER"; t.penalty_reason = 3;
                ClassicMatchFrame f2 = f1;
                f2.minute = 7;
                f2.boards[0].team[1].penalized.clear(); f2.boards[0].team[1].penalty_reason = -1;
                f2.boards[0].shots = {{1, true}};
                f2.boards[0].goals[1] += 1; f2.boards[0].team[1].shootout_goals = 1;
                f2.boards[0].team[1].credits.push_back({true, true, "BRANDL", ""});
                const auto out = classic_board_messages({f0, f1, f2}, 60, 0);
                bool penalty = false, shot = false;
                for (const ClassicMatchFrame& f : out) {
                    if (!f.message || f.message->step != ClassicBoardMessage::Step::Final) continue;
                    if (f.message->kind == 2 && f.message->player == "MEIER" && f.message->second == "wegen Haltens.") penalty = true;
                    if (f.message->kind == 6 && f.message->player == "BRANDL") shot = true;
                }
                require(penalty, "the penalty message names the player and the reason");
                require(shot, "the shoot-out shot message names the shooter");
            }
        }
        if (failures) throw std::runtime_error(std::to_string(failures) + " raster comparisons failed");
        std::printf("classic match view: ok\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Classic match view: %s\n", e.what());
        return 1;
    }
}
