// SPDX-License-Identifier: GPL-3.0-only
#include "game/messages.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "game/scorers.hpp"

namespace em {
namespace {
constexpr size_t kPlayerBase = 0x4FA5, kPlayerRecord = 0x21;  // record of player ID n at base + n * 0x21
constexpr int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};  // DS:0393
// DS:0A70, native character set ('{' = ae).
constexpr const char* kMonths[12] = {"Januar", "Februar", "M{rz", "April", "Mai", "Juni",
                                     "Juli", "August", "Sept.", "Okt.", "Nov.", "Dez."};
// DS:0CA0
constexpr const char* kTemplates[7] = {
    "$ bietet Ihnen an, $^Jahre zu verl{ngern.^",
    "$ ist an $^interessiert.^",
    "$^$^",
    "$ gibt das Eishockeyspielen im Alter von^# Jahren endg}ltig auf.^",
    "$ k}ndigt an, da~ er seinen Vertrag^nicht mehr verl{ngern wird.^",
    "$^$ $ ^",
    "$ ist Sportinvalide.^Er verl{~t Ihre Mannschaft endg}ltig.^",
};

uint8_t* entry(Bytes& s, int index) { return s.data() + kMessageBase + size_t(index) * kMessageSize; }
const uint8_t* entry(const Bytes& s, int index) { return s.data() + kMessageBase + size_t(index) * kMessageSize; }
void check(const Bytes& s) {
    if (s.size() < kMessageBase + kMessageSlots * kMessageSize || s.size() <= kMessageNewFlag + 4)
        throw std::out_of_range("messages: state too small");
}
// sub_19B54 with DS:038D != 0 (set by the message routine: no thousands separators)
std::string number(long value) { return std::to_string(value); }
uint8_t* player_handle(Bytes& s, int id) {
    if (id <= 0 || id >= 200) return nullptr;
    return s.data() + kPlayerBase + size_t(id) * kPlayerRecord + 0x1E;
}
}  // namespace

std::string native_message_text(BorlandRandom& rng, const MessageDate& date, MessageKind kind, const std::vector<MessageArg>& args) {
    const int back = rng.range(0, 3);
    return native_message_text(back, date, kind, args);
}

std::string native_message_text(int days_back, const MessageDate& date, MessageKind kind, const std::vector<MessageArg>& args) {
    // ovr036:0242..0347: the date line, dated back 0..3 days
    int day = date.day - days_back;
    int month = std::clamp(date.month - 1, 0, 11);
    if (kMonthDays[month] < day) day = kMonthDays[month];
    int year = date.year;
    if (day < 1) {
        if (--month < 0) { month = 11; --year; }
        day += kMonthDays[month];
    }
    std::string text = number(day) + ". " + kMonths[month] + " " + number(year) + "^";
    // ovr036:0351..0447: template with substitutions
    const int k = int(kind);
    if (k < 0 || k > 6) throw std::invalid_argument("messages: unknown template");
    size_t arg = 0;
    for (const char* c = kTemplates[k]; *c; ++c) {
        if (*c == '$' || *c == '#') {
            const MessageArg a = arg < args.size() ? args[arg] : MessageArg{};
            ++arg;
            text += *c == '#' || a.is_number ? number(a.number) : a.text;
        } else {
            text += *c;
        }
    }
    return text;
}

int native_add_message(Bytes& state, BorlandRandom& rng, const MessageDate& date, MessageKind kind,
                       const std::vector<MessageArg>& args, int manager, bool general, int player_id, bool show_now) {
    check(state);
    const std::string text = native_message_text(rng, date, kind, args);
    int slot = -1;  // ovr036:0011 first free entry
    for (int i = 0; i < kMessageSlots; i++)
        if (entry(state, i)[0] == 0) { slot = i; break; }
    if (slot < 0) return 0;
    uint8_t* e = entry(state, slot);
    e[0] = uint8_t((manager & 0xFF) | (general ? 8 : 0) | 0x30);
    const int stamp = std::min(date.day_index, 255);
    e[1] = uint8_t(stamp);
    e[2] = uint8_t(player_id);
    std::fill_n(e + 3, kMessageText, uint8_t(0));
    std::copy_n(text.begin(), std::min(text.size(), size_t(kMessageText)), e + 3);
    if (!show_now && manager >= 0 && manager < 4) state.at(kMessageNewFlag + size_t(manager)) = 1;
    return slot + 1;
}

