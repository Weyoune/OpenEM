// SPDX-License-Identifier: GPL-3.0-only
// Remaining UI pieces outside the match view (docs/CLASSIC_UI.md "Titelbild, Startmenü-Einstellungen, Pokal live"):
//   - title screen KB\197 (ovr034:0799..07E5) until a key or click
//   - Statistiken -> Spieler: a press on a listed player opens the player page (ovr025:0CDA..0DCB)
//   - start menu settings (ovr027:06C6..0A39) and their effect on the new game (ovr027:1808..1863)
//   - the live DEB cup draw: at the start of a new game (ovl05_4c7b) and in a later round (ovl05_536d)
//   - the end of the game at the end year (ovr034:0A84)
#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <stdexcept>

#include "formats/db.hpp"
#include "game/new_game.hpp"
#include "game/world_generator.hpp"
#include "ui/classic_app.hpp"
#include "ui/classic_misc_newmanager.hpp"
#include "ui/classic_stats_players.hpp"
#include "ui/session.hpp"

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        failures++;
    }
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
uint64_t virtual_now = 1000;
uint64_t virtual_clock() { return virtual_now; }
bool is_permutation_of_places(const std::vector<int>& order, size_t n) {
    std::vector<int> sorted = order, expected(n);
    std::iota(expected.begin(), expected.end(), 0);
    std::sort(sorted.begin(), sorted.end());
    return sorted == expected;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: em_classic_ui_gaps_tests <EM data> <work>");
        const fs::path data = argv[1], work = argv[2];
        fs::remove_all(work);
        fs::create_directories(work);
        set_classic_clock(virtual_clock);

        // --- title screen ---------------------------------------------------------------------------------------
        {
            ClassicApplication app(data, work, data / "START.HOK");
            ClassicCanvas plain(data);
            plain.background(197);
            app.show_title();
            app.draw();
            check(app.canvas().argb() == plain.argb(), "title screen = KB\\197 with its palette");
            check(app.probe().modal == "title", "probe names the title");
            app.click(10, 10, true);
            check(!app.title() && app.screen() == ClassicScreen::Main, "a press ends the title, the game goes on");
            app.show_title();
            app.key(SDLK_SPACE);
            check(!app.title(), "a key ends the title");
        }

        // --- Statistiken -> Spieler: a listed player of a human squad opens the player page -----------------------
        {
            int opened = 0, ignored = 0;
            for (const auto& entry : fs::directory_iterator(data)) {
                if (opened && ignored) break;
                if (entry.path().extension() != ".HOK" || entry.path().filename() == "BACKUP.HOK") continue;
                HokFile h;
                try { h = decode_hok(read_file(entry.path())); } catch (const std::exception&) { continue; }
                ClassicStatsPlayersView view;
                try { view = classic_stats_players_view(h); } catch (const std::exception&) { continue; }
                const int humans = h.state.at(0x0DC6);
                for (const auto& row : view.skaters) {
                    bool human = false;
                    for (int m : {0, 1, 2, 3, 4})
                        for (size_t slot = 0; slot < 30 && (m < humans || m == 4); slot++)
                            human |= h.state.at(0x696D + size_t(m) * 0x618 + slot * 0x34 + 0x0F) == row.player;
                    if (human ? opened : ignored) continue;
                    ClassicApplication app(data, work, entry.path());
                    app.render_screen("stats-players");
                    app.click(100, row.baseline - 2, false);
                    if (human) {
                        check(app.probe().modal == "player-detail", "a scorer of a human squad opens the player page");
                        app.click(190, 214, false);  // Aha!
                        check(app.probe().modal.empty() && app.screen() == ClassicScreen::Statistics, "Aha! returns to the list");
                        opened++;
                    } else {
                        check(app.probe().modal.empty(), "a computer club's scorer opens nothing");
                        ignored++;
                    }
                }
            }
            check(opened > 0 && ignored > 0, "saves with both kinds of scorers found");
        }

        // --- start menu settings ----------------------------------------------------------------------------------
        {
            NewGameOptions o;
            check(new_game_highscore_file(o) == 141, "EM.EXE defaults: highscore list H.141");
            NewGameOptions backup;
            backup.values = {5, 3, 0, 1};
            backup.mode = 3;
            check(new_game_highscore_file(backup) == 143, "BACKUP.HOK settings: H.143 (state 0x06AE)");
            step_new_game_option(o, 0, +1);
            step_new_game_option(o, 0, +1);
            step_new_game_option(o, 0, +1);
            check(o.values[0] == 5, "level choice stops at DS:011C = 5");
            for (int i = 0; i < 5; i++) step_new_game_option(o, 1, -1);
            check(o.values[1] == 0, "end choice down to 0 (1994)");
            step_new_game_option(o, 2, +1);
            check(o.values[2] == 0, "squad size stays 0 while the end choice is 0 (ovr027:08DE)");
            step_new_game_option(o, 1, +1);
            step_new_game_option(o, 2, +1);
            step_new_game_option(o, 1, -1);
            check(o.values[1] == 1 && o.values[2] == 1, "end choice cannot go back to 0 with squad size 1");
            check(!toggle_new_game_mode(o, 0, 2) && o.mode == 3, "league mode stays with two named managers");
            check(toggle_new_game_mode(o, 0, 1) && o.mode == 2, "league mode off: WM only");
            check(new_game_highscore_file(o) == 30000, "WM only: list 30000");
            check(toggle_new_game_mode(o, 1, 1) && o.mode == 0 && new_game_highscore_file(o) == 0, "both off: no list");

            ClassicNewManager menu;
            const HokFile base = decode_hok(read_file(*find_new_game_base(data)));
            menu.open_start(base);
            check(menu.options().values == (std::array<int, 4>{3, 3, 0, 1}) && menu.options().mode == 3, "start menu opens with the EM.EXE values");
            menu.click(base, 0x140, 0x4E + 3, false);  // row 0, upper arrow
            check(menu.options().values[0] == 2, "upper arrow of row 0 lowers the level choice");
            menu.click(base, 0x140, 0x4E + 0x1C + 0x0C, false);  // row 1, lower arrow
            check(menu.options().values[1] == 3, "lower arrow of row 1 stays at the maximum");
            menu.click(base, 0x140, 0x4E + 0x38 + 0x0C, false);  // row 2, lower arrow
            check(menu.options().values[2] == 1, "row 2 steps up");
            menu.click(base, 0x110, 0x35, false);  // WM button
            check(menu.options().mode == 1, "the WM button switches bit 1");

            ClassicCanvas a(data), b(data);
            ClassicNewManager fresh;
            fresh.open_start(base);
            fresh.draw(a, base, -1, -1);
            menu.draw(b, base, -1, -1);
            const auto pa = a.argb(), pb = b.argb();
            size_t changed = 0;
            for (int y = 0x51; y < 0x51 + 0x1C * 3; y++)
                for (int x = 0xC3; x < 0xC3 + 0x71; x++) changed += pa[size_t(y) * 320 + size_t(x)] != pb[size_t(y) * 320 + size_t(x)];
            check(changed > 0, "the option cells show the new values");

            // the settings go into the new game (one manager: the start state; ovr027:1808..1863)
            const Database db = decode_db(read_file(data / "KB" / "23.vga"));
            NewGameOptions chosen;
            chosen.values = {2, 1, 1, 2};
            chosen.mode = 1;
            const HokFile g = start_new_game(base, db, {"TESTER", 3, 5}, chosen);
            check(g.state.at(0x28) == 2 && g.state.at(0x29) == 1 && g.state.at(0x2A) == 1 && g.state.at(0x2B) == 2, "DS:0118..011B");
            check(g.state.at(0xAEB4) == 3, "level byte DS:0ED8 = 5 - choice");
            check((g.state.at(0xAEB5) | g.state.at(0xAEB6) << 8) == 1996, "end year DS:5A14 = 0x7CA + 2 x choice");
            check(g.state.at(0x025A) == 2, "league only: DS:D0B9 = 2");
            check((g.state.at(0x06AE) | g.state.at(0x06AF) << 8) == 0, "no highscore list for these settings");
            const uint32_t balance = uint32_t(g.state.at(0x0DCD + 0x18F)) | uint32_t(g.state.at(0x0DCD + 0x190)) << 8 |
                                     uint32_t(g.state.at(0x0DCD + 0x191)) << 16 | uint32_t(g.state.at(0x0DCD + 0x192)) << 24;
            check(balance == 3 * 150000 + 1200000, "balance DS:0ED8 x 150000 + 1200000");
        }

        // --- the live cup draw of a new game (one manager: the start state's draw) -----------------------------------
        {
            ClassicApplication app(data, work, fs::path());
            require(app.screen() == ClassicScreen::NewManager, "start menu without a save");
            app.click(25, 60, false);
            app.click(210, 140, false);
            app.text_input("LIVE");
            app.key(SDLK_RETURN);
            app.click(156, 60, false);
            app.click(210, 60, false);
            app.click(282, 212, false);  // Los jetzt!
            require(!app.session().loaded(), "question first");
            app.click(120, 172, false);  // "Aber klar"
            require(app.session().loaded(), "the game starts after the answer");
            check(app.session().start_cup_places().size() == 32 && is_permutation_of_places(app.session().start_cup_reveal(), 32),
                  "32 places, uncovered in a permutation");
            check(app.probe().modal == "cup-draw", "the board uncovers the places");
            app.draw();
            const auto first = app.canvas().argb();
            virtual_now += 55 * 2 * 10 * 3;  // three places
            app.tick();
            app.draw();
            check(app.canvas().argb() != first, "places are uncovered over time");
            app.click(10, 10, false);  // fast
            for (int i = 0; i < 40 && app.probe().modal == "cup-draw"; i++) { virtual_now += 55; app.tick(); }
            check(app.probe().modal == "cup-draw-key", "TASTE DR]CKEN after the last place");
            app.click(10, 10, false);
            check(app.screen() == ClassicScreen::Cup && app.probe().modal.empty(), "then the cup page");
        }

        // --- generator: "Aber klar" takes the uncovering draws from the Borland stream ------------------------------
        {
            const Bytes exe = read_file(data / "EM.EXE"), db = read_file(data / "KB" / "23.vga");
            WorldSettings s;
            s.managers = {WorldManager{"anna", 4, 3}, WorldManager{"bert", 6, 20}};
            const WorldSeeds seeds{7, 7, 7};
            const WorldResult quiet = generate_world(exe, db, s, seeds);
            s.cup_live = true;
            const WorldResult live = generate_world(exe, db, s, seeds);
            check(quiet.cup_places == live.cup_places && quiet.cup_reveal.empty(), "same draw, no order without the live draw");
            check(is_permutation_of_places(live.cup_reveal, 32), "the order is a permutation of the 32 places");
            check(quiet.borland != live.borland, "the uncovering draws move the Borland stream before the market days");
        }

        // --- a later round: the decider and the uncovering order (Game, ovl05_536d) ----------------------------------
        {
            const Bytes exe = read_file(data / "EM.EXE"), save = read_file(data / "START.HOK");
            Game quiet(exe, save), live(exe, save);
            int asked = 0, clubs = 0;
            std::vector<int> order, places;
            live.set_cup_draw_decider([&](int n) { asked++; clubs = n; return true; });
            live.set_cup_reveal_observer([&](const std::vector<int>& o, const std::vector<int>& p) { order = o; places = p; });
            for (int day = 0; day < 200 && !asked; day++) {
                quiet.advance_day();
                live.advance_day();
                if (!asked) require(quiet.borland_state() == live.borland_state(), "same game until the question");
            }
            require(asked == 1, "the question of the next round");
            check(clubs >= 4 && size_t(clubs) == places.size() && is_permutation_of_places(order, places.size()), "order over the places");
            check(quiet.borland_state() != live.borland_state(), "\"Aber klar\" takes Borland draws");
        }

        // --- the end year: the season change ends the game (highscore, "Das wars...", program end) -------------------
        {
            HokFile h = decode_hok(read_file(data / "SAISEND.HOK"));
            const Bytes exe = read_file(data / "EM.EXE");
            int season_change = -1;
            {
                Game normal(exe, encode_hok(h));
                const int year = normal.save().season_year;
                for (int day = 0; day < 80 && season_change < 0; day++) {
                    normal.advance_day();
                    if (normal.save().season_year != year) season_change = day;
                }
                check(season_change >= 0 && !normal.game_over(), "end year 22222: the season change goes on");
            }
            h.state.at(0xAEB5) = uint8_t(1994 & 0xFF);
            h.state.at(0xAEB6) = uint8_t(1994 >> 8);
            Bytes before_end;  // the last day before the season change, as an OpenEM checkpoint
            {
                Game ending(exe, encode_hok(h));
                int day = 0;
                for (; day < 80 && !ending.game_over(); day++) {
                    before_end = ending.checkpoint_bytes();
                    ending.advance_day();
                }
                check(ending.game_over() && day - 1 == season_change, "Game: the season change of the end year ends the game");
            }
            const fs::path file = work / "END1994.OEM";
            write_new_save(file, before_end);
            ClassicApplication app(data, work, file);
            // play on like the player: the crest, wait for timed screens, answer notices and tables
            for (int i = 0; i < 4000 && app.probe().modal != "game-end" && app.screen() != ClassicScreen::Highscore; i++) {
                const auto p = app.probe();
                if (p.modal == "match" || p.modal == "banner" || p.modal == "timed-notice" || p.modal == "advancing") {
                    virtual_now = std::max(virtual_now + 16, p.deadline.value_or(0));
                    app.tick();
                    app.draw();
                } else if (p.modal == "notice") app.click(120, 171, false);
                else if (p.modal == "cup-table" || p.modal.rfind("review", 0) == 0) app.click(160, 120, false);
                else if (p.modal == "messages") app.key(SDLK_SPACE);
                else if (!p.modal.empty()) app.click(190, 171, false);
                else if (app.screen() != ClassicScreen::Main) app.click(316, 4, true);
                else if (p.category >= 0) app.click(160, 20, true);
                else app.click(293, 52, false);  // the crest
            }
            check(app.screen() == ClassicScreen::Highscore || app.probe().modal == "game-end", "the season change of 1994 ends the game");
            if (app.screen() == ClassicScreen::Highscore) app.click(10, 10, false);  // the list of the settings
            check(app.probe().modal == "game-end", "\"Das wars...\" after the highscore");
            app.draw();
            app.click(10, 10, false);
            check(app.finished(), "a press ends the program");
        }

        if (failures) throw std::runtime_error(std::to_string(failures) + " checks failed");
        std::printf("classic ui gaps: ok\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "classic ui gaps: %s\n", e.what());
        return 1;
    }
}
