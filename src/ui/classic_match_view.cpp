// SPDX-License-Identifier: GPL-3.0-only
// Implementation of the classic match display (see classic_match_view.hpp):
// drawing of boards, minute panel, goal flashes and board messages; the
// timing of frames and message steps; inserting flashes, messages and goal
// scenes into the observed minutes; building a minute's frame from the
// running matches; and click handling on the boards.
#include "ui/classic_match_view.hpp"

#include <algorithm>
#include <cstdlib>

#include "game/game.hpp"
#include "ui/classic_cup.hpp"

namespace em::ui {
namespace {
constexpr auto kMini = ClassicFontId::Mini;        // the game's font 3
constexpr auto kBold = ClassicFontId::NewBold;     // the game's font 1
constexpr int kBoardKb = 19, kSpriteKb = 38, kKey = 0xC0;  // board picture KB/19, sprites/digits KB/38, transparency key
constexpr int kBoardStep = 45;                     // vertical distance between boards
constexpr uint8_t kBarColors[2] = {0x18, 0x13};    // the two line colours of a bar

// Horizontal bar: background 0x11 unless full, two lines x..x+value in the bar colours.
void hbar(ClassicCanvas& c, int x, int y, int length, int value) {
    value = std::min(value, length);
    if (value < length) c.rect(x, y, x + length, y + 1, 0x11);
    if (value > 0)
        for (int row = 0; row < 2; row++) c.line(x, y + row, x + value, y + row, kBarColors[row]);
}
// Vertical bar in the columns x+2 / x+3, filled from the bottom in the bar colours, the rest 0x12.
void vbar(ClassicCanvas& c, int x, int y, int length, int value) {
    value = std::min(value, length);
    if (value > 0)
        for (int col = 0; col < 2; col++) c.line(x + col + 2, y + length, x + col + 2, y + length - value - 1, kBarColors[col]);
    if (value < length)
        for (int col = 0; col < 2; col++) c.line(x + col + 2, y, x + col + 2, y + length - value, 0x12);
}
// A lamp; off a 5 x 5 square of colour 0x10, on the KB/38 sprite at (0x4C, 10).
void lamp(ClassicCanvas& c, int x, int y, bool on) {
    if (!on) c.rect(x, y + 1, x + 4, y + 5, 0x10);
    else c.blit(kSpriteKb, x + 1, y + 2, 0x4C, 10, 4, 4, true, kKey);
}
// One KB/38 digit (8 x 10). Live mode puts it on the clean screen, the plain mode on a colour 3 cell.
void digit(ClassicCanvas& c, bool plain, int value, int x, int y) {
    if (plain) c.rect(x, y, x + 7, y + 9, 3);
    c.blit(kSpriteKb, x, y, value * 8, 0, 8, 10, true, 0);
}
// Two two-digit numbers (score).
void score(ClassicCanvas& c, int home, int away, int x, int y) {
    digit(c, false, home / 10 % 10, x, y);
    digit(c, false, home % 10, x + 9, y);
    digit(c, false, away / 10 % 10, x + 0x15, y);
    digit(c, false, away % 10, x + 0x1E, y);
}
// The minute panel: three period bars (20 minutes each, 4 pixels per minute) and the minute in two digits.
void minute_panel(ClassicCanvas& c, bool plain, int minute) {
    int rest = minute;
    for (int k = 0; k < 3; k++) {
        hbar(c, 9, 0xC0 + 8 * k, 0x4F, rest * 4);
        rest = std::max(0, rest - 20);
    }
    digit(c, plain, minute / 10 % 10, 0x60, 0xC4);
    digit(c, plain, minute % 10, 0x6A, 0xC4);
}
// The attendance with '.' as thousands separator, as the game formats it.
std::string thousands(long value) {
    std::string s = std::to_string(value);
    for (int n = int(s.size()) - 3; n > 0; n -= 3) s.insert(size_t(n), ".");
    return s;
}
// Copies a w x h rectangle of a work buffer to the screen, skipping colour 0.
void copy(ClassicCanvas& c, const Image& from, int sx, int sy, int x, int y, int w, int h) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) {
            const int dx = x + col, dy = y + row;
            if (dx < 0 || dy < 0 || dx >= 320 || dy >= 240) continue;
            const uint8_t v = from.pixels[size_t(sy + row) * size_t(from.width) + size_t(sx + col)];
            if (v) c.pixels().at(dx, dy) = v;
        }
}

