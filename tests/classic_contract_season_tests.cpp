// SPDX-License-Identifier: GPL-3.0-only
// Contract talks of the season change (ovr026 -> ovl11_41db, ui/classic_contracts.cpp) and the "Laden" path of the
// start menu, compared with dosbox-automation captures of the original (work/exp/contract_20261110):
// MID57.HOK (15.04.1995, Mannheimer ERC) played to the season change; for each picture the game state was read from
// the running original (re/out/hok_layout.csv). The original writes the dialog's player into his record while the
// dialog is open (+0x0B = 1, the proposal at +0x28, ovr026 near 5f7c:41db's caller); the list behind it was drawn
// before, so the test puts that record back as the review left it (states/review.state) before drawing.
// Colours: round(v * 255 / 63) of the capture converted to the DOSBox-X scaling (v << 2) | (v >> 4).
#include <SDL.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <stdexcept>

#include "game/contracts.hpp"
#include "game/game.hpp"
#include "ui/classic_app.hpp"
#include "ui/session.hpp"

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;

namespace {
constexpr size_t kSquad = 0x696D, kRecord = 52;

size_t record_of(const Bytes& state, int player_id) {
    for (size_t slot = 0; slot < 29; slot++)
        if (state.at(kSquad + slot * kRecord + 0x0F) == player_id) return kSquad + slot * kRecord;
    throw std::runtime_error("player not in squad");
}

// The state of a capture with the dialog's record as the list shows it (see above).
Bytes list_state(const fs::path& dir, const std::string& name, int slot) {
    Bytes state = read_file(dir / "states" / (name + ".state"));
    const Bytes review = read_file(dir / "states" / "review.state");
    const size_t at = kSquad + size_t(slot) * kRecord;
    const size_t before = record_of(review, state.at(at + 0x0F));
    state.at(at + 0x0B) = 0;
    state.at(at + 0x18) = review.at(before + 0x18);
    state.at(at + 0x2F) = review.at(before + 0x2F);
    for (size_t b = 0; b < 4; b++) {
        state.at(at + 0x28 + b) = review.at(before + 0x28 + b);
        state.at(at + 0x30 + b) = review.at(before + 0x30 + b);
    }
    return state;
}

struct Diff { size_t count = 0; int x0 = 320, y0 = 240, x1 = -1, y1 = -1; };
Diff compare(const ClassicCanvas& canvas, const Bytes& golden) {
    Diff d;
    const auto argb = canvas.argb();
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            const size_t p = size_t(y) * 320 + size_t(x);
            const uint32_t g = uint32_t(golden[p * 3]) << 16 | uint32_t(golden[p * 3 + 1]) << 8 | golden[p * 3 + 2];
            if ((argb[p] & 0xFFFFFF) == g) continue;
            d.count++;
            d.x0 = std::min(d.x0, x); d.y0 = std::min(d.y0, y); d.x1 = std::max(d.x1, x); d.y1 = std::max(d.y1, y);
        }
    return d;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("classic_contract_season_tests DATA REFERENCE OUTPUT");
        const fs::path data = path_from_text(argv[1]), dir = path_from_text(argv[2]);
        const fs::path output = fs::absolute(path_from_text(argv[3])) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(output);
        const Bytes mid57 = read_file(dir / "MID57.HOK");
        int failures = 0;
        auto check = [&](bool ok, const std::string& what) {
            if (!ok) { std::cerr << "FAIL: " << what << "\n"; failures++; }
        };
        int file = 0;
        // A game in the state of capture `name` with the dialog of `slot` open, the steps applied, the cursor at (cx, cy).
        auto picture = [&](const std::string& golden, const std::string& name, int slot, int luck, int cx, int cy,
                           const std::function<void(ClassicApplication&)>& steps) {
            HokFile h = decode_hok(mid57);
            const Bytes state = list_state(dir, name, slot);
            std::copy(state.begin(), state.end(), h.state.begin());
            const fs::path save = output / ("S" + std::to_string(++file) + ".HOK");
            write_new_save(save, encode_hok(h));
            ClassicApplication app(data, output, save);
            app.render_screen("contract-season:" + std::to_string(slot) + ":" + std::to_string(luck));
            if (steps) steps(app);
            app.motion(cx, cy);
            app.draw();
            const Diff d = compare(app.canvas(), read_file(dir / "raster_goldens" / (golden + ".rgb")));
            if (d.count) {
                write_new_save(output / (golden + ".bmp"), app.canvas().bmp_bytes());
                char box[96];
                std::snprintf(box, sizeof box, " (%zu pixels in %d,%d..%d,%d)", d.count, d.x0, d.y0, d.x1, d.y1);
                check(false, golden + box);
            }
        };

