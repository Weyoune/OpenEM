// SPDX-License-Identifier: GPL-3.0-only
// The national coach's menu and pages (career WM appointment and the WM-only mode "Nur WM", docs/logic/WM.md):
//   * main-menu availability: night sky for the items of the tables DS:5159 (ovr034:1E6D, ovr034:2957)
//   * Mannschaft -> Transfer: "Spieler in WM-Kader" (ovl07_29ac)
//   * Mannschaft -> Training: "WM Vorbereitung", the friendlies (ovl07_1f27)
//   * the flag of the main menu refuses to start the WM with fewer than 10 nominees (ovr034:2372)
// Coordinates and colours come from the listing; the pages are compared with the DOSBox captures NATSTART_empty_squad /
// NATAUTO_native (work/ref) by eye only, not pixel-checked (the WM-only start was not captured in DOSBox).
#include <algorithm>
#include <cstdio>

#include "game/manager_layout.hpp"
#include "game/wm_mode.hpp"
#include "ui/classic_app.hpp"

namespace em::ui {

struct ClassicWmModeState {
    std::optional<MenuAvailability> menu;
    int first = 1;            // ovl07_29ac: first listed player id (si), pages of 15
    int selected = -1;        // ovl07_1f27: chosen opponent (var_F, nation index 0..11) or -1
    bool away = false;        // var_6: "Heim" / "Auswärts"
};

namespace {
constexpr auto bold = ClassicFontId::NewBold, mini = ClassicFontId::Mini, flow = ClassicFontId::Flow;
constexpr int kNations = 52;
constexpr size_t kNationalSquad = squad_block(5), kRecord = 0x34;

std::string team_name(const HokFile& h, int team) {
    return team >= 0 && size_t(team) < h.teams.size() ? h.teams[size_t(team)].name : std::string();
}
// sub_48E40: three frame lines (DS:4EDB = 7, 4, 29) and the fill colour 1
void wm_panel(ClassicCanvas& c, int l, int t, int r, int b) {
    constexpr uint8_t colors[3] = {7, 4, 29};
    for (int k = 0; k < 3; k++) c.outline(l + k, t + k, r - k, b - k, colors[k]);
    c.rect(l + 3, t + 3, r - 3, b - 3, 1);
}
// sub_48423: a national flag box (KB80 + team +0x34) with its bevel
void flag_box(ClassicCanvas& c, const HokFile& h, int x, int y, int team) {
    c.rect(x, y, x + 0x29, y + 0x29, 1);
    c.line(x, y, x + 0x29, y, 0x17);
    c.line(x, y, x, y + 0x29, 0x17);
    c.line(x + 0x29, y + 1, x + 0x29, y + 0x29, 0x1D);
    c.line(x + 1, y + 0x29, x + 0x28, y + 0x29, 0x1D);
    c.blit(80 + (h.teams.at(size_t(team)).raw[0x34] & 63), x + 2, y + 2, 0, 0, 0x26, 0x26);
}
void small_button(ClassicCanvas& c, int x, int y, std::string_view text) {  // sub_24D65, 57 x 12
    c.blit(4, x, y, 17, 179, 57, 12, true, 192);
    c.text(mini, x, y + 7, text, 29, 1, x + 59);
}
long national_account(const HokFile& h) { return long(int32_t(le32(h.state, manager_block(5) + 0x18F))); }
int nominees(const HokFile& h) {
    int n = 0;
    for (int slot = 0; slot < 0x1D; slot++) n += h.state.at(kNationalSquad + size_t(slot) * kRecord + 0x0F) != 0;
    return n;
}
}  // namespace

bool ClassicApplication::national_view() const {
    if (!session_.loaded()) return false;
    const Game& g = session_.game();
    return g.national_coach() >= 0 && g.national_coach() == active_manager();
}

bool ClassicApplication::wm_menu_disabled(int menu, int item) const {
    if (!session_.loaded() || menu < 0 || menu > 6) return false;
    auto& state = const_cast<ClassicApplication*>(this)->wm_mode_;
    if (!state) state = std::make_shared<ClassicWmModeState>();
    if (!state->menu) {
        try { state->menu = load_menu_availability(read_file(data_ / "EM.EXE")); }
        catch (const std::exception&) { return false; }
    }
    const Game& g = session_.game();
    return menu_item_disabled(*state->menu, g.save(), national_view(), g.managers(), menu, item, g.wm_started());
}

bool ClassicApplication::wm_menu_route(int item) {
    if (wm_menu_disabled(category_, item)) return true;  // ovr034:2585: a night-sky item ignores the click
    if (!national_view() || category_ != 1) return false;
    if (!wm_mode_) wm_mode_ = std::make_shared<ClassicWmModeState>();
    if (item == 3) { wm_mode_->first = 1; screen_ = ClassicScreen::WmSquad; return true; }  // ovr034:2670
    if (item == 4) {                                                                       // ovr034:2689
        action([](Game& g) { return ActionResult{{}, g.enter_friendlies()}; });
        wm_mode_->selected = -1;
        wm_mode_->away = false;
        screen_ = ClassicScreen::Friendlies;
        return true;
    }
    return false;
}

bool ClassicApplication::wm_flag_blocked() {
    if (!national_view() || nominees(snapshot()) >= 10) return false;
    notice("Wollen Sie die WM wirklich\nmit SO wenigen Spielern bestreiten ?");
    return true;
}

// ---- "Spieler in WM-Kader" (ovl07_29ac)
void ClassicApplication::draw_wm_squad() {
    const HokFile& h = snapshot();
    if (!wm_mode_) wm_mode_ = std::make_shared<ClassicWmModeState>();
    const int first = wm_mode_->first;
    const int nation = h.state.at(0x068C);
    canvas_.background(22);
    canvas_.frame(0x10, 2, 12, 12);
    canvas_.text_native(bold, 0, 0x15, "Spieler in WM-Kader", 7, -1, 0x13F);
    canvas_.line(0x1B, 0x17, 0x124, 0x17, 7);
    wm_panel(canvas_, 0x15, 0x1C, 0x12A, 0xB9);
    canvas_.line(0x20, 0x2D, 0x11F, 0x2D, 7);
    canvas_.text(mini, 0x23, 0x2C, "ART     SPIELER                VEREIN                         KO    TE     FO   GES   Lä-Sp.", 7);
    canvas_.line(0x20, 0x9B, 0x11F, 0x9B, 7);
    canvas_.outline(0x1E, 0xBF, 0x9A, 0xE1, 1);
    canvas_.line(0x1D, 0xC0, 0x1D, 0xE0, 0);
    canvas_.line(0x9B, 0xC0, 0x9B, 0xE0, 0);
    constexpr const char* labels[3] = {"-", "auto", "+"};
    for (int i = 0; i < 3; i++) small_button(canvas_, 0x31 + 0x54 * i, 0xA5, labels[i]);
    constexpr const char* roles[3] = {"TOR", "ABW", "STU"};
    for (int id = first; id < first + 15 && id <= 0xB3; id++) {
        if (size_t(id - 1) >= h.players.size()) break;
        const uint8_t* p = h.players[size_t(id - 1)].raw;
        // colour 7, dark blue 0x1D in the squad (+0x1D = the nation), light blue 3 for foreigners (+0x13)
        uint8_t color = 7;
        if (p[0x1D] == nation) color = 0x1D;
        if (p[0x13]) color = 3;
        const int y = 0x30 + (id - first) * 7 + 5;
        canvas_.text(mini, 0x20 + 1, y, roles[std::min(p[0x18] / 25, 2)], color);
        canvas_.text(mini, 0x20 + 0x14, y, h.players[size_t(id - 1)].name, color);
        canvas_.text(mini, 0x20 + 0x50, y, team_name(h, p[0x1D]), color);
        const int ges = (p[0x15] + p[0x16] + p[0x17]) / 3;
        const std::string stats = std::to_string(p[0x15]) + "    " + std::to_string(p[0x16]) + "    " + std::to_string(p[0x17]) +
                                  "    " + std::to_string(ges) + "     " + std::to_string(p[0x19]);
        canvas_.text(mini, 0x20 + 0xA3, y, stats, color);
    }
    // sub_48C06(nation, 0): the line overview of the nation
    const int x = 0x1F, y0 = 0xC2;
    canvas_.line(x, y0 + 0x10, x + 0x7A, y0 + 0x10, 0);
    canvas_.rect(x, y0 - 1, x + 0x7A, y0 + 6, 4);
    canvas_.text(flow, x, y0 + 6, team_name(h, nation), 0, -1, x + 0x7A);
    canvas_.text(mini, x + 0x0A, y0 + 0x0E, "ART        REIHE:       1.      2.      3.", 0);
    canvas_.rect(x + 0x40, y0 + 0x12, x + 0x7A, y0 + 0x1D, 4);
    const auto lines = session_.game().line_overview(nation);
    constexpr const char* kinds[2] = {"ABWEHR", "STURM"};
    for (int kind = 0; kind < 2; kind++) {
        canvas_.text(mini, x + 4, y0 + 0x16 + 7 * kind, kinds[kind], 1);
        for (int line = 0; line < 3; line++)
            canvas_.text(mini, x + 0x46 + 0x14 * line, y0 + 0x16 + 7 * kind, std::to_string(lines[size_t(kind)][size_t(line)]), 1);
    }
}

void ClassicApplication::click_wm_squad(int x, int y, bool right) {
    if (right) { screen_ = ClassicScreen::Main; return; }  // ovr031:2FF4
    if (!wm_mode_) wm_mode_ = std::make_shared<ClassicWmModeState>();
    int& first = wm_mode_->first;
    if (y >= 0xA5 && y < 0xA5 + 12) {
        if (x >= 0x31 && x < 0x31 + 57) { if (first > 0x0F) first -= 0x0F; return; }      // "-"
        if (x >= 0x85 && x < 0x85 + 57) {                                                   // "auto"
            action([](Game& g) { return ActionResult{{}, g.auto_select_national_squad()}; });
            return;
        }
        if (x >= 0xD9 && x < 0xD9 + 57) { if (first < 0xA5) first += 0x0F; return; }        // "+"
    }
    if (y <= 0x30 || y >= 0x98) return;
    const int id = first + (y - 0x30) / 7;
    if (id > 0xC7 || id < 1) return;
    const HokFile& h = snapshot();
    if (size_t(id - 1) >= h.players.size()) return;
    const uint8_t* p = h.players[size_t(id - 1)].raw;
    const bool in_squad = p[0x1D] == h.state.at(0x068C);
    if (!in_squad && p[0x13]) return;  // a foreigner cannot be called up
    const size_t player = size_t(id - 1);
    action([&](Game& g) { return ActionResult{{}, g.select_national_player(player, !in_squad)}; });
}

// ---- "WM Vorbereitung": friendlies (ovl07_1f27)
void ClassicApplication::draw_friendlies() {
    const HokFile& h = snapshot();
    if (!wm_mode_) wm_mode_ = std::make_shared<ClassicWmModeState>();
    const auto& st = *wm_mode_;
    const int nation = h.state.at(0x068C);
    canvas_.background(22);
    canvas_.frame(0x10, 2, 12, 12);
    canvas_.text_native(bold, 0, 0x15, "WM Vorbereitung", 7, 1, 0x13F);
    canvas_.line(0x1B, 0x17, 0x124, 0x17, 7);
    for (int i = 0; i < 12; i++) flag_box(canvas_, h, 0x15 + 0x2B * (i % 4), 0x1C + 0x2B * (i / 4), kNations + i);
    // the eight results (sub_48E40 with the friendly index): home and away name, score
    const auto records = friendly_records(h);
    for (int i = 0; i < 8; i++) {
        const int l = 0xC1, t = 0x1C + 0x14 * i;
        wm_panel(canvas_, l, t, l + 0x69, t + 0x12);
        if (size_t(i) >= records.size()) continue;
        const auto& r = records[size_t(i)];
        const uint8_t hc = r.home == nation ? 0x11 : 7, ac = r.away == nation ? 0x11 : 7;
        canvas_.text(mini, l, t + 7, team_name(h, r.home), hc, -1, l + 0x3A);
        canvas_.text(mini, l + 0x3C, t + 7, "-", 7);
        canvas_.text(mini, l + 0x38 - 0x30, t + 0x0D, team_name(h, r.away), ac, -1, l + 0x3A);
        canvas_.text(mini, l + 0x4C, t + 7, std::to_string(r.home_goals) + ":" + std::to_string(r.away_goals), 7);
    }
    wm_panel(canvas_, 0x15, 0x9D, 0xBF, 0xBB);
    if (st.selected < 0) {  // var_E: the "Heim" / "Auswärts" field
        canvas_.rect(0x1B, 0xA0, 0xBC, 0xB8, 1);
        canvas_.text(bold, 0x1B, 0xB3, st.away ? "Auswärts" : "Heim", 7, 0, 0xBC);
        return;
    }
    // var_D = 2: opponent, price and account in the field; the line overview of both nations below
    const int opponent = kNations + st.selected;
    canvas_.rect(0x18, 0xA0, 0x18 + 0xA4, 0xA0 + 0x18, 1);
    constexpr const char* labels[3] = {"Gegner", "Kosten", "Kontostand"};  // as in the original (REAL-CODE screen of 13.11.2026)
    for (int i = 0; i < 3; i++) {
        canvas_.text(mini, 0x18 + 3, 0xA0 + 7 + 7 * i, labels[i], 7);
        canvas_.text(mini, 0x18 + 0x33, 0xA0 + 7 + 7 * i, ":", 7);
    }
    canvas_.text(mini, 0x18 + 0x40, 0xA0 + 7, team_name(h, opponent), 7);
    canvas_.text(mini, 0x18 + 0x40, 0xA0 + 0x0E, money(friendly_price(h, opponent, !st.away)), 7);
    canvas_.text(mini, 0x18 + 0x40, 0xA0 + 0x15, money(national_account(h)), 7);
    for (int side = 0; side < 2; side++) {
        const int team = side == 0 ? nation : opponent;
        const int x = 0x1F + 0x86 * side, y0 = 0xC2;
        canvas_.line(x, y0 + 0x10, x + 0x7A, y0 + 0x10, 0);
        canvas_.rect(x, y0 - 1, x + 0x7A, y0 + 6, 4);
        canvas_.text(flow, x, y0 + 6, team_name(h, team), 0, -1, x + 0x7A);
        canvas_.text(mini, x + 0x0A, y0 + 0x0E, "ART        REIHE:       1.      2.      3.", 0);
        canvas_.rect(x + 0x40, y0 + 0x12, x + 0x7A, y0 + 0x1D, 4);
        const auto lines = session_.game().line_overview(team);
        constexpr const char* kinds[2] = {"ABWEHR", "STURM"};
        for (int kind = 0; kind < 2; kind++) {
            canvas_.text(mini, x + 4, y0 + 0x16 + 7 * kind, kinds[kind], 1);
            for (int line = 0; line < 3; line++)
                canvas_.text(mini, x + 0x46 + 0x14 * line, y0 + 0x16 + 7 * kind, std::to_string(lines[size_t(kind)][size_t(line)]), 1);
        }
    }
}

void ClassicApplication::click_friendlies(int x, int y, bool right) {
    if (!wm_mode_) wm_mode_ = std::make_shared<ClassicWmModeState>();
    auto& st = *wm_mode_;
    if (right) {  // ovr031:2976
        if (st.selected >= 0) { st.selected = -1; return; }
        screen_ = ClassicScreen::Main;
        return;
    }
    // ovr031:23D6: the flag cells (0..11) and, as 20, the Heim/Auswärts field
    int cell = -1;
    for (int i = 0; i < 12 && cell < 0; i++) {
        const int l = 0x15 + 0x2B * (i % 4), t = 0x1C + 0x2B * (i / 4);
        if (x >= l && x <= l + 0x2A && y >= t && y <= t + 0x2A) cell = i;
    }
    if (cell < 0 && x >= 0x15 && x <= 0xBF && y >= 0x9D && y <= 0xBB) cell = 20;
    if (cell == 20) {  // ovr031:290C
        st.away = !st.away;
        st.selected = -1;
        return;
    }
    if (cell < 0) return;
    const HokFile& h = snapshot();
    if (kNations + cell == h.state.at(0x068C)) return;  // ovr031:2433: not against the own nation
    if (st.selected != cell) {  // first click: price and strengths (ovl07_1af6 runs the national auto line-up first)
        st.selected = cell;
        action([](Game& g) { return ActionResult{{}, g.preview_friendly()}; });
        return;
    }
    const int opponent = kNations + cell;
    const bool home = !st.away;
    Game::FriendlyOutcome outcome;
    // ovl05_0e35 plays the game through ovl06_161d with the minute display of a WM game (ovl06_081d sets up the screen)
    pending_match_frames_.clear();
    pending_human_match_ = true;
    pending_league_day_ = false;
    pending_event_ = kEventWorldChampionship;
    try {
        session_.apply_observed([&](Game& g) {
            outcome = g.play_friendly(opponent, home);
            return ActionResult{{}, outcome.played};
        });
        invalidate();
    } catch (const std::exception& e) {
        notice(e.what());
        return;
    }
    if (outcome.played) begin_match_view();
    pending_match_frames_.clear();
    st.selected = -1;
    // the original returns to the page; the score stands in the list on the right (no notice)
    if (!outcome.refusal.empty()) notice(outcome.refusal);
}

}  // namespace em::ui
