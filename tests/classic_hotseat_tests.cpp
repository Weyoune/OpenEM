// SPDX-License-Identifier: GPL-3.0-only
// Hot seat with two managers (src/ui/classic_hotseat.cpp): pixel comparison with reference images of the original
// (REFERENCE_DIR/raster_goldens), offers between the two managers, and a run of several menu days through the UI.
//
// HOT2.HOK is the state of the original after a new game with two managers ("anna", EHC Freiburg, face 4; "bert",
// SC Preussen Berlin, face 6) and its first match day, taken at anna's menu of Wednesday 13.09.1993. The reference
// images: anna's menu (main_m0_d1), bert's menu after anna's crest (main_m1_d1) and bert's lineup page
// (lineup_m1_d1); the start menu with both rows (start2).
//
// Usage: classic_hotseat_tests DATA REFERENCE_DIR OUTPUT
// Failed checks are counted; the test fails at the end if any did. EM_DUMP_MISMATCH=<folder> writes the canvas
// of every differing picture as .bmp.
#include "ui/classic_app.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;

namespace {
// A screen rectangle excluded from a comparison.
struct Rect { int left, top, right, bottom; };  // right/bottom exclusive

// Throws with `message` when `condition` is false.
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Number of pixels outside `skip` in which the canvas differs from the raw 320 x 240 RGB reference image
// `golden`; prints the bounding box of the differences and optionally dumps the canvas (EM_DUMP_MISMATCH).
size_t differences(const ClassicCanvas& canvas, const fs::path& golden, const std::vector<Rect>& skip = {}) {
    const auto expected = read_file(golden);
    require(expected.size() == 320 * 240 * 3, "native RGB golden size");
    const auto actual = canvas.argb();
    size_t count = 0;
    int left = 320, top = 240, right = -1, bottom = -1;
    for (size_t pixel = 0; pixel < actual.size(); pixel++) {
        const int x = int(pixel % 320), y = int(pixel / 320);
        bool skipped = false;
        for (const Rect& r : skip) skipped |= x >= r.left && x < r.right && y >= r.top && y < r.bottom;
        if (skipped) continue;
        const uint32_t rgb = uint32_t(expected[pixel * 3]) << 16 | uint32_t(expected[pixel * 3 + 1]) << 8 | expected[pixel * 3 + 2];
        if ((actual[pixel] & 0xffffff) != rgb) {
            count++;
            left = std::min(left, x); right = std::max(right, x); top = std::min(top, y); bottom = std::max(bottom, y);
        }
    }
    if (count) {
        std::cerr << golden.filename().string() << ": " << count << " pixels differ in " << left << "," << top << ".." << right << "," << bottom << "\n";
        if (const char* dump = std::getenv("EM_DUMP_MISMATCH")) {
            const fs::path out = path_from_text(dump) / (golden.stem().string() + ".bmp");
            std::error_code ec;
            fs::remove(out, ec);
            write_new_save(out, canvas.bmp_bytes());
        }
    }
    return count;
}

// Name of `team` read from its raw team record (state 0x17E7, 0x3D bytes per team, zero-terminated, at most 22).
std::string raw_team(const HokFile& h, int team) {
    std::string s;
    for (size_t k = 0; k < 22 && h.state.at(0x17E7 + size_t(team) * 0x3D + k); k++) s.push_back(char(h.state.at(0x17E7 + size_t(team) * 0x3D + k)));
    return s;
}
// Index of the club named `name` among the 48 league clubs; throws if there is none.
int team_by_name(const HokFile& h, const std::string& name) {
    for (int t = 0; t < 48; t++)
        if (raw_team(h, t) == name) return t;
    throw std::runtime_error("club not found: " + name);
}

// Plays until the menu of the next menu day (match display skipped with a right click like a player would).
void play_on(ClassicApplication& app) {
    const int day = app.session().game().season_day();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (std::chrono::steady_clock::now() < until) {
        app.tick();
        if (app.replaying()) app.click(10, 10, true);
        else if (!app.advancing() && app.session().game().season_day() != day) return;
        SDL_Delay(2);
    }
    throw std::runtime_error("the day run did not return to the menu (day " + std::to_string(app.session().game().season_day()) + ", screen " +
                             std::to_string(int(app.screen())) + ", advancing " + std::to_string(app.advancing()) + ", contract " +
                             std::to_string(app.session().contract_question().has_value()) + ")");
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("classic_hotseat_tests DATA REFERENCE_DIR OUTPUT");
        const fs::path data = path_from_text(argv[1]), refs = path_from_text(argv[2]);
        const fs::path goldens = refs / "raster_goldens";
        const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path output = path_from_text(argv[3]) / std::to_string(serial);
        int failures = 0;
        auto check = [&](bool ok, const std::string& what) {
            if (!ok) { std::cerr << "FAIL: " << what << "\n"; failures++; }
        };

        // --- the original's state with two managers: menus of both managers on the same day ---------------
        {
            ClassicApplication app(data, output / "hot2", refs / "HOT2.HOK");
            const Game& g0 = app.session().game();
            require(g0.managers() == 2 && app.session().active_manager() == 0, "HOT2: two managers, manager 0 at the menu");
            const int day = g0.season_day();
            app.motion(295, 55);
            app.draw();
            check(differences(app.canvas(), goldens / "main_m0_d1.rgb") == 0, "anna's main menu (name, face, club, opponent, money)");

            app.click(295, 55, false);  // anna's crest: the menu goes to bert, the day is not played
            check(app.session().active_manager() == 1 && !app.advancing() && app.session().game().season_day() == day,
                  "the crest of manager 0 hands the menu to manager 1 on the same day");
            check(app.session().game().human_team() == team_by_name(app.session().game().save(), "SC PREUSSEN BERLIN"), "bert's club");
            app.tick();
            app.draw();
            check(differences(app.canvas(), goldens / "main_m1_d1.rgb") == 0, "bert's main menu");

            app.click(110, 110, false);  // Mannschaft
            app.click(25, 220, false);   // Aufst.
            check(app.screen() == ClassicScreen::Lineup, "bert's lineup page");
            app.motion(25, 220);
            app.draw();
            // slot 10 "BRANNSTR\M": OpenEM draws the ISO 646 byte 0x5C through UTF-8 with another glyph than the original (not hot seat)
            check(differences(app.canvas(), goldens / "lineup_m1_d1.rgb", {{74, 88, 86, 95}}) == 0, "bert's lineup (his squad)");
            app.click(300, 5, true);  // right click outside the roster
            check(app.screen() == ClassicScreen::Main && app.session().active_manager() == 1, "back at bert's menu");

            app.click(295, 55, false);  // bert's crest plays the day
            check(app.advancing() && app.session().active_manager() == 0, "the last manager's crest plays the day");
            play_on(app);
            check(app.session().active_manager() == 0 && app.session().game().season_day() > day, "the next menu day starts with manager 0");

            // a few more menu days through the UI
            for (int round = 0; round < 4; round++) {
                const int d = app.session().game().season_day();
                app.click(295, 55, false);
                require(app.session().active_manager() == 1 && app.session().game().season_day() == d, "hand-over on every menu day");
                app.draw();
                app.click(295, 55, false);
                play_on(app);
                require(app.session().active_manager() == 0 && app.session().game().season_day() > d, "day played after both managers");
            }
            const Game& g = app.session().game();
            check(g.managers() == 2, "still two managers");
            auto played = [&](int m) {
                const int team = g.save().state.at(0x0DCD + 0x1AF * size_t(m) + 0x14);
                const auto& row = g.league(team / 12).rows.at(size_t(team % 12));
                return row.games[0] + row.games[1];
            };
            const int played0 = played(0), played1 = played(1);
            check(played0 >= 3 && played1 >= 3, "both managers' clubs played their league games (" + std::to_string(played0) + ", " + std::to_string(played1) + ")");
        }

        // --- offers between managers: HOT3 = the original after bert listed his goalie MERK (pool slot 1), at anna's
        //     menu of 17.09.1993; HOT3BUY = the original after anna bought him for 500000 DM (1 year, 17100 DM) ----------
        {
            const auto rgb = [&](const char* n) { return goldens / (std::string("offer-") + n + ".rgb"); };
            auto open_transfer = [&](ClassicApplication& app) {
                app.click(110, 110, false);  // Mannschaft
                app.click(185, 220, false);  // Transfer
                require(app.screen() == ClassicScreen::Transfer, "anna's transfer page");
            };
            auto bid = [&](ClassicApplication& app, const char* amount) {
                app.click(200, 52, false);  // MERK, x <= 250: purchase
                app.text_input(amount);
                app.key(SDLK_RETURN);
            };
            const size_t merk_pool = 0x81CD + 52 + 0x1A;
            {
                ClassicApplication app(data, output / "hot3a", refs / "HOT3.HOK");
                open_transfer(app);
                app.motion(230, 230);
                app.draw();
                check(differences(app.canvas(), rgb("transfer_hot3")) == 0, "anna's transfer page with bert's MERK");
                bid(app, "100000");
                app.motion(200, 52);
                app.draw();
                // (314,36): right end of the pool hover line, a transfer-page detail outside the hot seat
                check(differences(app.canvas(), rgb("unfair_m0"), {{314, 36, 315, 37}}) == 0, "unfair bid: bert's dialog \"nicht erlaubt (unfair)\"");
                app.click(190, 172, false);  // "Nein"
                check((app.session().game().save().state.at(merk_pool) & 0x80) == 0, "an unfair bid sets no refusal bit");
                SDL_Delay(1450); app.tick();  // the timed notice "Ihr Angebot wurde abgelehnt." (25 BIOS ticks)
                bid(app, "500000");
                app.motion(200, 52);
                app.draw();
                check(differences(app.canvas(), rgb("question_hot3")) == 0, "bert is asked: \"Nehmen Sie das Angebot (500000 DM)\"");
                app.click(190, 172, false);  // bert: "Nein !"
                check(app.session().game().save().state.at(merk_pool) == 0x81, "bert's refusal sets bit 7 and anna's bit (as in the original)");
            }
            {
                ClassicApplication app(data, output / "hot3b", refs / "HOT3.HOK");
                open_transfer(app);
                bid(app, "500000");
                app.click(125, 172, false);  // bert: "Na gut."
                app.motion(125, 172);
                app.draw();
                // masked: the pool hover rules of the original (stale from the bid click) and the hover help line, transfer-page
                // details outside the hot seat
                check(differences(app.canvas(), rgb("contract_hot3"), {{176, 52, 316, 53}, {176, 68, 316, 69}, {60, 210, 110, 220}}) == 0,
                      "contract panel for MERK after bert's yes (at the top, as in the original)");
                app.click(40, 89, false);  // "Mach' ich!"
                app.motion(40, 89);
                app.draw();
                // masked: MERK's last column (profile draws of the insertion, OpenEM's Borland stream is not the original's here) and
                // the footer (after the contract panel the original keeps a number width of 1 for the footer; hover line)
                check(differences(app.canvas(), rgb("after_buy_hot3"), {{148, 40, 154, 46}, {0, 218, 320, 240}}) == 0, "anna's transfer page after the purchase");
                const Bytes s = decode_hok(app.session().game().save_bytes()).state;
                const Bytes want = decode_hok(read_file(refs / "HOT3BUY.HOK")).state;
                for (const auto& [at, what] : std::vector<std::pair<size_t, const char*>>{
                         {0x0DCD + 0x18F, "anna's balance"}, {0x0DCD + 0x1AF + 0x18F, "bert's balance"},
                         {0x4FA5 + 5 * 0x21 + 0x15, "MERK +0x15"}, {0x4FA5 + 5 * 0x21 + 0x1A, "MERK owner"}, {0x4FA5 + 5 * 0x21 + 0x1D, "MERK club"}}) {
                    bool same = true;
                    for (size_t k = 0; k < (std::string(what).find("balance") != std::string::npos ? 4u : 1u); k++) same &= s.at(at + k) == want.at(at + k);
                    check(same, std::string("after the purchase like the original: ") + what + " (OpenEM " + std::to_string(int32_t(le32(s, at))) + ", original " + std::to_string(int32_t(le32(want, at))) + ")");
                }
                check(app.session().game().save().squad_of(1).size() == decode_hok(read_file(refs / "HOT3.HOK")).squad_of(1).size(), "bert's squad unchanged");
            }
            {  // loan (x > 250): value / 3 for the fairness check; bert keeps the player (+0x1A), anna has him for a season
                ClassicApplication app(data, output / "hot3c", refs / "HOT3.HOK");
                open_transfer(app);
                const long anna = app.session().game().budget().balance;
                app.click(290, 52, false);
                app.text_input("170000");
                app.key(SDLK_RETURN);
                app.click(125, 172, false);  // bert: "Na gut."
                const Bytes s = decode_hok(app.session().game().save_bytes()).state;
                const size_t merk = 0x4FA5 + 5 * 0x21;
                check(app.session().game().budget().balance == anna - 510000, "loan: anna pays the bid, no sponsor");
                check(s.at(merk + 0x1A) == 1 && s.at(merk + 0x1D) == app.session().game().human_team(), "loan: bert keeps MERK, he plays for anna");
                const Bytes before = decode_hok(read_file(refs / "HOT3.HOK")).state;
                const size_t bert = 0x0DCD + 0x1AF + 0x18F;
                check(int32_t(le32(s, bert)) == int32_t(le32(before, bert)) + 510000, "loan: bert gets the bid");
                bool loaned = false;
                for (int slot = 0; slot < 29; slot++) {
                    const size_t r = 0x696D + size_t(slot) * 52;
                    loaned |= s.at(r + 15) == 5 && s.at(r + 0x0B) == 1 && s.at(r + 0x0C) == uint8_t(before.at(merk + 0x1D) + 0x80);
                }
                check(loaned, "loan: MERK in anna's squad for one season, returning to bert's club");
            }
        }

        // --- a new game with two managers plays its first days (Session, game/new_game.cpp start_new_world) ---
        {
            Session s(data);
            const HokFile base = decode_hok(read_file(*find_new_game_base(data)));
            s.start_new_game(std::vector<NewGameSetup>{{"anna", 4, team_by_name(base, "EHC FREIBURG")}, {"bert", 6, team_by_name(base, "SC PREUSSEN BERLIN")}});
            require(s.game().managers() == 2, "new game: two managers");
            for (int d = 0; d < 40; d++) {
                try {
                    require(s.advance_day(), "new game: day without a pending decision");
                } catch (const std::exception& e) {
                    throw std::runtime_error("new game, day " + std::to_string(d) + ": " + e.what());
                }
            }
        }

        // --- start menu of a new game with two managers ----------------------------------------------------
        {
            ClassicApplication app(data, output / "newgame", fs::path{});
            require(app.screen() == ClassicScreen::NewManager && !app.session().loaded(), "without a save the start menu opens");
            const HokFile base = decode_hok(read_file(*find_new_game_base(data)));
            auto choose_club = [&](int row, const std::string& name) {
                const int team = team_by_name(base, name);
                app.click(156, 60 + 51 * row, false);  // crest column
                for (int p = 0; p < team / 12; p++) app.click(315, 150, false);  // page down
                const int k = team % 12;
                app.click(0xBE + 40 * (k % 3) + 19, 0x29 + 40 * (k / 3) + 19, false);
            };
            app.click(25, 60, false);   // face column, row 0
            app.click(253, 100, false); // face 4
            app.text_input("anna");
            app.key(SDLK_RETURN);
            choose_club(0, "EHC FREIBURG");
            app.click(25, 111, false);  // row 1
            app.click(212, 140, false); // face 6
            app.text_input("bert");
            app.key(SDLK_RETURN);
            choose_club(1, "SC PREUSSEN BERLIN");
            app.motion(300, 20);
            app.draw();
            // the option cells show the settings of that run of the original
            // row 0: the original centres the lowercase name "anna" one pixel further right (its width table for a/n, not hot seat)
            check(differences(app.canvas(), goldens / "start2.rgb", {{0xC3, 0x51, 0x134, 0x51 + 0x1C * 4}, {47, 40, 134, 50}}) == 0, "start menu with two managers");

            app.click(282, 212, false);  // Los jetzt!
            check(!app.session().loaded(), "the cup question comes before the generator draws the cup");
            // the DEB cup draw question "An Alle:" over the empty draw board, pixel-equal to the original
            app.motion(281, 213);
            app.draw();
            check(differences(app.canvas(), goldens / "cupq2.rgb") == 0, "cup draw question \"An Alle:\" at the start");
            app.click(190, 172, false);  // "Kein Gedanke"
            require(app.session().loaded(), "Los jetzt! and the answer start the game");
            const Game& g = app.session().game();
            check(g.managers() == 2 && app.session().active_manager() == 0, "two managers, manager 0 at the menu");
            const HokFile& h = g.save();
            check(raw_team(h, h.state.at(0x0DCD + 0x14)) == "EHC FREIBURG" && raw_team(h, h.state.at(0x0DCD + 0x1AF + 0x14)) == "SC PREUSSEN BERLIN",
                  "the chosen clubs, found by name in the generated world");
            check(h.state.at(0x01EE) == 4 && h.state.at(0x01EF) == 6, "faces of manager m at state 0x01EE + m");
            check(!h.squad_of(1).empty() && h.squad_of(1).front().player > 0, "manager 1 has a squad");
            app.click(295, 55, false);
            check(app.session().active_manager() == 1 && !app.advancing(), "hand-over in the new game");
            app.click(295, 55, false);
            play_on(app);
            check(app.session().active_manager() == 0, "the first day of the new game was played");
        }

        if (failures) throw std::runtime_error(std::to_string(failures) + " checks failed");
        std::printf("classic hotseat: ok\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Classic hotseat: %s\n", e.what());
        return 1;
    }
}
