// SPDX-License-Identifier: GPL-3.0-only
// Match display of the classic UI (ovl14 boards, minute panel, "Spiele laufen") against DOSBox-X captures of the original
// (work/exp/match_20261108), and the observer of the minute loop: the display must not change the day.
#include "ui/classic_match_view.hpp"
#include "ui/classic_app.hpp"
#include <SDL.h>
#include "ui/session.hpp"
#include "ui/classic_cup.hpp"
#include "game/calendar.hpp"

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

int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("classic_match_view_tests DATA REFERENCE GOLDENS OUTPUT");
        const fs::path data = path_from_text(argv[1]), references = path_from_text(argv[2]), goldens = path_from_text(argv[3]),
                       output = path_from_text(argv[4]);
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
        if (failures) throw std::runtime_error(std::to_string(failures) + " raster comparisons failed");
        std::printf("classic match view: ok\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Classic match view: %s\n", e.what());
        return 1;
    }
}
