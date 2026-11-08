// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_match_view.hpp"

#include <algorithm>

#include "game/game.hpp"
#include "ui/classic_cup.hpp"

namespace em::ui {
namespace {
constexpr auto kMini = ClassicFontId::Mini;        // native font 3
constexpr auto kBold = ClassicFontId::NewBold;     // native font 1
constexpr int kBoardKb = 19, kSpriteKb = 38, kKey = 0xC0;
constexpr int kBoardStep = 45;
constexpr uint8_t kBarColors[2] = {0x18, 0x13};    // DS:5425 / DS:5427

// ovl14_1540: horizontal bar, background 0x11 unless full, two lines x..x+value in DS:5427.
void hbar(ClassicCanvas& c, int x, int y, int length, int value) {
    value = std::min(value, length);
    if (value < length) c.rect(x, y, x + length, y + 1, 0x11);
    if (value > 0)
        for (int row = 0; row < 2; row++) c.line(x, y + row, x + value, y + row, kBarColors[row]);
}
// ovl14_1477: vertical bar in the columns x+2 / x+3, filled from the bottom (DS:5425), the rest 0x12.
void vbar(ClassicCanvas& c, int x, int y, int length, int value) {
    value = std::min(value, length);
    if (value > 0)
        for (int col = 0; col < 2; col++) c.line(x + col + 2, y + length, x + col + 2, y + length - value - 1, kBarColors[col]);
    if (value < length)
        for (int col = 0; col < 2; col++) c.line(x + col + 2, y, x + col + 2, y + length - value, 0x12);
}
// ovl14_15c3: a lamp; off a 5 x 5 square of colour 0x10, on the KB38 sprite at (0x4C, 10).
void lamp(ClassicCanvas& c, int x, int y, bool on) {
    if (!on) c.rect(x, y + 1, x + 4, y + 5, 0x10);
    else c.blit(kSpriteKb, x + 1, y + 2, 0x4C, 10, 4, 4, true, kKey);
}
// ovl14_13a4: one KB38 digit (8 x 10). Live mode puts it on the clean screen, the plain mode on a colour 3 cell.
void digit(ClassicCanvas& c, bool plain, int value, int x, int y) {
    if (plain) c.rect(x, y, x + 7, y + 9, 3);
    c.blit(kSpriteKb, x, y, value * 8, 0, 8, 10, true, 0);
}
// ovl14_0300: two two-digit numbers (score).
void score(ClassicCanvas& c, int home, int away, int x, int y) {
    digit(c, false, home / 10 % 10, x, y);
    digit(c, false, home % 10, x + 9, y);
    digit(c, false, away / 10 % 10, x + 0x15, y);
    digit(c, false, away % 10, x + 0x1E, y);
}
// ovl14_0000: the minute panel (three period bars, minute in two digits).
void minute_panel(ClassicCanvas& c, bool plain, int minute) {
    int rest = minute;
    for (int k = 0; k < 3; k++) {
        hbar(c, 9, 0xC0 + 8 * k, 0x4F, rest * 4);
        rest = std::max(0, rest - 20);
    }
    digit(c, plain, minute / 10 % 10, 0x60, 0xC4);
    digit(c, plain, minute % 10, 0x6A, 0xC4);
}
// 199F:0164 with DS:038D = 0 (thousands separators) for the attendance.
std::string thousands(long value) {
    std::string s = std::to_string(value);
    for (int n = int(s.size()) - 3; n > 0; n -= 3) s.insert(size_t(n), ".");
    return s;
}
void copy(ClassicCanvas& c, const Image& from, int sx, int sy, int x, int y, int w, int h) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) {
            const int dx = x + col, dy = y + row;
            if (dx < 0 || dy < 0 || dx >= 320 || dy >= 240) continue;
            const uint8_t v = from.pixels[size_t(sy + row) * size_t(from.width) + size_t(sx + col)];
            if (v) c.pixels().at(dx, dy) = v;
        }
}

// ovl14_16ac: the static part of one board.
void board_frame(ClassicCanvas& c, const ClassicBoard& b, int y0) {
    c.blit(kBoardKb, 3, y0 - 3, 0, 0, 0x139, 0x2C, true, kKey);
    c.blit(b.team[0].crest, 0x60, y0, 0, 0, 0x26, 0x26, true, kKey);
    c.text(kMini, 5, y0 + 4, b.team[0].name, 7, 0, 0x60);
    c.blit(b.team[1].crest, 0xB6, y0, 0, 0, 0x26, 0x26, true, kKey);
    c.text(kMini, 0xE0, y0 + 4, b.team[1].name, 7, 0, 0x13D);
    long value = b.attendance;
    if (value > 200000) value -= 200000;
    const std::string text = value != 0 ? thousands(value) : "(ausw.)";
    c.text(kMini, 0x30, y0 + 0x26, text, b.sold_out ? 0x10 : 0, -1, 0x52);
}

