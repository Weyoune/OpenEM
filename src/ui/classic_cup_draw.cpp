// SPDX-License-Identifier: GPL-3.0-only
// The live DEB cup draw ("DEB-Pokal Auslosung") in the classic UI.
//
// The game draws a board (frame 13 x 12 tiles, title "DEB-Pokal Auslosung", n / 2 rows of two KB/25 bars) and asks
// "An Alle:" whether to follow the draw live. It asks only with the DEB-Pokal display switch on, not in the
// World-Championship-only mode, with four or more clubs and not during the game set-up; for two clubs (the final) the
// answer is "yes" without board or question. With "Aber klar" the places are uncovered in the order produced by
// rand(0, n - 1), drawn again while the chosen place is already open (game/competitions.hpp cup_reveal_order). These
// draws are part of the game's random stream, so the answer is part of the game logic (NewGameOptions::cup_live,
// Game::set_cup_draw_decider) and not merely a display option.
//
// Uncovering one place: a 120 x 10 bar of colour 4 with "<club>(<league>)" in the small font, centred in x 2..0x77 at
// baseline 7, in the league colour 9/10/11 or 0x18 for a club run by a human manager. Place p lies at x = 37 (even p)
// or 164 (odd p), y = 0x80 - (n / 2 * 11) / 2 + p / 2 * 11. The bar rolls in from the top over ten steps: in step s the
// rows 0..s of the new bar stand above the rows 0..8-s of the KB/25 bar, two BIOS timer ticks (55 ms each) per step.
// A mouse press makes the rest fast: only the last step, one tick per place. Afterwards "TASTE DR]CKEN" (colour 7,
// centred in 0x5A..0xED at baseline 0xDD) waits for a press, and then the cup page is shown.
#include "ui/classic_app.hpp"
#include "ui/classic_cup.hpp"

#include <algorithm>
#include <cstdlib>

namespace em::ui {
namespace {
constexpr uint64_t kTickMs = 55;                       // one BIOS tick
constexpr std::array<uint8_t, 3> kLeagueColor{9, 10, 11};  // label colour per league (Bundesliga, Zweite Liga, Oberliga)
constexpr size_t kTeams = 0x17E7, kTeamSize = 0x3D;        // save-state team records (name in the first 22 bytes)
// League of a club for its label, 0-based: the game's league flag is 1 for teams below 12, 2 below 24, 4 up to 52,
// else 1, and the bar shows (flag >> 1) + 1, i.e. 1/2/3.
int cup_league(int team) { return team < 12 || team > 52 ? 0 : team < 24 ? 1 : 2; }
// Top-left corner of place p's bar on the board for a draw of `clubs` clubs.
int place_x(int place) { return place % 2 ? 164 : 37; }
int place_y(int clubs, int place) { return 0x80 - ((clubs >> 1) * 11 >> 1) + place / 2 * 11; }
}  // namespace

// State of a running live cup draw (ClassicApplication::cup_live_).
struct ClassicApplication::CupLiveDraw {
    std::vector<int> places, order;    // club per place, uncovering order
    std::vector<std::string> labels;   // "<club>(<league>)" per place, game encoding
    std::vector<uint8_t> colors;       // label colour per place
    size_t shown = 0;                  // places uncovered completely (order[0..shown))
    int step = 0;                      // roll-in step 0..9 of order[shown]
    bool fast = false;                 // a press made the rest fast
    uint64_t next = 0;                 // time (ms, classic_ticks) of the next step
    bool waiting = false;              // "TASTE DR]CKEN" shown, waiting for a press
    bool page = false;                 // the cup page of the game after the draw is shown
    bool in_day = false;               // the draw runs inside a day advance (not at a new game)
    std::unique_ptr<HokFile> after;    // the game after the draw (cup page)
};

// Connects the game session's cup-draw callbacks: the decider asks the
// question, and the reveal callback runs the animation inside the day by
// pumping the frontend loop until it is finished.
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
// cup games before it draws the next round), then the board with the question. Without the frontend's pump (headless
// runs) the answer is "Kein Gedanke". Returns true to follow the draw live.
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

// Starts the animation. after: the game state after the draw (labels and the
// cup page come from it); places: club per place; order: the order in which
// places are uncovered; in_day: inside a day advance rather than at a new game.
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
        c->labels.push_back(name + "(" + std::to_string(league + 1) + ")");  // name, "(", league number, ")"
        uint8_t color = kLeagueColor[size_t(league)];
        for (int m = 0; m < humans; m++)
            if (s.at(0x0DCD + size_t(m) * 0x1AF + 0x14) == team) color = 0x18;
        c->colors.push_back(color);
    }
    c->next = classic_ticks() + 2 * kTickMs;
    // the final (two clubs): no board and no question; after the draw the original shows the cup page "Endspiel" and waits
    // for a click. The two bars the game briefly draws over the previous screen are not drawn here.
    if (in_day && places.size() == 2 && !std::getenv("EM_OLD_FINALPAGE")) c->page = true;
    cup_live_ = std::move(c);
}

