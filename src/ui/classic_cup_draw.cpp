// SPDX-License-Identifier: GPL-3.0-only
// The live DEB cup draw (docs/CLASSIC_UI.md "DEB-Pokal-Auslosung").
//
// ovl05_4653 (ovr029:4653..492D) draws the board (frame 13 x 12, "DEB-Pokal Auslosung", n / 2 rows of two KB25 bars)
// and asks "An Alle:" whether to follow the draw live; the answer is DS:E068. Asked only with the DEB-Pokal display
// switch on (DS:0227), not in the WM-only mode (DS:D0B9 = 1), with four or more clubs and outside the game set-up
// DS:01DB; for two clubs (final) the flag is set without board or question. With "Aber klar" the draw routine
// (ovl05_4c7b for the first round of a new game, ovl05_536d for the later rounds) uncovers the places in the Borland
// order rand(0, n - 1), drawn again while the place is open (game/competitions.hpp cup_reveal_order); the draws are
// part of the game's random stream, so the answer goes into the game logic (NewGameOptions::cup_live,
// Game::set_cup_draw_decider).
//
// ovl05_492e(place, club, fast, competition) uncovers one place (ovr029:495F..4B91): a 120 x 10 bar of colour 4 with
// "<club> (<league>)" in font 3, centred in 2..0x77 at baseline 7, colour DS:4E17[league] (9/10/11) or 0x18 for a
// human club. Place p lies at x = DS:4DD0[p % 2] (37 / 164), y = 0x80 - (n / 2 * 11) / 2 + p / 2 * 11. The bar rolls in
// from the top over ten steps: in step s the rows 0..s of the new bar stand above the rows 0..8-s of the KB25 bar
// (sub_21BC9 into a work buffer, then onto the screen), two BIOS ticks per step (sub_1ECA5 twice). A mouse press
// (DS:A9E6 = 1 after FUN_199f_0878) makes the rest fast: only the last step, one tick. ovl05_4b97 then writes
// "TASTE DR]CKEN" (DS:2ACF, font 3, colour 7, centred in 0x5A..0xED at baseline 0xDD), waits for a press (sub_19B08)
// and shows the cup page (sub_40A03(competition, 0) = 4996:38F3).
#include "ui/classic_app.hpp"
#include "ui/classic_cup.hpp"

#include <algorithm>