// ovl14_0d46: the live part of one board.
void board_live(ClassicCanvas& c, const ClassicBoard& b, int index) {
    const int y = index * kBoardStep + 0xF;
    score(c, b.goals[0] % 30, b.goals[1] % 30, 0x8B, index * kBoardStep + 6);
    for (int side = 0; side < 2; side++) {
        const ClassicBoardTeam& t = b.team[size_t(side)];
        const int x = 0xD + side * 0xE3;
        for (int kind = 0; kind < 2; kind++) {
            for (int k = 0; k < 4; k++) lamp(c, x + kind * 8 + 8, y + k * 7 - 1, t.line[kind] == k);
            vbar(c, x + kind * 0x18, y, 0x19, std::max(0, (t.energy[kind] - 0x32) / 2));
        }
        // the digits go through a 97 x 25 work buffer (DGROUP 6727, font 3, cleared to 0) and are copied out
        // (sub_21D23 skips colour 0, so only the glyphs reach the screen)
        Image work(0x61, 0x19, 0);
        const ClassicFont& f = c.font();
        f.draw(work, kMini, 0, 5, std::to_string(t.skaters), 7);
        f.draw(work, kMini, 0, 0xC, std::to_string(t.penalty_code & 7), 7);
        f.draw(work, kMini, 0, 0x13, std::to_string(t.penalty_code >> 4 & 7), 7);
        for (int p = 0; p < 3; p++) f.draw(work, kMini, 0x14, p * 8 + 5, std::to_string(std::min(t.chances[p], 9)), 7);
        const std::string minutes = std::to_string(t.penalty_minutes) + " min.";
        f.draw(work, kMini, 0x28 + (0x4A - 0x28) / 2 - f.measure(kMini, minutes) / 2, 5, minutes, 0x10);
        if (side == 0) {
            for (int p = 0; p < 3; p++)
                f.draw(work, kMini, 0x4B, p * 8 + 5,
                       std::to_string(std::min(b.period_goals[p][0], 9)) + ":" + std::to_string(std::min(b.period_goals[p][1], 9)), 7);
        }
        hbar(c, x + 0x27, y + 9, 0x19, t.effort * 0x19 / 100);
        hbar(c, x + 0x27, y + 0x10, 0x19, t.offence * 0x19 / 7);
        copy(c, work, 0, 0, 0x88 + side * 0x22 + 3, y + 6, 4, 0x14);
        copy(c, work, 0x14, 0, 0x56 + side * 0x88 + 2, y + 5, 4, 0x17);
        copy(c, work, 0x28, 0, side * 0xE3 + 0x30, y - 3, 0x22, 6);
        if (side == 0) copy(c, work, 0x4B, 0, 0x99, y + 4, 0xE, 0x17);
    }
}
}  // namespace

void draw_classic_match_view(ClassicCanvas& c, const ClassicMatchFrame& frame) {
    c.background(22);
    if (frame.boards.empty()) {
        // ovr030:2642: "Spiele laufen" (DS:0620) centred in font 1, a plain panel and the minute display
        c.text(kBold, 0, 0x50, "Spiele laufen", 0x1E, 0, 0x13F);
        c.outline(3, 0xB7, 0x7B, 0xDA, 0);
        c.rect(4, 0xB8, 0x7A, 0xD9, 3);
        minute_panel(c, true, frame.minute);
        return;
    }
    c.blit(kBoardKb, 3, 0xB6, 0, 0x2D, 0x7A, 0x26, true, kKey);
    c.blit(kBoardKb, 0xC2, 0xB4, 0xBE, 0x3C, 0x7A, 0x26, true, kKey);
    c.blit(kBoardKb, 0x81, 0xBB, 0x7B, 0x2D, 0x3F, 0x35, true, 0);
    const size_t boards = std::min<size_t>(frame.boards.size(), 4);
    for (size_t i = 0; i < boards; i++) board_frame(c, frame.boards[i], 4 + kBoardStep * int(i));
    c.blit(kBoardKb, 3, 0xE2, 0xBB, 0x2D, 0x15, 0xC, true, kKey);
    for (size_t i = 0; i < boards; i++) board_live(c, frame.boards[i], int(i));
    minute_panel(c, false, frame.minute);
    lamp(c, 10, 0xE4, frame.settings_lamp);
    c.blit(kSpriteKb, 0x9A, 0xCB, frame.blink ? 0x80 : 0x50, 0, 0xC, 4, true, kKey);
}

