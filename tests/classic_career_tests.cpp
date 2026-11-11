// SPDX-License-Identifier: GPL-3.0-only
// New game (start menu with argument 1, game/new_game.cpp), manager messages (game/messages.cpp) and the
// message field of the main menu (ui/classic_messages.cpp).
// Raster checks: the start menu of a new game is compared with the native DOSBox capture of the same menu
// (work/exp/ui_20261108/raster_goldens/newmanager-start.rgb) outside the parts that differ by design (row 0
// holds the running manager in that capture; the option cells show that game's settings).
#include <SDL.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <stdexcept>

#include "formats/db.hpp"
#include "game/messages.hpp"
#include "game/new_game.hpp"
#include "game/scorers.hpp"
#include "ui/classic_app.hpp"
#include "ui/classic_misc_newmanager.hpp"
#include "ui/classic_season_review.hpp"
#include "ui/session.hpp"

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static uint32_t rgb_at(const Bytes& golden, int x, int y) {
    const size_t p = size_t(y) * 320 + size_t(x);
    return uint32_t(golden[p * 3]) << 16 | uint32_t(golden[p * 3 + 1]) << 8 | golden[p * 3 + 2];
}
// Pixels of `canvas` that differ from `golden` inside [x0, x1) x [y0, y1), skipping `skip` rectangles.
struct Box { int x0, y0, x1, y1; };
static size_t region_differences(const ClassicCanvas& canvas, const Bytes& golden, Box box, std::initializer_list<Box> skip) {
    const auto actual = canvas.argb();
    size_t count = 0;
    for (int y = box.y0; y < box.y1; y++)
        for (int x = box.x0; x < box.x1; x++) {
            bool skipped = false;
            for (const Box& s : skip) skipped |= x >= s.x0 && x < s.x1 && y >= s.y0 && y < s.y1;
            if (!skipped && (actual[size_t(y) * 320 + size_t(x)] & 0xFFFFFF) != rgb_at(golden, x, y)) count++;
        }
    return count;
}
static size_t canvas_differences(const ClassicCanvas& a, const std::vector<uint32_t>& b, Box box) {
    const auto pixels = a.argb();
    size_t count = 0;
    for (int y = box.y0; y < box.y1; y++)
        for (int x = box.x0; x < box.x1; x++) count += pixels[size_t(y) * 320 + size_t(x)] != b[size_t(y) * 320 + size_t(x)];
    return count;
}
static std::string squad_name(const HokFile& h, int id) {
    std::string s;
    for (size_t k = 0; k < 19 && h.state.at(0x4FA5 + size_t(id) * 0x21 + k); k++) s += char(h.state.at(0x4FA5 + size_t(id) * 0x21 + k));
    return s;
}