namespace em::ui {
namespace {
constexpr uint64_t kTickMs = 55;                       // one BIOS tick
constexpr std::array<uint8_t, 3> kLeagueColor{9, 10, 11};  // DS:4E17
constexpr size_t kTeams = 0x17E7, kTeamSize = 0x3D;
// sub_42BB5: 1 below DS:07C1 (12), 2 below DS:07C2 (24), 4 up to DS:06AD, else 1; the bar shows (value >> 1) + 1.
int cup_league(int team) { return team < 12 || team > 52 ? 0 : team < 24 ? 1 : 2; }
int place_x(int place) { return place % 2 ? 164 : 37; }
int place_y(int clubs, int place) { return 0x80 - ((clubs >> 1) * 11 >> 1) + place / 2 * 11; }
}  // namespace

struct ClassicApplication::CupLiveDraw {
    std::vector<int> places, order;    // club per place, uncovering order
    std::vector<std::string> labels;   // native "<club> (<league>)" per place
    std::vector<uint8_t> colors;
    size_t shown = 0;                  // places uncovered completely (order[0..shown))
    int step = 0;                      // step of order[shown]
    bool fast = false;
    uint64_t next = 0;
    bool waiting = false;              // "TASTE DR]CKEN"
    bool page = false;                 // in-day: the cup page of the staged game
    bool in_day = false;
    std::unique_ptr<HokFile> after;    // the game after the draw (cup page)
};

void ClassicApplication::install_cup_draw_handlers() {
    session_.set_cup_draw_handlers([this](int clubs) { return cup_draw_question(clubs); },
                                   [this](const Game& game, const std::vector<int>& order, const std::vector<int>& places) {
                                       if (!pump_) return;
                                       start_cup_live(decode_hok(game.save_bytes()), places, order, true);
                                       const bool was = in_day_pump_;
                                       in_day_pump_ = true;
                                       while (cup_live_ && !finished_ && pump_) pump_();
                                       in_day_pump_ = was;
                                   });
}

// Inside advance_day (Game::set_cup_draw_decider): the minutes of the day so far are shown first (the original plays the
// cup games before ovl05_536d), then the board with the question. Without the frontend's pump (headless runs) the
// answer is "Kein Gedanke".
bool ClassicApplication::cup_draw_question(int clubs) {
    if (!pump_) return false;
    struct Guard {  // tick() must not start another day while the frontend loop runs inside this one
        bool& flag; bool was;
        explicit Guard(bool& f) : flag(f), was(f) { flag = true; }
        ~Guard() { flag = was; }
    } guard(in_day_pump_);
    if (live_day_) match_frames_.clear();  // the minutes were shown while they were played
    else {
        begin_match_view();
        while (!match_frames_.empty() && !finished_ && pump_) pump_();
    }
    ask_cup_draw();
    if (!manager_question_) return false;
    manager_question_->cup_clubs = clubs;
    cup_answer_.reset();
    while (manager_question_ && !finished_ && pump_) pump_();
    return cup_answer_.value_or(false);
}

void ClassicApplication::start_cup_live(const HokFile& after, const std::vector<int>& places, const std::vector<int>& order,
                                        bool in_day) {
    if (places.empty() || order.size() != places.size()) return;
    auto c = std::make_shared<CupLiveDraw>();
    c->places = places;
    c->order = order;
    c->in_day = in_day;
    c->after = std::make_unique<HokFile>(clone_hok(after));
    const Bytes& s = after.state;
    const int humans = s.at(0x0DC6);
    for (const int team : places) {
        std::string name;
        for (size_t k = 0; k < 22 && s.at(kTeams + size_t(team) * kTeamSize + k); k++)
            name.push_back(char(s.at(kTeams + size_t(team) * kTeamSize + k)));
        const int league = cup_league(team);
        c->labels.push_back(name + "(" + std::to_string(league + 1) + ")");  // DS:4E79 "(", sub_19CB2, DS:4E7B ")"
        uint8_t color = kLeagueColor[size_t(league)];
        for (int m = 0; m < humans; m++)
            if (s.at(0x0DCD + size_t(m) * 0x1AF + 0x14) == team) color = 0x18;
        c->colors.push_back(color);
    }
    c->next = classic_ticks() + 2 * kTickMs;
    cup_live_ = std::move(c);
}

void ClassicApplication::draw_cup_live() {
    const auto& c = *cup_live_;
    if (c.page) {
        draw_classic_cup(canvas_, *c.after, calendar_);
        return;
    }
    const int clubs = int(c.places.size());
    canvas_.background();
    canvas_.frame(3, 3, 13, 12);
    canvas_.text(ClassicFontId::NewBold, 0, 0x1A, "DEB-Pokal Auslosung", 7, 1, 0x141);
    std::vector<int> step(c.places.size(), -1);  // -1: KB25 bar, 0..9: rolling, 9: uncovered
    for (size_t k = 0; k < c.shown; k++) step[size_t(c.order[k])] = 9;
    if (c.shown < c.order.size()) step[size_t(c.order[c.shown])] = c.step;
    for (int p = 0; p < clubs; p++) {
        const int x = place_x(p), y = place_y(clubs, p), s = step[size_t(p)];
        if (s < 0) {
            canvas_.blit(25, x, y, 0, 0, 0x78, 10, true, 0xC0);
            continue;
        }
        if (s < 9) canvas_.blit(25, x, y + s + 1, 0, 0, 0x78, 9 - s, true, 0xC0);
        canvas_.rect(x, y, x + 0x77, y + s, 4);  // sub_19820 colour 4, rows 0..s
        // the text of the bar, clipped to the rows already rolled in (font 3 lies in rows 0..9 of the bar)
        ClassicCanvas& cv = canvas_;
        std::vector<uint8_t> below;
        for (int row = s + 1; row < 10; row++)
            for (int col = 0; col < 0x78; col++) below.push_back(cv.pixels().at(x + col, y + row));
        canvas_.text_native(ClassicFontId::Mini, x + 2, y + 7, c.labels[size_t(p)], c.colors[size_t(p)], -1, x + 0x77);
        size_t i = 0;
        for (int row = s + 1; row < 10; row++)
            for (int col = 0; col < 0x78; col++) cv.pixels().at(x + col, y + row) = below[i++];
    }
    if (c.waiting) canvas_.text_native(ClassicFontId::Mini, 0x5A, 0xDD, "TASTE DR]CKEN", 7, -1, 0xED);
}

void ClassicApplication::tick_cup_live() {
    auto& c = *cup_live_;
    if (c.waiting || c.page || c.shown >= c.order.size()) {
        c.waiting = !c.page;
        return;
    }
    const uint64_t now = classic_ticks();
    while (!c.waiting && now >= c.next) {
        if (c.fast && c.step < 9) c.step = 9;  // fast: only the last step
        else if (c.step < 9) c.step++;
        if (c.step >= 9) {
            c.shown++;
            c.step = c.fast ? 9 : 0;
            if (c.shown >= c.order.size()) { c.waiting = true; break; }
        }
        c.next += (c.fast ? 1 : 2) * kTickMs;
    }
}

bool ClassicApplication::cup_live_waiting() const { return cup_live_ && cup_live_->waiting && !cup_live_->page; }
bool ClassicApplication::cup_live_page() const { return cup_live_ && cup_live_->page; }

void ClassicApplication::click_cup_live(int, int, bool right) {
    auto& c = *cup_live_;
    if (c.page) {  // the cup page (4996:38F3) ends with the right button, as from the menu
        if (right) cup_live_.reset();
        return;
    }
    if (!c.waiting) {  // ovr029:4B00 DS:A9E6 = 1: the rest goes fast
        c.fast = true;
        return;
    }
    if (c.in_day) {  // sub_40A03 inside the day: the cup page of the game after the draw
        c.page = true;
        return;
    }
    cup_live_.reset();
    open_cup();  // a new game: the cup page, then the main menu
}

// The answer of the start question: a new game waits for it (the live draw takes Borland draws before the market days).
void ClassicApplication::finish_new_game(bool cup_live) {
    if (!pending_start_) return;
    auto pending = std::move(*pending_start_);
    pending_start_.reset();
    pending.options.cup_live = cup_live;
    try {
        session_.start_new_game(pending.managers, 1, &pending.options);
        if (misc_) close_start_menu();
        invalidate();
        screen_ = ClassicScreen::Main;
        table_order_ = session_.game().league(division_).ranking;
        if (cup_live) start_cup_live(snapshot(), session_.start_cup_places(), session_.start_cup_reveal(), false);
    } catch (const std::exception& e) {
        notice(e.what());
    }
}

}  // namespace em::ui