void draw_classic_cup_minute(ClassicCanvas& c, const HokFile& before_day, const SeasonCalendar& calendar, int minute,
                             const std::vector<ClassicCupLive>& live) {
    ClassicCupView view = classic_cup(before_day, calendar);
    const size_t title = view.native_caption.find("   ");
    view.native_caption = view.native_caption.substr(0, title) + "        " + std::to_string(minute) + ".Minute";  // ovr029:3D2C: title, DS:4E5F (7 blanks), number, ".", DS:279B; 8 blanks after our trimmed title (raster test)
    const int game = int(static_cast<int8_t>(before_day.state.at(0x031B)));
    const auto number = [](int v) { std::string s = std::to_string(v); if (s.size() < 2) s.insert(0, 2 - s.size(), '%'); return s; };
    for (auto& group : view.groups)
        for (auto& row : group.rows)
            for (const ClassicCupLive& m : live) {
                const bool same = m.home == row.home && m.away == row.away, swapped = m.home == row.away && m.away == row.home;
                if (!same && !swapped) continue;
                const int left = same ? m.goals[0] : m.goals[1], right = same ? m.goals[1] : m.goals[0];
                const int j = std::max(0, game);
                row.scores.push_back({187 + (3 - std::max(0, game)) * 10 + 36 * j, m.markers, uint8_t(j % 2 ? 30 : 1),
                                      number(left) + ":" + number(right)});
            }
    draw_classic_cup_view(c, view);
}

std::vector<ClassicCupLive> classic_cup_live(int minute, const std::vector<LiveMatch>& matches) {
    std::vector<ClassicCupLive> out;
    for (const LiveMatch& m : matches) {
        const MatchProgress p = m.run->progress();
        ClassicCupLive c;
        c.home = m.home; c.away = m.away;
        c.goals[0] = p.score.home; c.goals[1] = p.score.away;
        if (minute > 60 && p.score.decision_marker >= 30) c.markers = p.score.decision_marker >= 60 ? 2 : 1;
        out.push_back(c);
    }
    return out;
}

int classic_match_minute_ms(int speed) {
    constexpr int kDrawing = 80;  // one minute of drawing in DOSBox-X (60 minutes in about 5 s at speed 70)
    if (speed >= 70) return kDrawing;
    return kDrawing + (30 - speed / 2) * 55 / 3;
}

ClassicMatchFrame classic_match_frame(const Game& game, int minute, const std::vector<LiveMatch>& matches, long attendance,
                                      bool sold_out) {
    ClassicMatchFrame frame;
    frame.minute = std::max(1, minute);
    const HokFile& h = game.save();
    for (const LiveMatch& m : matches) {
        if (frame.boards.size() >= 4) break;
        const auto reported = [&](int team) { return (h.state.at(0x17E7 + size_t(team) * 61 + 0x34) & 0x80) != 0; };
        if (!reported(m.home) && !reported(m.away)) continue;
        ClassicBoard b;
        const MatchProgress p = m.run->progress();
        b.goals[0] = p.score.home;
        b.goals[1] = p.score.away;
        for (int k = 0; k < 3; k++) for (int s = 0; s < 2; s++) b.period_goals[k][s] = p.period_goals[k][s];
        for (int side = 0; side < 2; side++) {
            const int team = side == 0 ? m.home : m.away;
            const HokTeam& live = side == 0 ? m.run->home_state() : m.run->away_state();
            ClassicBoardTeam& t = b.team[size_t(side)];
            t.name = live.name;
            t.crest = team < 64 ? 80 + (h.state.at(0x17E7 + size_t(team) * 61 + 0x34) & 63) : 143;
            for (int kind = 0; kind < 2; kind++) {
                t.line[kind] = live.current_line[kind] & 3;
                t.energy[kind] = live.energy[kind][size_t(live.current_line[kind] & 3)];
            }
            t.skaters = live.skaters;
            t.penalty_code = p.penalty_code[side];
            t.effort = live.effort_current;
            t.offence = live.flags >> 4 & 7;
            for (int k = 0; k < 3; k++) t.chances[k] = p.period_chances[k][side];
            t.penalty_minutes = p.penalty_minutes[side];
        }
        if (m.home == game.human_team()) { b.attendance = attendance; b.sold_out = sold_out; }
        frame.boards.push_back(std::move(b));
    }
    return frame;
}

bool classic_match_display_wanted(const Game& game, bool human_plays, bool cup) {
    if (human_plays) return true;
    const GameSettings s = game.settings();
    if (cup) return s.flags[10] != 0;
    return s.flags[1] != 0 || s.flags[4] != 0 || s.flags[7] != 0;
}

}  // namespace em::ui