// The static part of one board: frame, crests, names and attendance ("(ausw.)" for an away game; values above 200000
// have 200000 subtracted; a sold-out arena is shown in colour 0x10).
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

// The live part of one board: score, line lamps and energy bars, skaters, penalty counters, chances per period,
// penalty minutes, period scores, effort and offence bars.
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
        // the digits go through a 97 x 25 work buffer (small font, cleared to 0) and are copied out
        // (the copy skips colour 0, so only the glyphs reach the screen)
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
// The game's centred text on a single column: x = centre - width / 2, moved to 0 when the text would leave the screen.
void centred(ClassicCanvas& c, int centre, int y, const std::string& text) {
    int x = centre - c.font().measure(kMini, text) / 2;
    if (x + c.font().measure(kMini, text) > 0x13F || x < 0) x = 0;
    c.text(kMini, x, y, text, 1, 0);
}

// The goal flash over the message panel, in the small font.
void goal_flash(ClassicCanvas& c, const ClassicGoalFlash& f) {
    centred(c, 0xF5, 0xC0, f.home);
    centred(c, 0x127, 0xC0, "-");
    centred(c, 0x10D, 0xC7, f.away);
    for (int a = 0; a < 4; a += 3) {  // the colon: a 2 x 2 dot in colour 4 on a black shadow, twice (filled boxes)
        c.rect(0x101, 0xCF + a, 0x102, 0xD0 + a, 0);
        c.rect(0x100, 0xCE + a, 0x101, 0xCF + a, 4);
    }
    if (!f.score) return;  // delay(50) before the digits
    int home = f.goals[0];
    while (home > 0x1D) home -= 0x1E;  // only the home goals lose their +30 marker, as in the game
    score(c, home, f.goals[1], 0xEF, 0xCC);
}

// Caption and result text of a board-message kind (see ClassicBoardMessage).
const char* message_caption(int kind) {
    if (kind <= 1) return "Chance:";
    if (kind <= 3) return "Strafzeit:";
    if (kind == 4) return "Verletzt:";
    return "Penalty:";
}
const char* message_result(int kind) {
    static const char* const results[7] = {"Vergeben", "TOR!", "2 Minuten", "5 Minuten", "", "Vergeben", "TOR!"};
    return results[std::clamp(kind, 0, 6)];
}
// The second line of the final step: "Vorlage: <assist>" for chances and injuries, the reason for penalties.
std::string message_second(const ClassicBoardMessage& m) {
    if (m.kind <= 1 || m.kind == 4) return m.second.empty() ? std::string() : "Vorlage: " + m.second;
    if (m.kind <= 3) return m.second;  // the reason of a penalty
    return {};
}
constexpr int kMessageX = 0xC7, kMessageY = 0xB7, kMessageW = 0x6F, kMessageH = 0x20;  // message work buffer on screen

// One board-message step: the work buffer is drawn with centred text between x 0 and 0x6F and copied without colour 0
// over the panel restored from the background page.
void board_message(ClassicCanvas& c, const ClassicBoardMessage& m) {
    Image work(kMessageW, kMessageH, 0);
    const ClassicFont& font = c.font();
    const auto put = [&](ClassicFontId face, int baseline, const std::string& text, uint8_t color) {
        if (text.empty()) return;
        const int width = font.measure(face, text);
        int x = kMessageW / 2 - width / 2;
        if (x < 0 || x + width > 0x13F) x = 0;
        font.draw(work, face, x + 1, baseline + 1, text, 255);  // black shadow (index 255, see ClassicCanvas::text)
        font.draw(work, face, x, baseline, text, color);
    };
    switch (m.step) {
    case ClassicBoardMessage::Step::Caption:
        if (m.text) put(kBold, 0x14, message_caption(m.kind), 0x18);
        break;
    case ClassicBoardMessage::Step::Team:
        put(kBold, 0x14, message_caption(m.kind), 0x18);
        put(kMini, 0x1E, m.team, 7);
        break;
    case ClassicBoardMessage::Step::Result:
        if (m.text) put(kBold, 0x16, message_result(m.kind), 0x18);
        break;
    case ClassicBoardMessage::Step::Final:
        put(kMini, 0x0A, message_result(m.kind), 0x18);
        put(kMini, 0x12, m.player, 0x18);
        if (m.lines > 1) put(kMini, 0x1A, message_second(m), 0x18);
        break;
    }
    copy(c, work, 0, 0, kMessageX, kMessageY, kMessageW, kMessageH);
}