        // --- the dialog's numbers against the original's record -------------------------------------------
        const Bytes c0 = read_file(dir / "states" / "c0.state");
        const Bytes years2 = read_file(dir / "states" / "years2.state");
        // The daily mood DS:E1A4 is no part of a save: the one whose two-year demand is the original's (85..115).
        int luck = -1;
        long fee = 0;
        {
            HokFile h = decode_hok(mid57);
            const Bytes state = list_state(dir, "c0", 0);
            std::copy(state.begin(), state.end(), h.state.begin());
            const fs::path save = output / "TABLES.HOK";
            write_new_save(save, encode_hok(h));
            Session session(data);
            session.load(save);
            const HokFile staged = decode_hok(session.game().save_bytes());
            BorlandRandom unused;
            const ContractPlayer cp = contract_player(staged, 0, 0);
            const long proposal = long(int32_t(player_worth(session.contract_tables(), cp, Worth::Salary, unused) *
                                               (147 - int(staged.players.at(size_t(staged.squad_of(0)[0].player)).age)) / 100));
            check(proposal == long(le32(c0, kSquad + 0x28)), "one-year proposal as the original's (ovl02_0000)");
            // The two-year demand of the dialog: season_change_demand, with the mood of the day DS:E1A4 (no part of a
            // save; 0x71 = 113 was read from the running original) and the age of squad slot 3 (the original's di = 3).
            for (int l = 85; l <= 115 && luck < 0; l++)
                if (season_change_demand(session.contract_tables(), staged, 0, cp, 2, l, unused) == long(le32(years2, kSquad + 0x28))) luck = l;
            check(luck == 113, "two-year demand with the mood 113 read from the original (season_change_demand)");
            // Franke's transfer fee when he leaves (ovl11_3d36(record, 0) / 2): 142.000 DM in the capture
            const Bytes leave = list_state(dir, "leave1", 1);
            std::copy(leave.begin(), leave.end(), h.state.begin());
            const fs::path leave_save = output / "LEAVE.HOK";
            write_new_save(leave_save, encode_hok(h));
            Session at_leave(data);
            at_leave.load(leave_save);
            fee = player_worth(at_leave.contract_tables(), contract_player(decode_hok(at_leave.game().save_bytes()), 0, 1), Worth::Value, unused) / 2;
            check(fee == 142000 * 3, "transfer fee of the leaving player (" + std::to_string(fee / 3) + " DM)");
        }
        std::cout << "mood " << luck << "\n";
        const auto edit = [](int x, int y, std::string digits) {
            return [=](ClassicApplication& app) {
                app.click(x, y, false);
                app.release();
                if (!digits.empty()) { app.text_input(digits); app.key(SDLK_RETURN); }
            };
        };
        const auto notice = [](bool behind, std::string text) {
            return [=](ClassicApplication& app) { app.render_screen("season-notice:" + std::string(behind ? "1:" : "0:") + text); };
        };

