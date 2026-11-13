// SPDX-License-Identifier: GPL-3.0-only
// Statistiken pages (Frueher, Verein, Spieler, Fairplay, WM) and Paarungen -> EuroCup: view logic with values
// the original shows for MONAT1 and MORNPOST, and raster comparison with reference images of the original EM.EXE.
//
// Usage: classic_stats_tests DATA REFERENCE GOLDENS (REFERENCE holds MONAT1.HOK and MORNPOST.HOK)
//   EM_STATS_ACTUAL=<folder> writes every rendered scene as raw RGB (<scene>-<save>.actual.rgb).
//   Exit code 1 if a check or a picture fails, 2 on an error.
#include "ui/classic_app.hpp"
#include "ui/classic_stats_club.hpp"
#include "ui/classic_stats_fairplay.hpp"
#include "ui/classic_stats_history.hpp"
#include "ui/classic_stats_players.hpp"
#include "ui/classic_stats_wm.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace em;
using namespace em::ui;

namespace {
// Number of failed checks and pictures.
int failures = 0;
// Reports and counts a failed check without stopping the test.
void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("classic_stats_tests DATA REFERENCE GOLDENS");
        const auto data = path_from_text(argv[1]), references = path_from_text(argv[2]), goldens = path_from_text(argv[3]);
        const char* dump = std::getenv("EM_STATS_ACTUAL");  // optional: write the rendered RGB there

        // Logic, values read from the native screens of MONAT1 / MORNPOST.
        {
            const HokFile h = decode_hok(read_file(references / "MONAT1.HOK"));
            const auto view = classic_stats_players_view(h);
            check(view.skaters.size() == 12 && view.keepers.size() == 3, "player statistics row counts");
            if (!view.skaters.empty()) check(view.skaters[0].native_score == "15(%8/%7)", "first scorer score");
            if (view.keepers.size() == 3) check(view.keepers[2].color == 24, "own keeper highlighted");
            check(!view.empty_notice, "no empty notice with scorers");
            for (const auto& row : classic_stats_players_view(h, 2, true).skaters) check(row.color == 24, "manager filter keeps managed clubs only");
            check(classic_stats_players_button(60, 186) == 0 && classic_stats_players_button(220, 200) == 4, "player statistics buttons");
            const auto fair = classic_stats_fairplay_order(h);
            check(h.teams.at(size_t(fair[0])).name.rfind("EISB", 0) == 0 && h.teams.at(size_t(fair[11])).name.rfind("MANNHEIM", 0) == 0, "fair play order");
            check(classic_stats_club_years(h) == 0, "first career year");
            check(classic_stats_wm_view(h).score[0] == 0, "no WM results in 1993");
            check(classic_stats_history_scroll(169, 33) == -1 && classic_stats_history_scroll(169, 42) == 1, "history arrows");
            check(classic_stats_wm_team(60, 160) == 52 && classic_stats_wm_team(290, 200) == 63, "WM boxes");
        }
        {
            const HokFile h = decode_hok(read_file(references / "MORNPOST.HOK"));
            check(classic_stats_club_years(h) == 50, "career history capped at fifty years");
            const auto wm = classic_stats_wm_view(h);
            check(wm.score[size_t(wm.order[0])] == 122, "WM leader 122 points");
            check(!classic_stats_players_view(h).empty_notice, "empty squad keeps the native local row value");
        }

        // Reference images of the original (GOLDENS/<scene>-<save>.rgb); the 6-bit VGA colours are scaled to
        // 8 bits as (v<<2)|(v>>4). In the Frueher, WM and Fairplay references the pointer sits one pixel above
        // the click position, which the mouse positions below reproduce.
        // One scene: screen name for render_screen, pointer position and the save to load.
        struct Scene { const char* name; int mouse_x, mouse_y; const char* save; };
        constexpr std::array<Scene, 11> scenes{{
            {"stats-history", 80, 218, "MONAT1"},
            {"stats-history", 80, 218, "MORNPOST"},
            {"stats-club", 150, 237, "MONAT1"},
            {"stats-club", 150, 237, "MORNPOST"},
            {"stats-wm", 319, 193, "MONAT1"},
            {"stats-wm", 293, 218, "MORNPOST"},
            {"stats-fairplay", 240, 218, "MONAT1"},
            {"stats-fairplay", 240, 218, "MORNPOST"},
            {"stats-players", 187, 219, "MONAT1"},
            {"stats-players", 187, 219, "MORNPOST"},
            {"euro-fixtures1", 216, 236, "MONAT1"},
        }};
        for (const auto& scene : scenes) {
            ClassicApplication app(data, goldens / "unused-save-output", references / (std::string(scene.save) + ".HOK"));
            app.motion(scene.mouse_x, scene.mouse_y);
            app.render_screen(scene.name);
            app.draw();
            std::string stem = scene.save;
            for (char& c : stem) c = char(std::tolower(static_cast<unsigned char>(c)));
            const auto expected = read_file(goldens / (std::string(scene.name) + "-" + stem + ".rgb"));
            if (expected.size() != 320 * 240 * 3) throw std::runtime_error("Invalid reference RGB size");
            const auto actual = app.canvas().argb();
            size_t differences = 0, first = 0;
            Bytes rgb(320 * 240 * 3);
            for (size_t pixel = 0; pixel < actual.size(); ++pixel) {
                rgb[pixel * 3] = uint8_t(actual[pixel] >> 16); rgb[pixel * 3 + 1] = uint8_t(actual[pixel] >> 8); rgb[pixel * 3 + 2] = uint8_t(actual[pixel]);
                const uint32_t want = uint32_t(expected[pixel * 3]) << 16 | uint32_t(expected[pixel * 3 + 1]) << 8 | expected[pixel * 3 + 2];
                if ((actual[pixel] & 0xffffff) != want) { if (!differences) first = pixel; ++differences; }
            }
            if (dump) {
                std::ofstream out(path_from_text(dump) / (std::string(scene.name) + "-" + stem + ".actual.rgb"), std::ios::binary);
                out.write(reinterpret_cast<const char*>(rgb.data()), std::streamsize(rgb.size()));
            }
            if (differences) {
                std::fprintf(stderr, "%s-%s: %zu pixel differences, first at %zu,%zu\n", scene.name, stem.c_str(), differences, first % 320, first / 320);
                ++failures;
            } else std::printf("%s-%s: 76800/76800 pixels match the original\n", scene.name, stem.c_str());
        }
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "classic_stats_tests: %s\n", e.what());
        return 2;
    }
}