// The reporter's lip-sync list of a text: one entry 0 per vowel (the characters of "25AEIOU" count as vowels; the
// character after a vowel is skipped), at most 0x1C entries, padded with 9 to eight entries.
std::vector<int> lip_sync(const std::string& utf8) {
    const std::string s = ClassicFont::utf8_to_native(utf8);
    std::vector<int> out;
    for (size_t i = 0; i < s.size(); i++) {
        char ch = s[i];
        if (ch >= 'a' && ch <= 'z') ch = char(ch - 0x20);
        for (const char v : std::string("25AEIOU"))
            if (v == ch && out.size() < 0x1C) { i++; out.push_back(0); }
    }
    while (out.size() < 8) out.push_back(9);
    return out;
}
// The game's delay(value) waits value / 3 BIOS ticks; the mouth animation (speed < 41) shows seven mouth sprites and the
// closed mouth, one per vertical retrace (70 Hz).
int units_ms(int units) { return units * 54925 / 3000; }
int mouth_ms(int speed) { return speed < 41 ? 8 * 1000 / 70 : 0; }
// After drawing a line: per lip-sync entry the mouth animation (entry 0) or delay(6), only below speed 41.
int speak_ms(const std::string& text, int speed) {
    if (speed >= 41) return 0;
    int ms = 0;
    for (const int e : lip_sync(text)) ms += e == 0 ? mouth_ms(speed) : units_ms(6);
    return ms;
}
}  // namespace

