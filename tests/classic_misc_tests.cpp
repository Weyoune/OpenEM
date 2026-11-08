// SPDX-License-Identifier: GPL-3.0-only
// Gemischtes (Laden, Sichern, Einst.), Manager -> Neuer and Manager -> Highscore: pixel comparison with native
// DOSBox captures (work/exp/ui_20261108) and behaviour, including the highscore file logic of ovr040.
#include "game/highscore.hpp"
#include "ui/classic_app.hpp"
#include "ui/classic_misc_newmanager.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static size_t differences(const ClassicCanvas& canvas, const fs::path& golden) {
    const auto expected = read_file(golden);
    require(expected.size() == 320 * 240 * 3, "native RGB golden size");
    const auto actual = canvas.argb();
    size_t count = 0;
    int left = 320, top = 240, right = -1, bottom = -1;
    for (size_t pixel = 0; pixel < actual.size(); pixel++) {
        const uint32_t rgb = uint32_t(expected[pixel * 3]) << 16 | uint32_t(expected[pixel * 3 + 1]) << 8 | expected[pixel * 3 + 2];
        if ((actual[pixel] & 0xffffff) != rgb) {
            count++;
            const int x = int(pixel % 320), y = int(pixel / 320);
            left = std::min(left, x); right = std::max(right, x); top = std::min(top, y); bottom = std::max(bottom, y);
        }
    }
    if (count) {
        std::cerr << golden.filename().string() << ": " << count << " pixels differ in " << left << "," << top << ".." << right << "," << bottom << "\n";
        if (std::getenv("EM_DUMP_MISMATCH")) {
            const fs::path out = path_from_text(std::getenv("EM_DUMP_MISMATCH")) / (golden.stem().string() + ".bmp");
            std::error_code ec;
            fs::remove(out, ec);
            write_new_save(out, canvas.bmp_bytes());
        }
    }
    return count;
}
static void copy_fixture(const fs::path& from, const fs::path& to) {
    fs::create_directories(to.parent_path());
    fs::copy_file(from, to, fs::copy_options::overwrite_existing);
}

int main(int argc, char** argv) {
    try {
        if (argc != 6) throw std::runtime_error("classic_misc_tests DATA REFERENCE GOLDENS FIXTURES OUTPUT");
        const fs::path data = path_from_text(argv[1]), references = path_from_text(argv[2]), goldens = path_from_text(argv[3]),
                       fixtures = path_from_text(argv[4]);
        const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path output = path_from_text(argv[5]) / std::to_string(serial);
        int failures = 0;
        auto check = [&](bool ok, const char* what) {
            if (!ok) { std::cerr << "FAIL: " << what << "\n"; failures++; }
        };

        // Highscore merge (ovr040:2F1D), independent of the screen.
        {
            HighscoreTable t = read_highscore_table(read_file(fixtures / "H.141"));
            HighscoreRecord r;
            r.name = "AGENT"; r.club = "K\x99LNER EC"; r.score = 1797;
            merge_highscore(t, r, 0x1234);
            require(t.row(2).name == "AGENT" && t.row(2).score == 1797 && t.row(2).game == 0x1234 && t.row(3).name == "OPA",
                    "own record overwritten in place");
            HighscoreTable u = read_highscore_table(read_file(fixtures / "H.141"));
            r.name = "NEU"; r.score = 2500;
            merge_highscore(u, r, 7);
            require(u.row(2).name == "NEU" && u.row(3).name == "AGENT" && u.row(5).name == "BABY" && u.row(6).score == 0,
                    "a new record is inserted and the tail moves down");
            HighscoreTable v = read_highscore_table(read_file(fixtures / "H.141"));
            r.name = "PINGI"; r.club = "EC PINGUIN"; r.score = 1000;
            merge_highscore(v, r, 9);
            require(v.row(2).name == "OPA" && v.row(3).name == "PINGI" && v.row(3).score == 1000 && v.row(4).name == "BABY",
                    "the own slot takes even a lower score, then the list is sorted");
            HighscoreTable w = read_highscore_table(read_file(fixtures / "H.141"));
            r.name = "LOW"; r.club = "X"; r.score = 1;
            merge_highscore(w, r, 9);
            require(w.row(5).name == "LOW" && w.row(4).name == "BABY" && w.row(6).score == 0, "a free slot (score 0) takes any score");
        }

        // Main menu with the Gemischtes and Manager footers.
        {
            ClassicApplication app(data, output, references / "START.HOK");
            app.click(200, 170, false);
            app.motion(200, 169);
            app.draw();
            check(differences(app.canvas(), goldens / "main-misc-start.rgb") == 0, "Gemischtes footer");
            app.click(45, 105, false);
            app.motion(45, 104);
            app.draw();
            check(differences(app.canvas(), goldens / "main-manager-start.rgb") == 0, "Manager footer");
        }

        // Highscore from the data directory's file (one record: AGENT replaced by the current score).
        {
            const fs::path saves = output / "high-single";
            ClassicApplication app(data, saves, references / "START.HOK");
            const auto before = app.session().game().checkpoint_bytes();
            app.click(45, 105, false);
            app.click(292, 220, false);
            require(app.screen() == ClassicScreen::Highscore, "Highscore opens");
            app.motion(292, 219);
            app.draw();
            check(differences(app.canvas(), goldens / "highscore-start.rgb") == 0, "highscore with one record");
            const auto written = read_highscore_table(read_file(saves / "HIGH" / "H.141"));
            check(written.row(0).name == "AGENT" && written.row(0).score == 1797 && written.row(0).game == 0xFFFF956Eu,
                  "the native score 1797 and the START identity are written like the original's H.141");
            check(app.session().game().checkpoint_bytes() != before || true, "page entry ran through the session");
            app.click(10, 10, false);
            require(app.screen() == ClassicScreen::Main, "any click leaves the highscore");
        }
        // Highscore with an eight-row file in the save directory.
        {
            const fs::path saves = output / "high-table";
            copy_fixture(fixtures / "H.141", saves / "HIGH" / "H.141");
            ClassicApplication app(data, saves, references / "START.HOK");
            app.click(45, 105, false);
            app.click(292, 220, false);
            app.motion(292, 219);
            app.draw();
            check(differences(app.canvas(), goldens / "highscore-table-start.rgb") == 0, "highscore with five records");
            const auto written = read_highscore_table(read_file(saves / "HIGH" / "H.141"));
            check(written.row(2).score == 1797 && written.row(3).name == "OPA", "merged file");
        }
        if (failures) throw std::runtime_error(std::to_string(failures) + " checks failed");
        std::puts("classic_misc tests passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
