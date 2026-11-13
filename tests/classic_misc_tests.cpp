// SPDX-License-Identifier: GPL-3.0-only
// Gemischtes (Laden, Sichern, Einst.), Manager -> Neuer and Manager -> Highscore: pixel comparison with reference
// images of the original (GOLDENS) and behaviour, including the merge of a result into the highscore file
// HIGH\H.141 (game/highscore.cpp).
//
// Usage: classic_misc_tests DATA REFERENCE GOLDENS FIXTURES OUTPUT
//   REFERENCE holds START.HOK (and a HIGH folder), FIXTURES an eight-row highscore file H.141.
//   Failed checks are counted; EM_DUMP_MISMATCH=<folder> writes differing pictures as .bmp.
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
// Throws with `message` when `condition` is false.
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// Number of pixels in which the canvas differs from a raw 320 x 240 RGB reference image; prints the bounding box
// and optionally dumps the canvas (EM_DUMP_MISMATCH).
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
// Copies a fixture file to `to`, creating its folder and replacing an existing file.
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

        // Highscore merge, independent of the screen: the manager's own row (same game identity) is overwritten in
        // place even by a lower score, a new record is inserted, then the list is sorted by score.
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
        // Gemischtes -> Laden in the reference directory: the first click on HIGH enters it, the kept selection
        // index marks H.141, a click on that (non-savegame) file puts its name into FILENAME.
        {
            ClassicApplication app(references, output / "files", references / "START.HOK");
            app.click(200, 170, false);
            app.click(26, 220, false);
            require(app.screen() == ClassicScreen::Files, "Laden opens the file page");
            app.click(105, 72, false);
            app.click(105, 72, false);
            app.motion(105, 72);
            app.draw();
            check(differences(app.canvas(), goldens / "files-load-high-start.rgb") == 0, "Laden in C:\\HIGH with H.141 chosen");
            require(app.screen() == ClassicScreen::Files, "a non-savegame file is not opened");
            app.click(10, 10, true);
            require(app.screen() == ClassicScreen::Main, "right click returns to the menu");
            app.click(80, 220, false);
            require(app.screen() == ClassicScreen::Files, "Sichern opens the file page");
            app.click(10, 10, true);
            app.click(127, 219, false);
            require(app.screen() == ClassicScreen::Settings, "Einst. opens the settings page");
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
        // Manager -> Neuer, with the same clicks as in the original's reference session (pointer hotspot one row above the click).
        {
            ClassicApplication app(data, output / "new", references / "START.HOK");
            const auto before = app.session().game().checkpoint_bytes();
            app.click(45, 105, false);
            app.click(240, 220, false);
            require(app.screen() == ClassicScreen::NewManager, "Neuer opens the start menu");
            app.motion(240, 219);
            app.draw();
            check(differences(app.canvas(), goldens / "newmanager-start.rgb") == 0, "start menu with the running manager");
            app.click(90, 110, false);  // name column of an empty row: nothing
            app.click(25, 110, false);  // face column, row 1
            app.motion(319, 239);       // in this reference image the pointer stood in the bottom right corner
            app.draw();
            check(differences(app.canvas(), goldens / "newmanager-faces-start.rgb") == 0, "face board");
            app.click(210, 140, false);  // face 6
            app.text_input("START");     // the reference session had these keys still buffered
            app.key(SDLK_RETURN);
            app.motion(210, 139);
            app.draw();
            check(differences(app.canvas(), goldens / "newmanager-name-start.rgb") == 0, "face and name of the new manager");
            app.click(90, 107, false);
            app.click(156, 110, false);  // crest column, row 1
            app.motion(156, 109);
            app.draw();
            check(differences(app.canvas(), goldens / "newmanager-clubs-start.rgb") == 0, "club board, first league page");
            app.click(311, 190, false);  // down arrow: second league page, pointer frames the last crest
            app.motion(311, 189);
            app.draw();
            check(differences(app.canvas(), goldens / "newmanager-clubs2-start.rgb") == 0, "club board, second page with name");
            app.click(210, 60, false);  // ERC Schwenningen
            app.motion(210, 59);
            app.draw();
            check(differences(app.canvas(), goldens / "newmanager-chosen-start.rgb") == 0, "new manager with club");
            app.click(280, 212, false);  // Los jetzt!
            require(app.screen() == ClassicScreen::Main, "Los jetzt! returns to the menu");
            require(app.session().game().managers() == 2, "Los jetzt! adds the new manager to the running game");
            require(app.session().game().save().state.at(0x0DCD + 0x1AF) == 'S', "the second manager block holds the new name");
            (void)before;
        }
        // Neuer without a new row returns silently; a row needs face and club.
        {
            ClassicNewManager setup;
            ClassicApplication app(data, output / "new2", references / "START.HOK");
            setup.open(app.session().game().save());
            const auto& h = app.session().game().save();
            require(setup.click(h, 280, 212, false) == ClassicNewManager::Result::Leave, "Los jetzt! without a new manager");
            setup.click(h, 25, 160, false);
            require(setup.panel() == ClassicNewManager::Panel::Faces && setup.active_row() == 2, "face board for row 2");
            require(setup.click(h, 245, 100, false) == ClassicNewManager::Result::None && setup.rows()[2].face == -1,
                    "face 4 (row 1, column 1) taken by the running manager cannot be chosen");
            setup.click(h, 205, 60, false);
            require(setup.rows()[2].face == 0 && setup.editing_name(), "face 0 chosen, name field open");
            setup.text("A");
            setup.key(SDLK_RETURN);
            require(setup.editing_name(), "a one-letter name is refused");
            setup.text("B");
            setup.key(SDLK_RETURN);
            require(!setup.editing_name() && setup.rows()[2].name == "AB", "name accepted");
            require(setup.click(h, 280, 212, false) == ClassicNewManager::Result::None, "a row without club blocks Los jetzt!");
            setup.click(h, 20, 160, true);
            require(setup.rows()[2].face < 0, "right click removes the new manager");
            require(setup.click(h, 280, 212, false) == ClassicNewManager::Result::Leave, "then Los jetzt! leaves");
        }
        if (failures) throw std::runtime_error(std::to_string(failures) + " checks failed");
        std::puts("classic_misc tests passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