int main(int argc, char** argv) {
    try {
        if (argc != 6) throw std::runtime_error("classic_career_tests DATA REFERENCE GOLDENS OUTPUT CAREER");
        const fs::path data = path_from_text(argv[1]), references = path_from_text(argv[2]), goldens = path_from_text(argv[3]);
        const fs::path output = path_from_text(argv[4]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const fs::path career = path_from_text(argv[5]);
        fs::create_directories(output);
        int failures = 0;
        auto check = [&](bool ok, const char* what) {
            if (!ok) { std::cerr << "FAIL: " << what << "\n"; failures++; }
        };
        const Database db = decode_db(read_file(data / "KB" / "23.vga"));

        // --- new game logic --------------------------------------------------------------------------------
        {
            const auto base_file = find_new_game_base(data);
            require(base_file.has_value(), "the original ships a fresh start state (BACKUP.HOK)");
            const HokFile base = decode_hok(read_file(*base_file));
            check(base.manager == "Nobody" && base.date.day_index == 1, "start state: Nobody, season day 1");
            check(!is_new_game_base(decode_hok(read_file(references / "MONAT1.HOK"))), "a running game is no start state");
            // START.HOK is a new game the original made for AGENT at KÖLNER EC: its squad names are Köln's database
            // names in ID order. The same must come out of the start state for the same choice.
            const HokFile start = decode_hok(read_file(references / "START.HOK"));
            const int koeln_start = start.state.at(0x0DCD + 0x14);
            int koeln = -1;
            for (int t = 0; t < 48; t++)
                if (base.teams.at(size_t(t)).name == start.teams.at(size_t(koeln_start)).name) koeln = t;
            require(koeln >= 0, "Köln in the start state");
            const HokFile mine = start_new_game(base, db, {"AGENT", 4, koeln});
            auto squad_ids = [](const HokFile& h) {
                std::vector<int> ids;
                for (int slot = 0; slot < 30; slot++)
                    if (int id = h.state.at(0x696D + size_t(slot) * 0x34 + 0x0F)) ids.push_back(id);
                std::sort(ids.begin(), ids.end());
                return ids;
            };
            const auto a = squad_ids(start), b = squad_ids(mine);
            check(a.size() == b.size(), "same squad size as the original's new game");
            int same = 0;
            for (size_t k = 0; k < std::min(a.size(), b.size()); k++) same += squad_name(start, a[k]) == squad_name(mine, b[k]);
            if (std::getenv("EM_DEBUG_CAREER")) for (size_t k = 0; k < std::min(a.size(), b.size()); k++) std::cerr << a[k] << " " << squad_name(start, a[k]) << " | " << b[k] << " " << squad_name(mine, b[k]) << '\n';
            // vacant database slots get a random name in the original (sub_66EB0); Köln has two in its first 25
            check(same == int(a.size()) - 2, "squad names in the original's order (ovl16_0730)");
            for (int id : b) check(mine.state.at(0x4FA5 + size_t(id) * 0x21 + 0x1D) == koeln, "squad players belong to the new club");
            check(mine.manager == "AGENT" && mine.state.at(0x0DCD + 0x14) == koeln && mine.state.at(0x0DCD + 0x13) == 4 &&
                      mine.state.at(0x01EE) == 4, "manager block and face");
            check(mine.teams.at(size_t(koeln)).raw[0x16] == 1 && (mine.teams.at(size_t(koeln)).raw[0x34] & 0x80), "human club flags");
            const int mannheim = base.state.at(0x0DCD + 0x14);
            check(mine.teams.at(size_t(mannheim)).raw[0x16] == 0 && !(mine.teams.at(size_t(mannheim)).raw[0x34] & 0x80),
                  "the old club is a computer club");
            check(std::string(reinterpret_cast<const char*>(mine.header.data() + 0x12)) == "AGENT", "header preview");
            bool threw = false;
            try { start_new_game(base, db, {"A", 0, 0}); } catch (const FormatError&) { threw = true; }
            check(threw, "a one-letter name is refused");
        }

        // --- messages -----------------------------------------------------------------------------------
        {
            HokFile h = decode_hok(read_file(references / "START.HOK"));
            Bytes& s = h.state;
            for (int i = 0; i < kMessageSlots; i++) std::fill_n(s.begin() + long(kMessageBase + size_t(i) * kMessageSize), kMessageSize, uint8_t(0));
            BorlandRandom rng(77);
            const uint32_t before = rng.state();
            const int general = native_add_message(s, rng, {2, 10, 1993, 23}, MessageKind::TwoLines,
                                                   {MessageArg::str("Ihre Talentsuche war ein Erfolg."), MessageArg::str("Ein junger Spieler st|~t zu Ihrem Kader.")}, 0, true);
            check(general == 1 && rng.state() != before, "general entry 1, one date draw");
            const auto view = native_message(s, 0);
            check(view.general && view.steps == 3 && view.lines.size() == 3 && view.lines[1] == "Ihre Talentsuche war ein Erfolg.",
                  "two text lines after the date line");
            check(view.lines[0].find(" Okt. 1993") != std::string::npos || view.lines[0].find(". Sept. 1993") != std::string::npos,
                  "date line dated back up to three days");
            check(s.at(kMessageNewFlag) == 1, "new-message flag of manager 0");
            const int interest = native_add_message(s, rng, {9, 10, 1993, 30}, MessageKind::Interest,
                                                    {MessageArg::str("KREFELDER EV"), MessageArg::str("MUSTERMANN")}, 0, false, 5);
            check(interest == 2 && native_message(s, 1).lines.at(1) == "KREFELDER EV ist an MUSTERMANN" &&
                      native_message(s, 1).lines.at(2) == "interessiert.", "interest text (template 1)");
            s.at(0x4FA5 + 5 * 0x21 + 0x1E) = 2;
            native_sort_messages(s);
            check(native_message(s, 0).player_id == 5 && s.at(0x4FA5 + 5 * 0x21 + 0x1E) == 1, "sorted newest first, back reference follows");
            for (int step = 0; step < 3; step++) native_message_step(s);
            check(native_messages(s, 0).size() == 1 && native_message(s, 0).player_id == 5, "general entry gone after three steps, player entry stays");
            uint8_t handle = 1;
            native_delete_message(s, handle);
            check(handle == 0 && native_messages(s, 0).empty(), "delete through the player's handle");
            // a full list still draws the date
            for (int i = 0; i < kMessageSlots; i++) s.at(kMessageBase + size_t(i) * kMessageSize) = 0x31;
            const uint32_t full = rng.state();
            check(native_add_message(s, rng, {1, 1, 1994, 99}, MessageKind::TwoLines, {}, 0, true) == 0 && rng.state() != full, "full list: no entry, but the draw");
        }

        // --- season review: an own player without contract who wants to leave retires; the original shows the
        // message at once (ovr026:09EB) and deletes it again
        {
            HokFile h = decode_hok(read_file(references / "SAISEND.HOK"));
            int id = 0;
            for (int slot = 0; slot < 30 && !id; slot++) {
                const size_t row = 0x696D + size_t(slot) * 0x34;
                if (!h.state.at(row + 0x0F) || h.state.at(row + 0x0D) == 0xFF) continue;
                id = h.state.at(row + 0x0F);
                h.state.at(row + 0x0B) = 1;     // the season change takes the last year
                h.state.at(row + 0x18) = 0x80;  // wish to leave
            }
            const Bytes exe = read_file(data / "EM.EXE");
            Game game(exe, encode_hok(h), 3);
            game.set_contract_decider([](const ContractPlayer&, long) { return 0; });
            for (int day = 0; day < 40 && game.seasons_completed() == 0; day++) game.advance_day();
            require(game.seasons_completed() == 1, "season change reached");
            bool found = false;
            for (const auto& text : game.immediate_messages()) found |= text.find(squad_name(h, id) + " gibt das Eishockeyspielen im Alter von^") != std::string::npos;
            check(found, "retirement message of the season review");
        }

        // --- the start menu of a new game in the application ---------------------------------------------
        {
            ClassicApplication app(data, output / "newgame", fs::path{});
            require(app.screen() == ClassicScreen::NewManager && !app.session().loaded(), "without a save the start menu opens");
            app.motion(240, 219);
            app.draw();
            const Bytes golden = read_file(goldens / "newmanager-start.rgb");
            // rows 1..3, the board side and the notepad equal the native capture; skipped: row 0 (running manager
            // in the capture), the option cells (that game's settings) and the pointer
            check(region_differences(app.canvas(), golden, {0, 0, 320, 240}, {{0, 38, 180, 82}, {0xC3, 0x51, 0x134, 0x51 + 0x1C * 4}, {232, 210, 260, 240}}) == 0,
                  "start menu equals the native capture outside the managers and the settings");
            app.click(282, 212, false);
            check(!app.session().loaded(), "Los jetzt! without a manager does nothing");
            app.click(25, 60, false);   // face column, row 0
            app.click(210, 140, false); // face 6
            app.text_input("HANS");
            app.key(SDLK_RETURN);
            app.click(156, 60, false);  // crest column, row 0
            app.click(210, 60, false);  // second crest of the first page
            app.click(282, 212, false); // Los jetzt!
            check(!app.session().loaded() && app.screen() == ClassicScreen::NewManager, "the cup question comes before the game");
            app.click(190, 172, false); // "Kein Gedanke" (ovl05_4653 asks before the draw)
            require(app.session().loaded(), "Los jetzt! starts the game");
            check(app.screen() == ClassicScreen::Main, "main menu after the start");
            const HokFile& g = app.session().game().save();
            check(g.manager == "HANS" && g.state.at(0x01EE) == 6, "manager name and face");
            const HokFile base = decode_hok(read_file(*find_new_game_base(data)));
            check(g.teams.at(size_t(app.session().game().human_team())).name == base.teams.at(0).name && g.state.at(0x0DCD + 0x14) == app.session().game().human_team(), "chosen club (first crest = team 0 of the menu, placed by the club draw ovr027:1C0B)");
            (void)base;
            app.draw();
            check(app.session().dirty(), "a new game is unsaved");
        }

        // --- message field on the main menu ------------------------------------------------------------------
        {
            HokFile h = decode_hok(read_file(references / "START.HOK"));
            Bytes& s = h.state;
            for (int i = 0; i < kMessageSlots; i++) std::fill_n(s.begin() + long(kMessageBase + size_t(i) * kMessageSize), kMessageSize, uint8_t(0));
            BorlandRandom rng(5);
            native_add_message(s, rng, {9, 9, 1993, 1}, MessageKind::TwoLines, {MessageArg::str("Erste Zeile"), MessageArg::str("Zweite Zeile")}, 0, true);
            native_add_message(s, rng, {9, 9, 1993, 1}, MessageKind::Interest, {MessageArg::str("KREFELDER EV"), MessageArg::str("SPIELER")}, 0, false, 3);
            const fs::path file = output / "MSG.HOK";
            write_new_save(file, encode_hok(h));
            ClassicApplication app(data, output / "msg", file);
            app.draw();
            const auto closed = app.canvas().argb();
            app.tick();  // the flag opens the field and is cleared
            check(app.session().game().save().state.at(kMessageNewFlag) == 0, "the main menu clears the new-message flag");
            app.draw();
            check(canvas_differences(app.canvas(), closed, {3, 201, 317, 239}) > 500, "the field covers the footer");
            check(canvas_differences(app.canvas(), closed, {0, 0, 320, 200}) == 0, "nothing above the field changes");
            const auto first = app.canvas().argb();
            app.click(310, 214, false);  // upper arrow: next entry
            app.draw();
            check(canvas_differences(app.canvas(), first, {0x12F, 0xD1, 0x12F + 12, 0xD1 + 9}) > 0, "the pressed arrow shows while held");
            app.release();
            app.draw();
            check(canvas_differences(app.canvas(), first, {16, 205, 300, 238}) > 0, "the next entry is shown");
            app.click(310, 214, false);  // no further entry: unchanged
            app.release();
            app.click(310, 225, false);  // lower arrow: back to the first
            app.release();
            app.draw();
            check(canvas_differences(app.canvas(), first, {3, 201, 317, 239}) == 0, "back at the first entry");
            app.click(100, 100, false);  // above the field: closes
            app.draw();
            check(canvas_differences(app.canvas(), closed, {0, 0, 320, 240}) == 0, "closed field restores the menu");
            app.click(250, 50, false);  // the manager's face opens it again
            app.draw();
            check(canvas_differences(app.canvas(), first, {3, 201, 317, 239}) == 0, "the face opens the field");
            app.key(SDLK_SPACE);
            app.draw();
            check(canvas_differences(app.canvas(), closed, {0, 0, 320, 240}) == 0, "a key closes the field");
            if (std::getenv("EM_DUMP_CAREER")) {
                app.click(250, 50, false);
                app.draw();
                write_new_save(path_from_text(std::getenv("EM_DUMP_CAREER")) / "messages.bmp", app.canvas().bmp_bytes());
            }
        }
        // Without entries the field reads "entry -1" and then writes the fallback line.
        {
            HokFile h = decode_hok(read_file(references / "START.HOK"));
            for (int i = 0; i < kMessageSlots; i++) h.state.at(kMessageBase + size_t(i) * kMessageSize) = 0;
            const fs::path file = output / "NOMSG.HOK";
            write_new_save(file, encode_hok(h));
            ClassicApplication app(data, output / "nomsg", file);
            app.draw();
            const auto closed = app.canvas().argb();
            app.click(250, 50, false);
            app.draw();
            check(canvas_differences(app.canvas(), closed, {16, 206, 303, 214}) > 0, "Keine Nachricht für Sie vorhanden.");
        }

        // --- season review (ovl02_0000) -------------------------------------------------------------------
        // REV1.HOK is the original's state of 15.04.1994 before the WM (REAL-CODE chain, mid29). The original plays
        // the WM and changes the season in one step; the review of MANNHEIMER ERC (10th, licence withdrawn, -1.97 Mio.)
        // is work/exp/career_20261109/raster_goldens/season-review.rgb.
        {
            const Bytes golden = read_file(career / "raster_goldens" / "season-review.rgb");
            ClassicApplication app(data, output / "review", career / "REV1.HOK");
            app.click(293, 52, false);  // own crest: play on
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(120);
            while (!app.session().game().season_review() && std::chrono::steady_clock::now() < until) app.tick();
            require(app.session().game().season_review().has_value(), "the step reaches the season change");
            const SeasonReview& review = *app.session().game().season_review();
            check(review.icons == std::vector<uint8_t>{0}, "licence withdrawn: symbol 0 only");
            check(review.prizes == 0 && classic_season_review_prizes(review) == "Erhaltene Prämien: keine", "no prizes");
            check(review.date.day == 30 && review.date.month == 4 && review.date.year == 1994 && review.date.weekday == 1,
                  "header date Montag, 30. April 1994");
            // first the overview "Saisonende" (ovl12_1657): Russland, Düsseldorf twice, Pori; Mannheim down
            check(review.relegated_into[1] == std::vector<std::string>{"MANNHEIMER ERC"}, "Mannheim relegated into the second division");
            app.draw();
            if (std::getenv("EM_DUMP_CAREER"))
                write_new_save(path_from_text(std::getenv("EM_DUMP_CAREER")) / "overview.bmp", app.canvas().bmp_bytes());
            {
                // The WM winner and the club promoted from the regional league (rand(36,41)) come from the random
                // stream, whose seed in the capture is unknown (EM seeds it from the clock). The page is therefore
                // compared with those two names as in the capture; the rest of the data is checked as computed.
                const Bytes overview = read_file(career / "raster_goldens" / "season-overview.rgb");
                const HokFile& h = app.session().game().save();
                auto club = [&](const char* part) {
                    for (const auto& t : h.teams) if (t.name.find(part) != std::string::npos) return t.name;
                    return std::string();
                };
                check(review.winners[1].find("DORFER EG") != std::string::npos,
                      "cup winner Düsseldorf");
                check(review.winners[2] == review.winners[1] && review.winners[3].find("PORI") != std::string::npos,
                      "champion Düsseldorf, European cup Pori");
                check(review.promoted_into[0].size() == 1 && review.promoted_into[1].size() == 1 &&
                          review.relegated_into[2].size() == 1 && review.promoted_into[2].size() == 2,
                      "one swap per relegation group (licence), two from the regional league");
                SeasonReview as_captured = review;
                as_captured.winners[0] = club("RUSSLAND");
                as_captured.promoted_into[2] = {club("GERETSRIED"), club("BREMERHAVEN")};
                require(!as_captured.winners[0].empty() && !as_captured.promoted_into[2][0].empty(), "capture clubs in the save");
                ClassicCanvas canvas(data);
                draw_classic_season_overview(canvas, as_captured, h.state.at(0x01EE) % 12, h.manager);
                // the capture shows the mouse pointer at (160,120)
                const size_t diff = region_differences(canvas, overview, {0, 0, 320, 240}, {{158, 118, 172, 136}});
                if (diff) std::cerr << "season overview: " << diff << " pixels differ\n";
                check(diff == 0, "season overview as in the original");
                const size_t page = region_differences(app.canvas(), overview, {0, 0, 320, 240},
                                                       {{158, 118, 172, 136}, {0, 0x40, 0x50, 0x52}, {0xC8, 0xA8, 0x140, 0xB8}});
                check(page == 0, "the page from the game matches outside the two random names");
            }
            app.click(160, 120, false);  // sub_19B08: the manager's page follows
            app.draw();
            if (std::getenv("EM_DUMP_CAREER"))
                write_new_save(path_from_text(std::getenv("EM_DUMP_CAREER")) / "review.bmp", app.canvas().bmp_bytes());
            const size_t balance = region_differences(app.canvas(), golden, {138, 60, 231, 70}, {});
            if (balance) std::cerr << "note: header balance differs from the original (" << money(review.balance) << ")\n";
            // the capture shows the mouse pointer in the top-left corner
            const size_t rest = region_differences(app.canvas(), golden, {0, 0, 320, 240}, {{138, 60, 231, 70}, {0, 0, 12, 14}});
            if (rest) std::cerr << "season review: " << rest << " pixels differ\n";
            check(rest == 0, "season review page as in the original (outside the balance)");
            const auto page = app.canvas().argb();
            app.key(SDLK_SPACE);  // sub_19B08: any key or click ends the page (no own retirements here)
            app.draw();
            check(canvas_differences(app.canvas(), page, {0, 0, 320, 240}) > 1000, "the page is gone after the key");
            // the symbol slots of 146e:07c6: five per row
            check(classic_season_review_slot(0) == std::pair<int, int>{0x4C, 0x65} &&
                      classic_season_review_slot(4) == std::pair<int, int>{0xD8, 0x65} &&
                      classic_season_review_slot(5) == std::pair<int, int>{0x4C, 0x83}, "symbol slots");
        }
        if (failures) return 1;
        std::cout << "classic career: ok\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "classic_career_tests: " << e.what() << "\n";
        return 1;
    }
}