void native_flash_message(Bytes& state, const std::string& text) {
    check(state);
    // ovr026:07F7..09F3: the message goes into the first free entry, is shown at once and deleted again; the text stays
    for (int i = 0; i < kMessageSlots; i++) {
        uint8_t* e = entry(state, i);
        if (e[0] != 0) continue;
        std::fill_n(e + 3, kMessageText, uint8_t(0));
        std::copy_n(text.begin(), std::min(text.size(), size_t(kMessageText)), e + 3);
        e[1] = e[2] = 0;
        return;
    }
}

void native_delete_message(Bytes& state, uint8_t& handle) {
    check(state);
    if (handle > 0 && handle < 25) {
        uint8_t* e = entry(state, handle - 1);
        e[0] = e[1] = e[2] = 0;  // first byte and the following word only
    }
    handle = 0;
}

void native_sort_messages(Bytes& state) {
    check(state);
    if (std::getenv("EM_DEBUG_SORT")) std::fprintf(stderr, "message sort [%s]\n", g_draw_scope ? g_draw_scope : "");
    std::array<uint8_t, kMessageSize> swap{};
    for (int i = 0; i < kMessageSlots - 1; i++)
        for (int j = i + 1; j < kMessageSlots; j++) {
            uint8_t* a = entry(state, i);
            uint8_t* b = entry(state, j);
            if (b[1] <= a[1]) continue;  // swap when the later entry is newer
            const uint8_t flags_a = a[0], flags_b = b[0], player_a = a[2], player_b = b[2];
            std::copy_n(a, kMessageSize, swap.begin());
            std::copy_n(b, kMessageSize, a);
            std::copy_n(swap.begin(), kMessageSize, b);
            if (flags_a && !(flags_a & 8))
                if (uint8_t* h = player_handle(state, player_a)) *h = uint8_t(j + 1);
            if (flags_b && !(flags_b & 8))
                if (uint8_t* h = player_handle(state, player_b)) *h = uint8_t(i + 1);
        }
}

void native_message_step(Bytes& state) {
    check(state);
    for (int i = 0; i < kMessageSlots; i++) {
        uint8_t* e = entry(state, i);
        if (!(e[0] & 8)) continue;
        int steps = e[0] >> 4;
        if (steps == 0) continue;
        if (--steps == 0) e[0] = e[1] = e[2] = 0;
        else e[0] = uint8_t((e[0] & 0x0F) | steps << 4);
    }
}

int native_next_message(const Bytes& state, int from, bool forward, int manager) {
    check(state);
    if (forward) {
        for (int i = from + 1; i < kMessageSlots; i++)
            if (entry(state, i)[0] && (entry(state, i)[0] & 7) == manager) return i;
    } else {
        for (int i = from - 1; i >= 0; i--)
            if (entry(state, i)[0] && (entry(state, i)[0] & 7) == manager) return i;
    }
    return -1;
}

std::string native_from_utf8(const std::string& utf8) {
    std::string out;
    for (size_t i = 0; i < utf8.size(); i++) {
        const unsigned char c = uint8_t(utf8[i]);
        if (c == 0xC3 && i + 1 < utf8.size()) {
            const unsigned char d = uint8_t(utf8[++i]);
            const char* map = d == 0xA4 ? "{" : d == 0xB6 ? "|" : d == 0xBC ? "}" : d == 0x9F ? "~" : d == 0x84 ? "[" : d == 0x96 ? "\\" : d == 0x9C ? "]" : "?";
            out += map;
        } else {
            out += char(c);
        }
    }
    return out;
}

MessageView native_message(const Bytes& state, int index) {
    check(state);
    if (index < 0 || index >= kMessageSlots) throw std::out_of_range("messages: index");
    const uint8_t* e = entry(state, index);
    MessageView v;
    v.index = index;
    v.manager = e[0] & 7;
    v.general = (e[0] & 8) != 0;
    v.steps = e[0] >> 4;
    v.day_index = e[1];
    v.player_id = e[2];
    std::string line;
    for (size_t n = 0; n < kMessageText && e[3 + n]; n++) {
        if (e[3 + n] == '^') { v.lines.push_back(line); line.clear(); }
        else line += char(e[3 + n]);
    }
    // ovr036:061C: only text ended by '^' is drawn
    return v;
}

std::vector<MessageView> native_messages(const Bytes& state, int manager) {
    std::vector<MessageView> out;
    for (int i = native_next_message(state, -1, true, manager); i >= 0; i = native_next_message(state, i, true, manager))
        out.push_back(native_message(state, i));
    return out;
}

}  // namespace em

#include "game/game.hpp"

namespace em {
// ovr034:2139: the main menu clears the manager's flag before it opens the message field.
void Game::clear_message_flag(int manager) {
    if (manager >= 0 && manager < 4) save_.state.at(kMessageNewFlag + size_t(manager)) = 0;
}
}  // namespace em