void draw_classic_match_view(ClassicCanvas& c, const ClassicMatchFrame& frame) {
    if (frame.scene) {
        draw_classic_goal_scene(c, *frame.scene);
        return;
    }
    c.background(22);
    if (frame.boards.empty()) {
        // "Spiele laufen" centred in the bold font, a plain panel and the minute display
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
    if (frame.flash) goal_flash(c, *frame.flash);
    if (frame.message) board_message(c, *frame.message);
}

void draw_classic_cup_minute(ClassicCanvas& c, const HokFile& before_day, const SeasonCalendar& calendar, int minute,
                             const std::vector<ClassicCupLive>& live) {
    ClassicCupView view = classic_cup(before_day, calendar);
    const size_t title = view.native_caption.find("   ");
    view.native_caption = view.native_caption.substr(0, title) + "        " + std::to_string(minute) + ".Minute";  // the game writes title, 7 blanks, number, ".Minute"; 8 blanks here because our title is trimmed
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
    constexpr int kDrawing = 80;  // the time the original needs to draw one minute (60 minutes in about 5 s at speed 70)
    if (speed >= 70) return kDrawing;
    return kDrawing + (30 - speed / 2) * 55 / 3;
}

int classic_match_frame_ms(const ClassicMatchFrame& frame, int speed) {
    // the game's delay(value) waits value / 3 BIOS ticks of 54.925 ms
    if (frame.hold_ms >= 0) return frame.hold_ms;
    if (frame.flash) return (frame.flash->score ? 70 / 3 : 50 / 3) * 54925 / 1000;
    return classic_match_minute_ms(speed);
}

std::vector<ClassicMatchFrame> classic_goal_flashes(std::vector<ClassicMatchFrame> frames) {
    std::vector<ClassicMatchFrame> out;
    std::optional<ClassicMatchFrame> last;  // the last minute frame (cup tables are copies of it)
    for (ClassicMatchFrame& frame : frames) {
        if (frame.waits()) { out.push_back(std::move(frame)); continue; }
        if (last && !frame.boards.empty()) {
            const ClassicMatchFrame* previous = &*last;
            for (const ClassicReportedMatch& m : frame.reported) {
                const auto before = std::find_if(previous->reported.begin(), previous->reported.end(),
                                                 [&](const ClassicReportedMatch& p) { return p.home == m.home && p.away == m.away; });
                int goals[2] = {};
                if (before != previous->reported.end()) { goals[0] = before->goals[0]; goals[1] = before->goals[1]; }
                for (int side = 0; side < 2; side++)
                    while (goals[side] < m.goals[side]) {
                        goals[side]++;
                        ClassicMatchFrame f = *previous;
                        f.minute = frame.minute;
                        f.cup_table = 0;
                        f.cup.clear();
                        f.flash = ClassicGoalFlash{m.names[0], m.names[1], {goals[0], goals[1]}, false};
                        out.push_back(f);
                        f.flash->score = true;
                        out.push_back(std::move(f));
                    }
            }
        }
        last = frame;
        out.push_back(std::move(frame));
    }
    return out;
}

std::vector<ClassicMatchFrame> classic_board_message_steps(const ClassicMatchFrame& base, const ClassicBoardMessage& message,
                                                           int speed, bool goal_scenes) {
    std::vector<ClassicMatchFrame> out;
    if (speed >= 70) return out;
    const auto step = [&](ClassicBoardMessage::Step s, bool text, int lines, int ms) {
        ClassicMatchFrame f = base;
        f.flash.reset();
        f.cup_table = 0;
        f.cup.clear();
        f.message = message;
        f.message->step = s;
        f.message->text = text;
        f.message->lines = lines;
        f.hold_ms = ms;
        out.push_back(std::move(f));
    };
    const int slow = speed == 50 ? 5 : 0;
    if (!goal_scenes) {
        const std::vector<int> caption = lip_sync(message_caption(message.kind));
        const int blinks = 4 - 2 * (speed == 60);
        for (int i = 0; i < blinks; i++)  // odd steps show the caption, even ones the cleared buffer
            step(ClassicBoardMessage::Step::Caption, i % 2 != 0, 1,
                 units_ms(slow + 2) + (caption[size_t(i)] ? units_ms(4) : mouth_ms(speed)));
        out.back().hold_ms += units_ms(80 - speed);
        step(ClassicBoardMessage::Step::Team, true, 1,
             speak_ms(message.team, speed) + (speed == 60 || speed == 50 ? units_ms(25) : 0));
        if (message.kind != 4) {
            const std::vector<int> result = lip_sync(message_result(message.kind));
            const int blinks2 = 4 - 3 * (speed == 60);
            for (int i = 0; i < blinks2; i++)  // even steps show the result
                step(ClassicBoardMessage::Step::Result, i % 2 == 0, 1,
                     units_ms(slow + 1 + (speed == 50)) + (result[size_t(i)] ? units_ms(4) : mouth_ms(speed)));
        }
    }
    // the final lines reach the screen only with a player name
    if (!message.player.empty()) {
        step(ClassicBoardMessage::Step::Final, true, 1, speak_ms(message.player, speed));
        const std::string second = message_second(message);
        if (!second.empty()) step(ClassicBoardMessage::Step::Final, true, 2, speak_ms(second, speed));
    }
    if (!out.empty()) out.back().hold_ms += units_ms(45 - 10 * (speed > 49));
    return out;
}

ClassicGoalScene classic_goal_scene_of(const ClassicBoard& b, int minute) {
    std::array<int, 2> colour{b.team[0].colour, b.team[1].colour};
    int distance = 0;  // clash rule: sum of the nibble differences of the two shirt colours
    for (int shift = 0; shift < 12; shift += 4) distance += std::abs((colour[0] >> shift & 15) - (colour[1] >> shift & 15));
    if (distance < 6) colour[1] = colour[0] ^ 0xFFF;
    ClassicGoalScene s;
    s.arena = b.arena;
    s.colour = colour;
    s.names = {b.team[0].name, b.team[1].name};
    s.goals = {b.goals[0] % 30, b.goals[1] % 30};
    // the attendance of the manager's home game, else "AUSWÄRTS"
    s.info = b.team[0].human ? std::to_string(b.attendance) + " ZUSCHAUER" : "AUSWÄRTS";
    s.minute = minute;
    return s;
}

std::vector<ClassicMatchFrame> classic_board_messages(std::vector<ClassicMatchFrame> frames, int speed, int goal_scenes,
                                                      const std::filesystem::path& data) {
    const bool scenes = goal_scenes && !data.empty();
    // Stufe 2 ("Eisfeld bleibt an"): the game opens the rink once instead of the boards; the minute loop only renews
    // "<m>.MINUTE", the scenes run in place and end without restoring a board; since no board is shown there are
    // neither board messages nor goal flashes.
    const bool rink_stays = scenes && goal_scenes == 2;
    if (speed >= 70 && !scenes) return frames;
    int chance_count = 0;
    std::vector<ClassicMatchFrame> out;
    std::optional<ClassicMatchFrame> last;
    std::optional<ClassicGoalScene> rest;  // Stufe 2: the view between the scenes
    const auto human_board = [](const ClassicMatchFrame& f) -> const ClassicBoard* {  // the match the rink is opened for (first board)
        return f.boards.empty() ? nullptr : &f.boards.front();
    };
    const auto scene_of = [](const ClassicBoard& b, int minute) { return classic_goal_scene_of(b, minute); };
    for (ClassicMatchFrame& frame : frames) {
        if (rink_stays && frame.flash) continue;  // no goal flashes without a board
        // cup tables and goal flashes (classic_goal_flashes runs first) pass unchanged
        if (frame.waits() || frame.flash || frame.message || frame.scene) { out.push_back(std::move(frame)); continue; }
        if (rink_stays && !rest)
            if (const ClassicBoard* b = human_board(frame)) {
                rest = scene_of(*b, frame.minute);  // camera (0, 0), no objects
                rest->info.clear();                // the game opens the rink without the attendance line
            }
        if (last && !frame.boards.empty() && last->boards.size() == frame.boards.size()) {
            ClassicMatchFrame base = *last;
            base.minute = frame.minute;
            for (size_t b = 0; b < frame.boards.size(); b++) {
                const ClassicBoard& now = frame.boards[b];
                const ClassicBoard& before = last->boards[b];
                if (now.team[0].name != before.team[0].name || now.team[1].name != before.team[1].name) continue;
                // the goal scene of every chance of a match on a board
                const bool scene_board = scenes;
                std::array<int, 2> running{before.goals[0], before.goals[1]};
                std::vector<bool> used(now.scenes.size());
                // delay(30) before the scene (on the board, at Stufe 2 on the resting rink), then the scene
                const auto play = [&](const std::string& script, int side, bool goal, int minute) {
                    ClassicGoalScene s = scene_of(now, minute);
                    s.goals = {running[0] % 30, running[1] % 30};
                    s.script = script;
                    ClassicMatchFrame scene_base = base;
                    if (rink_stays) { scene_base.boards.clear(); scene_base.reported.clear(); }
                    ClassicMatchFrame wait = scene_base;
                    wait.message.reset();
                    wait.flash.reset();
                    if (rink_stays && rest) { wait.scene = *rest; wait.scene->minute = minute; wait.scene->final = false; }
                    wait.hold_ms = 30 * 54925 / 3000;
                    out.push_back(std::move(wait));
                    for (ClassicMatchFrame& f : classic_goal_scene_frames(data, scene_base, s, side, goal, rink_stays))
                        out.push_back(std::move(f));
                    if (rink_stays) rest = out.back().scene;
                    chance_count++;
                };
                // the scene the simulation drew for this chance (on the game's random stream), if any
                const auto drawn = [&](int side, bool goal, bool penalty) -> const ClassicBoard::Scene* {
                    for (size_t i = 0; i < now.scenes.size(); i++) {
                        const ClassicBoard::Scene& d = now.scenes[i];
                        if (used[i] || d.side != side || d.goal != goal || d.penalty != penalty) continue;
                        used[i] = true;
                        return &d;
                    }
                    return nullptr;
                };
                for (int side = 0; side < 2; side++) {
                    const ClassicBoardTeam& t = now.team[size_t(side)];
                    const ClassicBoardTeam& p = before.team[size_t(side)];
                    const auto emit = [&](ClassicBoardMessage m) {
                        if (scene_board && m.kind <= 1) {
                            std::string script;
                            bool scene_skip = false;
                            if (const ClassicBoard::Scene* d = drawn(side, m.kind == 1, false)) {
                                if (d->shown) scene_skip = true;  // the live display already showed it (and its replays)
                                script = classic_goal_scene_script(d->number, side, d->goal, d->variant == 'e', d->variant == 'j');
                            } else {
                                // without drawn scenes (a view built without the simulation): a fixed pick;
                                // the j scenes when the attacking side has skaters + running penalties = 6
                                const int penalties = ((t.penalty_code & 7) != 0) + ((t.penalty_code >> 4 & 7) != 0);
                                script = classic_goal_scene_script(2 + (frame.minute * 7 + chance_count * 3 + side) % 10, side,
                                                                   m.kind == 1, false, t.skaters + penalties == 6);
                            }
                            if (!scene_skip) play(script, side, m.kind == 1, frame.minute);
                        }
                        if (m.kind == 1) running[size_t(side)]++;
                        if (speed >= 70 || rink_stays) return;
                        m.team = t.name;
                        for (ClassicMatchFrame& f : classic_board_message_steps(base, m, speed, goal_scenes != 0)) out.push_back(std::move(f));
                    };
                    // the scorer/assist picks made in this minute (manager's club): chances first, then shoot-out shots
                    std::vector<const ClassicBoardTeam::Credit*> chance_credits;
                    for (size_t i = p.credits.size(); i < t.credits.size(); i++)
                        if (!t.credits[i].shot) chance_credits.push_back(&t.credits[i]);
                    size_t next_credit = 0;
                    // kind 4: a player of the manager's club injured in this minute
                    for (size_t i = 0; i < t.scorers.size() && i < p.scorers.size(); i++)
                        if (t.scorers[i].injured && !p.scorers[i].injured) {
                            ClassicBoardMessage m{4};
                            m.player = t.scorers[i].name;
                            emit(m);
                        }
                    const int minutes = t.penalty_minutes - p.penalty_minutes;
                    if (minutes == 2 || minutes == 5) {
                        // the penalised player and "wegen " + one of the 13 reasons, picked with rand(0, 12) (5 minutes: always 5)
                        static const char* const reasons[13] = {
                            "Stockschlags.", "Ellbogenchecks.", "hohem Stocks.", "Haltens.", "Hakens.", "Übertr. Härte.",
                            "Cross-Checks.", "Faustschlags.", "Bandenchecks.", "Reklamierens.", "Beinstellen.",
                            "Torverschiebens.", "Stockstichs."};
                        ClassicBoardMessage m{minutes == 2 ? 2 : 3};
                        m.player = t.penalized;
                        const int reason = minutes == 5 ? 5 : t.penalty_reason;
                        if (reason >= 0 && reason < 13) m.second = std::string("wegen ") + reasons[reason];
                        emit(m);
                    }
                    int chances = 0;
                    for (int k = 0; k < 3; k++) chances += t.chances[k] - p.chances[k];
                    const int goals = (now.goals[side] - t.shootout_goals) - (before.goals[side] - p.shootout_goals);
                    for (int g = 0; g < goals; g++) {
                        ClassicBoardMessage m{1};
                        if (next_credit < chance_credits.size()) {
                            const ClassicBoardTeam::Credit& c = *chance_credits[next_credit++];
                            m.player = c.scorer;
                            m.second = c.assist;
                        } else {
                            for (size_t i = 0; i < t.scorers.size() && i < p.scorers.size(); i++) {
                                if (t.scorers[i].goals > p.scorers[i].goals && m.player.empty()) m.player = t.scorers[i].name;
                                if (t.scorers[i].assists > p.scorers[i].assists && m.second.empty()) m.second = t.scorers[i].name;
                            }
                        }
                        emit(m);
                    }
                    for (int miss = goals; miss < chances; miss++) {
                        ClassicBoardMessage m{0};
                        if (next_credit < chance_credits.size()) {  // a missed chance of the manager's club names the shooter
                            const ClassicBoardTeam::Credit& c = *chance_credits[next_credit++];
                            m.player = c.scorer;
                            m.second = c.assist;
                        }
                        emit(m);
                    }
                }
                // every shot of a shoot-out plays its scene (penalty shot, variant e, minute 0x78), then its board
                // message (kind 5 / 6; with goal scenes the shoot-out runs without boards, so there is none)
                size_t shot_credit[2] = {before.team[0].credits.size(), before.team[1].credits.size()};
                for (const auto& [side, goal] : now.shots) {
                    if (scene_board)
                        for (size_t i = 0; i < now.scenes.size(); i++) {
                            const ClassicBoard::Scene& d = now.scenes[i];
                            if (used[i] || !d.penalty || d.side != side || d.goal != goal) continue;
                            used[i] = true;
                            if (!d.shown) play(classic_goal_scene_script(d.number, d.side, d.goal, true, false), d.side, d.goal, 0x78);
                            break;
                        }
                    if (goal) running[size_t(side)]++;
                    if (speed >= 70 || goal_scenes || rink_stays) continue;
                    const ClassicBoardTeam& t = now.team[size_t(side)];
                    ClassicBoardMessage m{goal ? 6 : 5};
                    m.team = t.name;
                    size_t& c = shot_credit[size_t(side)];
                    while (c < t.credits.size() && !t.credits[c].shot) c++;
                    if (c < t.credits.size()) { m.player = t.credits[c].scorer; m.second = t.credits[c].assist; c++; }
                    for (ClassicMatchFrame& f : classic_board_message_steps(base, m, speed, false)) out.push_back(std::move(f));
                }
                if (scene_board)  // shots without a record (a view built without the simulation)
                    for (size_t i = 0; i < now.scenes.size(); i++) {
                        const ClassicBoard::Scene& d = now.scenes[i];
                        if (used[i] || !d.penalty || d.shown) continue;
                        used[i] = true;
                        play(classic_goal_scene_script(d.number, d.side, d.goal, true, false), d.side, d.goal, 0x78);
                        if (d.goal) running[size_t(d.side)]++;
                    }
            }
        }
        last = frame;
        if (rink_stays && rest && human_board(frame)) {
            // the minute frame shows the rink as the last scene left it, with the new minute
            ClassicGoalScene view = *rest;
            view.minute = frame.minute;
            view.final = false;
            frame.boards.clear();
            frame.reported.clear();
            frame.scene = view;
        }
        out.push_back(std::move(frame));
    }
    return out;
}

ClassicMatchFrame classic_match_frame(const Game& game, int minute, const std::vector<LiveMatch>& matches, long attendance,
                                      bool sold_out) {
    ClassicMatchFrame frame;
    frame.minute = std::max(1, minute);
    const HokFile& h = game.save();
    for (const LiveMatch& m : matches) {
        const auto mode = [&](int team) { return team < 150 ? h.state.at(0x17E7 + size_t(team) * 61 + 0x34) & 0xC0 : 0; };
        const MatchProgress p = m.run->progress();
        if (frame.boards.size() >= 4 || !((mode(m.home) | mode(m.away)) & 0x80)) {
            if (!(mode(m.home) | mode(m.away))) continue;
            ClassicReportedMatch r;  // the goals of this match are flashed in the message panel
            r.home = m.home; r.away = m.away;
            r.names[0] = m.run->home_state().name;
            r.names[1] = m.run->away_state().name;
            r.goals[0] = p.score.home - p.score.shootout_goals[0];
            r.goals[1] = p.score.away - p.score.shootout_goals[1];
            frame.reported.push_back(std::move(r));
            continue;
        }
        ClassicBoard b;
        b.goals[0] = p.score.home;
        b.goals[1] = p.score.away;
        for (int k = 0; k < 3; k++) for (int s = 0; s < 2; s++) b.period_goals[k][s] = p.period_goals[k][s];
        for (const GoalScene& g : p.scenes) b.scenes.push_back({g.side, g.goal, g.number, g.variant, g.penalty, g.shown});
        b.shots = p.shots;
        if (p.reporter == 0) frame.blink = true;  // the minute loop's rand(0, 4) = 0 closes the reporter's eyes
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
            t.shootout_goals = p.score.shootout_goals[side];
            t.colour = team < 150 ? h.state.at(0x17E7 + size_t(team) * 61 + 0x3B) | h.state.at(0x17E7 + size_t(team) * 61 + 0x3C) << 8 : 0;
            t.human = team == game.human_team();
            t.id = team;
            if (team == game.human_team()) {
                // a squad player with a role in line 4 of the kind (squad +0x15 = 4 + 0x80 * kind) lights the fourth lamp
                for (const HokSquadPlayer& sp : h.squad)
                    if (sp.role && (sp.line & 0x7F) == 4) t.fourth_line[(sp.line & 0x80) ? 1 : 0] = true;
                // the game credits the goal while the minute runs (Game::credit_human_goal): the counters name the
                // scorer and the assist of the manager's goals (the players of a missed chance are not observable)
                for (size_t slot = 0; slot < h.squad.size(); slot++) {
                    const HokSquadPlayer& sp = h.squad[slot];
                    if (sp.player < 0 || size_t(sp.player) >= h.players.size()) continue;
                    const HokPlayer& player = h.players[size_t(sp.player)];
                    ClassicBoardTeam::Scorer s;
                    s.name = player.name;
                    s.goals = sp.league_goals + sp.cup_goals + sp.other_goals;
                    s.assists = player.raw ? player.raw[0x1F] | player.raw[0x20] << 8 : 0;
                    s.injured = (h.state.at(0x696D + slot * 52 + 9) & 3) == 2;  // squad +0x09 status 2 (injured)
                    t.scorers.push_back(std::move(s));
                }
                // the game's scorer/assist picks name the players of every chance and shot (also a missed one) and the penalty
                const auto name_of = [&](int slot) -> std::string {
                    if (slot < 0 || size_t(slot) >= h.squad.size()) return {};
                    const int player = h.squad[size_t(slot)].player;
                    return player >= 0 && size_t(player) < h.players.size() ? h.players[size_t(player)].name : std::string();
                };
                for (const Game::ChanceCredit& c : game.chance_credits())
                    if (c.team == team) t.credits.push_back({c.goal, c.shot, name_of(c.scorer), name_of(c.assist)});
                t.penalized = name_of(p.penalty_slot[side]);
            }
            t.penalty_reason = p.penalty_reason[side];
        }
        if (m.home == game.human_team()) {
            b.attendance = attendance;
            b.sold_out = sold_out;
            // fill level (attendance << 2) / capacity, 2 without places
            const long capacity = long(game.budget().places[0]) + game.budget().places[1];
            b.arena = capacity ? int((attendance << 2) / capacity) : 2;
        }
        frame.boards.push_back(std::move(b));
    }
    return frame;
}