// Draws the board with every place in its current state (covered KB/25 bar,
// rolling in, or uncovered), or the cup page once the draw is done.
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
        canvas_.rect(x, y, x + 0x77, y + s, 4);  // the new bar in colour 4, rows 0..s
        // the text of the bar, clipped to the rows already rolled in (font 3 lies in rows 0..9 of the bar)
        ClassicCanvas& cv = canvas_;
        std::vector<uint8_t> below;
        for (int row = s + 1; row < 10; row++)
            for (int col = 0; col < 0x78; col++) below.push_back(cv.pixels().at(x + col, y + row));
        canvas_.text_native(ClassicFontId::Mini, x + 2, y + 7, c.labels[size_t(p)], c.colors[size_t(p)], 0, x + 0x77);  // with a black shadow, as in the game
        size_t i = 0;
        for (int row = s + 1; row < 10; row++)
            for (int col = 0; col < 0x78; col++) cv.pixels().at(x + col, y + row) = below[i++];
    }
    // At a new game the original leaves a remnant where its question dialog stood: within rows 107..183 the left bars
    // appear 3 pixels wider, i.e. x 157..159 keep the bar-slot texture beside the uncovered places. A display error of
    // the original that only looks broken, so OpenEM leaves it out (shown with classic_original_look).
    if (!c.in_day && classic_original_look()) {
        // the colours of a bar slot (8-bit RGB): top edge 60 80 90, body 30 50 60, bottom edge 20 30 50
        auto index_of = [&](uint8_t r, uint8_t g, uint8_t b) {
            const auto& pal = canvas_.palette().rgb;
            for (size_t i = 0; i < pal.size(); i++)
                if ((pal[i][0] & 0xFC) == r && (pal[i][1] & 0xFC) == g && (pal[i][2] & 0xFC) == b) return int(i);
            return -1;
        };
        const int top = index_of(0x60, 0x80, 0x90), body = index_of(0x30, 0x50, 0x60), bottom = index_of(0x20, 0x30, 0x50);
        if (top >= 0 && body >= 0 && bottom >= 0)
            for (int p = 0; p < clubs; p += 2) {
                const int y0 = place_y(clubs, p);
                for (int row = 0; row < 10; row++) {
                    const int y = y0 + row;
                    if (y < 107 || y > 183) continue;
                    const int colour = row == 0 ? top : row == 9 ? bottom : body;
                    for (int x = 157; x <= 159; x++) canvas_.pixels().at(x, y) = uint8_t(colour);
                }
            }
    }
    if (c.waiting) canvas_.text_native(ClassicFontId::Mini, 0x5A, 0xDD, "TASTE DR]CKEN", 7, 0, 0xED);  // with a black shadow, as in the game
}

// Advances the roll-in animation according to the elapsed time: one step
// per two ticks, or one whole place per tick in fast mode. Sets waiting
// once all places are uncovered.
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

// A press during the animation makes the rest fast; at "TASTE DR]CKEN" it
// continues to the cup page (inside a day) or to the cup page opened from
// the main menu (new game). The cup page closes with the right button.
void ClassicApplication::click_cup_live(int, int, bool right) {
    auto& c = *cup_live_;
    if (c.page) {  // the cup page ends with the right button, as from the menu
        if (right) cup_live_.reset();
        return;
    }
    if (!c.waiting) {  // a press while places are rolling in: the rest goes fast
        c.fast = true;
        return;
    }
    if (c.in_day) {  // inside the day: the cup page of the game after the draw
        c.page = true;
        return;
    }
    cup_live_.reset();
    open_cup();  // a new game: the cup page, then the main menu
}

// The answer of the start question: a new game waits for it, because the live draw consumes random numbers before the
// market days. Starts the pending new game with that answer and, if "yes", runs the live draw of the first round.
void ClassicApplication::finish_new_game(bool cup_live) {
    if (!pending_start_) return;
    auto pending = std::move(*pending_start_);
    pending_start_.reset();
    pending.options.cup_live = cup_live;
    pending.options.intro_draws = new_game_intro_;
    try {
        session_.start_new_game(pending.managers, new_game_seed_, &pending.options);
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
