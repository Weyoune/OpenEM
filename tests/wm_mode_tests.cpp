// SPDX-License-Identifier: GPL-3.0-only
// The WM-only mode "Nur WM" (src/game/wm_mode.cpp, src/ui/classic_wm_mode.cpp, docs/logic/WM.md "WM-Modus"):
//   * rules taken from the listing: mode byte, highscore list, menu tables DS:5159, friendly prices and bonus
//   * a programmatic run: new WM-only game -> jump to the WM, appointment and grant -> squad -> friendlies ->
//     eight WM days -> season change -> the next appointment with the placement grant, contracts unchanged
//   * the line overview against the native WM-squad capture NATAUTO (ABWEHR 90 88 86, STURM 89 83 86)
//   * a scripted start through the start menu of the application (icon, "Los jetzt!", national menu, squad, flag)
// The WM-only start itself was not captured in DOSBox; the flow follows the listing (ovr027, ovr038, ovr034, ovr031).
#include <SDL.h>

#include <chrono>
#include <iostream>
#include <stdexcept>

#include "formats/db.hpp"
#include "game/game.hpp"
#include "game/new_game.hpp"
#include "game/scorers.hpp"
#include "game/wm_mode.hpp"
#include "ui/classic_app.hpp"

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::cerr << "FAIL: " << what << "\n"; failures++; }
}
void require(bool ok, const std::string& what) {
    if (!ok) throw std::runtime_error(what);
}
long national_account(const HokFile& h) { return long(int32_t(le32(h.state, manager_block(5) + 0x18F))); }
int nominees(const HokFile& h) {
    int n = 0;
    for (int slot = 0; slot < 0x1D; slot++) n += h.state.at(squad_block(5) + size_t(slot) * 0x34 + 0x0F) != 0;
    return n;
}
std::vector<int> contract_years(const HokFile& h) {
    std::vector<int> out;
    for (int slot = 0; slot < 0x1D; slot++)
        if (h.state.at(squad_block(0) + size_t(slot) * 0x34 + 0x0F)) out.push_back(h.state.at(squad_block(0) + size_t(slot) * 0x34 + 0x0B));
    return out;
}
// EM_WM_DUMP=dir writes the drawn pages as BMP files (for a look next to work/ref/NAT*.png).
void dump(const ClassicCanvas& c, const char* name) {
    const char* dir = std::getenv("EM_WM_DUMP");
    if (!dir) return;
    auto px = c.argb();
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), 320, 240, 32, 320 * 4, SDL_PIXELFORMAT_ARGB8888);
    SDL_SaveBMP(s, (std::string(dir) + "/" + name + ".bmp").c_str());
    SDL_FreeSurface(s);
}
// Plays until the WM is over and the season has changed (at most `limit` steps).
void play_to_next_preparation(Game& g, int limit) {
    const int year = g.save().year;
    for (int i = 0; i < limit && g.save().year == year; i++) g.advance_day();
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("wm_mode_tests DATA NATIONAL_TEAM_DIR OUTPUT");
        const fs::path data = argv[1], national = argv[2];
        const fs::path output = fs::path(argv[3]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(output);
        const Bytes exe = read_file(data / "EM.EXE");
        const Database db = decode_db(read_file(data / "KB" / "23.vga"));
        const HokFile base = decode_hok(read_file(*find_new_game_base(data)));

        // --- rules from the listing -------------------------------------------------------------------------
        check(native_game_mode(kStartModeBoth) == 4 && native_game_mode(kStartModeWm) == 1 &&
                  native_game_mode(kStartModeLeague) == 2 && native_game_mode(0) == 0, "DS:D0B9 from var_57 (ovr027:1808)");
        check(start_menu_highscore(kStartModeWm, {0, 3, 2, 2}), "Nur WM always keeps a list");
        check(!start_menu_highscore(kStartModeBoth, {2, 0, 0, 0}) && start_menu_highscore(kStartModeBoth, {3, 0, 0, 0}),
              "the notepad needs level > 2 (ovr027:096D)");
        const MenuAvailability menu = load_menu_availability(exe);
        for (int i = 0; i < 42; i++) check(menu.table[0][size_t(i)] == 0, "club table DS:5159 is all on");
        const std::array<uint8_t, 42> national_table{1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1,
                                                     1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        check(menu.table[1] == national_table, "national table DS:5183");
        BorlandRandom r1(7);
        check(friendly_bonus(3, 0, false, r1) == 0, "q 0 home: 3 - 1 - 2 = 0");
        check(friendly_bonus(0, 4, false, r1) == 3, "q 4: clamped to 1..2, plus one for q > 3");
        check(friendly_bonus(-5, 3, false, r1) == 1, "q 3: at least 1");

        // --- programmatic run ---------------------------------------------------------------------------------
        int koeln = -1;
        for (int t = 0; t < 48; t++)
            if (base.teams.at(size_t(t)).name == "KÖLNER EC") koeln = t;
        require(koeln >= 0, "Köln in the start state");
        const HokFile start = start_wm_only_game(base, db, {"AGENT", 4, koeln});
        check(start.state.at(0x025A) == 1 && le16(start.state, 0x06AE) == 0x7530, "mode byte 1 and list H.30000");
        Game g(exe, encode_hok(start), 3);
        g.set_database(db);
        const SeasonCalendar cal = load_season_calendar(exe);
        const int first_wm = cal.nth_day(kEventWorldChampionship, 1);
        const int nat = g.national_team();
        std::cerr << "nation " << nat << " " << (nat >= 0 ? g.save().teams[size_t(nat)].name : "") << "\n";
        check(g.wm_only() && g.national_coach() == 0 && nat == start.state.at(0x068C) && nat >= 52 && nat < 64, "manager 0 is the national coach");
        auto other = [&](int k) { int t = 52 + k % 12; return t == nat ? 52 + (k + 1) % 12 : t; };
        check(g.season_day() + 1 == first_wm && g.date().month == 4, "the club calendar is skipped to the WM");
        check(national_account(g.save()) == 1500000, "first grant 500.000 DM on the national account");
        check(!g.wm_started() && nominees(g.save()) == 0, "no squad yet (NATSTART_empty_squad)");
        check(menu_item_disabled(menu, g.save(), true, 1, 0, 0, false) && !menu_item_disabled(menu, g.save(), true, 1, 0, 5, false),
              "Manager: only the highscore");
        check(menu_item_disabled(menu, g.save(), true, 1, 1, 2, false) && !menu_item_disabled(menu, g.save(), true, 1, 1, 3, false),
              "Mannschaft without Verträge");
        check(menu_item_disabled(menu, g.save(), true, 1, 3, 0, false) && !menu_item_disabled(menu, g.save(), true, 1, 3, 5, false),
              "Tabellen: only WM");
        check(!menu_item_disabled(menu, g.save(), true, 1, 5, 3, false) && menu_item_disabled(menu, g.save(), true, 1, 5, 0, false),
              "Stärken: only WM");

        // a friendly needs nominees for the bonus; the squad first
        check(g.auto_select_national_squad() && nominees(g.save()) == 29, "auto: 29 nominees");
        check(g.enter_friendlies(), "friendlies open before the WM");
        bool exhausted = true;
        for (int slot = 0; slot < 29; slot++) exhausted &= g.save().state.at(squad_block(5) + size_t(slot) * 0x34 + 0x13) == 70;
        check(exhausted, "opening the friendlies sets +0x13 = 70");
        check(g.play_friendly(nat, true).refusal.empty() && !g.play_friendly(nat, true).played, "not against the own nation");
        const long before = national_account(g.save());
        const auto f1 = g.play_friendly(other(0), false);  // away: base price
        check(f1.played && f1.price == friendly_price(g.save(), other(0), false) && national_account(g.save()) == before - f1.price, "away friendly costs the table price");
        check(f1.home == other(0) && f1.away == nat, "away game: the opponent is at home");
        const auto f2 = g.play_friendly(other(8), true);  // home: double
        check(f2.played && f2.price == 2 * friendly_price(g.save(), other(8), false), "home friendly costs twice the price");
        const auto records = friendly_records(g.save());
        check(records.size() == 2 && records[1].home == nat && records[1].home_goals == f2.home_goals, "results kept in 0x068E..");
        for (int i = 2; i < 8; i++) check(g.play_friendly(other(i), false).played, "eight friendlies");
        check(g.play_friendly(other(1), false).refusal == "Nicht mehr als\n8 Freundschaftsspiele", "a ninth is refused");

        // the tournament: the first step plays WM day 1
        const auto years_before = contract_years(g.save());
        const long after_friendlies = national_account(g.save());
        g.advance_day();
        check(g.wm_started() && g.season_day() == first_wm, "the flag plays the first WM day");
        check(menu_item_disabled(menu, g.save(), true, 1, 1, 3, true) && menu_item_disabled(menu, g.save(), true, 1, 1, 4, true),
              "Transfer and Training closed during the WM");
        Game::FriendlyOutcome late = g.play_friendly(other(1), false);
        check(!late.played, "no friendlies after the start");
        for (int i = 0; i < 40 && g.world_ranking().empty(); i++) g.advance_day();
        require(g.world_ranking().size() == 8, "the WM was played to the end");
        const auto ranking = g.world_ranking();
        int place = 11;
        for (size_t k = 0; k < ranking.size(); k++)
            if (ranking[k] == nat) place = int(k) + 1;
        play_to_next_preparation(g, 40);
        check(g.save().year == start.year + 1, "the season changed after the WM");
        check(g.national_coach() == 0 && !g.wm_started() && g.season_day() + 1 == first_wm, "next year: appointed again, at the WM");
        check(g.friendlies_played() == 0, "friendly counter reset");
        const long grant = place <= 8 ? (1000000 - 100000 * (place - 1)) * 3L : 300000;
        check(national_account(g.save()) == after_friendlies + grant, "next appointment: the rest plus the placement grant (DS:029C)");
        std::cerr << "WM place " << place << ", national account " << national_account(g.save()) / 3 << " DM (grant "
                  << grant / 3 << ")\n";
        check(contract_years(g.save()) == years_before, "contract years unchanged in WM-only (ovr026:05D9)");

        // --- line overview against NATAUTO ---------------------------------------------------------------------
        {
            Game n(exe, read_file(national / "NATAUTO_native.hok"));
            const auto lines = n.line_overview(n.save().state.at(0x068C));
            const std::array<std::array<int, 3>, 2> expected{{{90, 88, 86}, {89, 83, 86}}};
            if (lines != expected)
                std::cerr << "line overview " << lines[0][0] << " " << lines[0][1] << " " << lines[0][2] << " / " << lines[1][0] << " "
                          << lines[1][1] << " " << lines[1][2] << "\n";
            check(lines == expected, "ABWEHR/STURM box as in NATAUTO_native.png");
        }

        // --- scripted start through the application -------------------------------------------------------------
        {
            ClassicApplication app(data, output / "wmstart", fs::path{});
            require(app.screen() == ClassicScreen::NewManager && !app.session().loaded(), "start menu");
            app.click(200, 52, false);  // the Liga/Cup icon off: only WM remains
            app.click(25, 60, false);   // face column, row 0
            app.click(210, 140, false); // face
            app.text_input("HANS");
            app.key(SDLK_RETURN);
            app.click(156, 60, false);  // crest column
            app.click(210, 60, false);  // a club
            app.click(282, 212, false); // Los jetzt!
            require(app.session().loaded(), "Los jetzt! starts the WM game");
            const Game& ag = app.session().game();
            check(ag.wm_only() && ag.national_coach() == 0, "the application started a WM-only game");
            check(app.screen() == ClassicScreen::Main && app.probe().modal.empty(), "no cup draw question in Nur WM");
            app.draw(); dump(app.canvas(), "main");
            app.click(295, 55, false);  // the flag with an empty squad
            check(!app.advancing() && app.probe().text.find("SO wenigen") != std::string::npos, "the WM needs ten players");
            app.key(SDLK_ESCAPE);
            app.click(150, 100, false);  // Mannschaft
            app.draw(); dump(app.canvas(), "main_team");
            app.click(3 + 53 * 2 + 20, 220, false);  // Verträge: night sky
            check(app.screen() == ClassicScreen::Main, "Verträge is closed");
            app.click(3 + 53 * 3 + 20, 220, false);  // Transfer -> WM squad
            check(app.screen() == ClassicScreen::WmSquad, "Transfer opens the WM squad");
            app.draw(); dump(app.canvas(), "squad_empty");
            app.click(0x85 + 20, 0xA5 + 5, false);  // auto
            check(nominees(app.session().game().save()) == 29, "auto in the squad page");
            app.draw(); dump(app.canvas(), "squad_auto");
            app.click(10, 10, true);
            app.click(150, 100, false);
            app.click(3 + 53 * 4 + 20, 220, false);  // Training -> friendlies
            check(app.screen() == ClassicScreen::Friendlies, "Training opens the friendlies");
            app.draw(); dump(app.canvas(), "friendlies0");
            app.click(0x15 + 20, 0x1C + 20, false);  // flag 0 (52): select
            app.draw(); dump(app.canvas(), "friendlies");
            app.click(0x15 + 20, 0x1C + 20, false);  // play
            check(app.session().game().friendlies_played() == 1, "a friendly from the page");
            app.key(SDLK_ESCAPE);
            app.click(10, 10, true);
            check(app.screen() == ClassicScreen::Main, "back to the menu");
            app.click(295, 55, false);  // the flag
            check(app.advancing(), "the WM starts with 29 nominees");
        }
        if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
        std::cout << "wm_mode: ok\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "wm_mode_tests: " << e.what() << "\n";
        return 1;
    }
}