std::optional<ClassicBoardOrder> classic_board_click(const ClassicMatchFrame& frame, int x, int y) {
    if (frame.boards.empty() || frame.scene || frame.waits()) return std::nullopt;  // clicks count only while boards are shown
    ClassicBoardOrder order;
    bool hit = false;
    if (x > 2 && x < 0x15 && y > 0xE1 && y < 0xEF) { order.quick = true; hit = true; }
    // a bar is 0x19 wide and 7 high (value proportional to x), a lamp 6 x 6
    const auto bar = [&](int left, int top, int range) {
        return x >= left && x <= left + 0x19 && y >= top && y <= top + 6 ? (x - left) * range / 0x19 : -1;
    };
    for (size_t b = 0; b < frame.boards.size() && b < 5; b++) {
        const int top = int(b) * 0x2D;
        if (!(top < y && y < top + 0x2B)) continue;
        const int side = x > 0xA0 ? 1 : 0;
        const ClassicBoardTeam& t = frame.boards[b].team[size_t(side)];
        if (!t.human) continue;
        const int left = side ? 0xF0 : 0x0D, middle = side ? 0xB6 : 0x60;
        order.team = t.id;
        if (const int v = bar(left + 0x27, top + 0x16, 100); v >= 0) { order.effort = v; hit = true; }
        if (const int v = bar(left + 0x27, top + 0x1D, 7); v >= 0) { order.offence = v; hit = true; }
        for (int k = 0; k < 2; k++)
            for (int c = 0; c < 4; c++) {
                const int lx = left + k * 8 + 8, ly = top + c * 7 + 0x0E;
                if (x >= lx && x <= lx + 5 && y >= ly && y <= ly + 5 && (c < 3 || t.fourth_line[k])) { order.line[k] = c; hit = true; }
            }
        if (x >= middle && x <= middle + 0x27) { order.squad = true; hit = true; }
    }
    if (!hit) return std::nullopt;
    return order;
}

bool classic_match_display_wanted(const Game& game, bool human_plays, uint8_t event) {
    if (human_plays) return true;
    const GameSettings s = game.settings();
    if (event == kEventSectionEnd) return false;
    if (event & kEventEuroCup) return s.flags[11] != 0;           // Europapokal
    if (event & kEventWorldChampionship) return s.flags[12] != 0;  // WM-Spiele
    if (!(event & kEventLeagueMask)) return s.flags[10] != 0;     // DEB-Pokal, play-offs
    return s.flags[1] != 0 || s.flags[4] != 0 || s.flags[7] != 0;
}

}  // namespace em::ui