        // the dialog low (slots 0..11): proposal, years field, two years, insurance
        picture("season-c0", "c0", 0, luck, 160, 120, {});
        picture("season-years-edit", "years-edit", 0, luck, 120, 140, edit(120, 140, ""));
        picture("season-years2", "years2", 0, luck, 120, 140, edit(120, 140, "2"));
        picture("season-insured", "insured", 0, luck, 120, 156, [&](ClassicApplication& app) {
            edit(120, 140, "2")(app);
            app.click(120, 156, false);
            app.release();
        });
        // "Abbruch": the farewell and the fee over the list as it was
        picture("season-c1", "c1", 1, luck, 38, 165, {});
        picture("season-leave1", "leave1", 1, luck, 118, 165, notice(false, "FRANKE\nkehrt Ihrem Verein\nden R\xC3\xBC" "cken..."));
        picture("season-leave2", "leave2", 1, luck, 120, 171, notice(false, "Sie erhalten eine\nAbl\xC3\xB6sesumme von\n" + std::to_string(fee / 3) + " DM"));
        picture("season-c1b", "c1b", 1, luck, 120, 171, {});
        // the dialog high (slot 12 on): an offer of 100 DM is refused over the dialog, then the farewell
        picture("season-top", "top", 12, luck, 38, 165, {});
        picture("season-salary-edit", "salary-edit", 12, luck, 120, 68, edit(120, 68, ""));
        picture("season-salary100", "salary100", 12, luck, 120, 68, edit(120, 68, "100"));
        picture("season-refused", "refused", 12, luck, 38, 85, [&](ClassicApplication& app) {
            edit(120, 68, "100")(app);
            notice(true, "OBRESA\nist nicht an\nIhrem Angebot\ninteressiert.")(app);
        });
        picture("season-refused-leave1", "refused-leave1", 12, luck, 120, 171, notice(false, "OBRESA\nkehrt Ihrem Verein\nden R\xC3\xBC" "cken..."));
        picture("season-top2", "top2", 12, luck, 120, 171, {});

        // --- start menu of a new game: the header opens "Laden" (ovr027:0E87), a right click goes back -------------
        {
            // the game folder of the capture: the original's files, its HIGH folder and MID57.HOK
            const fs::path game = output / "EM";
            fs::copy(data, game, fs::copy_options::recursive);
            fs::create_directories(game / "HIGH");
            write_new_save(game / "MID57.HOK", mid57);
            ClassicApplication app(game, output, {});
            check(app.screen() == ClassicScreen::NewManager, "a fresh start shows the start menu");
            app.motion(160, 120);
            app.draw();
            // SPIEL-LEVEL (211..294, 81..90) is not compared: a fresh original shows the EXE default DS:0118 (level 3),
            // OpenEM the level of BACKUP.HOK (5); the options of a new game are not this branch's (docs/CLASSIC_UI.md)
            const auto differences = [&](const std::string& golden_name, bool skip_level) {
                const Bytes golden = read_file(dir / "raster_goldens" / (golden_name + ".rgb"));
                const auto argb = app.canvas().argb();
                size_t count = 0;
                for (int y = 0; y < 240; y++)
                    for (int x = 0; x < 320; x++) {
                        if (skip_level && x >= 211 && x <= 294 && y >= 81 && y <= 90) continue;
                        const size_t p = size_t(y) * 320 + size_t(x);
                        count += (argb[p] & 0xFFFFFF) != (uint32_t(golden[p * 3]) << 16 | uint32_t(golden[p * 3 + 1]) << 8 | golden[p * 3 + 2]);
                    }
                if (count) write_new_save(output / (golden_name + ".bmp"), app.canvas().bmp_bytes());
                return count;
            };
            check(differences("start-menu", true) == 0, "start menu");
            app.motion(160, 10);
            app.click(160, 10, false);
            app.release();
            check(app.screen() == ClassicScreen::Files, "the header of the start menu opens Laden");
            app.draw();
            check(differences("start-load", false) == 0, "Laden from the start menu");
            app.click(160, 120, true);
            app.release();
            check(app.screen() == ClassicScreen::NewManager, "a right click in Laden goes back to the start menu");
            // loading a save leaves the start menu for the main menu
            app.click(160, 10, false);
            app.release();
            app.click(150, 199, false);
            app.text_input("MID57.HOK");
            app.key(SDLK_RETURN);
            check(app.screen() == ClassicScreen::Main && app.session().loaded(), "a save loaded from the start menu");
            app.draw();
        }

        if (failures) { std::cerr << failures << " failures\n"; return 1; }
        std::cout << "classic_contract_season: all pictures equal\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
