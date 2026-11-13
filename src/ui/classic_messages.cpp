// SPDX-License-Identifier: GPL-3.0-only
// The message field of the classic main menu (see classic_messages.hpp).
#include "ui/classic_messages.hpp"

#include <string>

#include "game/messages.hpp"

namespace em::ui {
namespace {
constexpr auto kFont = ClassicFontId::Mini;  // the game's font 3
constexpr uint8_t kKey = 0xC0;               // transparent colour of KB/6
// Where the game's address arithmetic puts the text of "entry -1": one entry size (0x71) before the first entry's
// text, which lands at save-state offset 0x42, inside the share block that starts at 0x30.
constexpr size_t kEntryMinusOneText = 0x42;

// The text bytes of entry `index` as the game addresses them (entry base + index * entry size + 3), up to the first
// NUL or kMessageText bytes; index -1 reads kEntryMinusOneText.
std::string raw_text(const Bytes& s, int index) {
    const size_t at = index < 0 ? kEntryMinusOneText : kMessageBase + size_t(index) * kMessageSize + 3;
    std::string t;
    for (size_t n = 0; n < kMessageText && at + n < s.size() && s[at + n]; n++) t += char(s[at + n]);
    return t;
}
}  // namespace

void ClassicMessageField::open(const Bytes& state, int manager) {
    manager_ = manager;
    index_ = native_next_message(state, -1, true, manager);  // the first (newest) entry of the manager
    active_ = true;
    pressed_ = -1;
}

void ClassicMessageField::show_immediate(std::vector<std::string> texts) {
    if (texts.empty()) return;
    immediate_ = std::move(texts);
    active_ = true;
    pressed_ = -1;
}

void ClassicMessageField::close() {
    pressed_ = -1;
    if (!immediate_.empty()) immediate_.erase(immediate_.begin());
    active_ = !immediate_.empty();
}

void ClassicMessageField::draw(ClassicCanvas& c, const Bytes& state) const {
    if (!active_) return;
    c.blit(6, 3, 0xC9, 0, 0, 0x13A, 0x26, true, kKey);
    const bool further = immediate_.empty() && native_next_message(state, index_, true, manager_) >= 0;
    int y = 0xD4;
    bool first = true;
    std::string line;
    for (const char ch : immediate_.empty() ? raw_text(state, index_) : immediate_.front()) {
        if (ch != '^') { line += ch; continue; }
        c.text_native(kFont, 0x10, y, line, uint8_t(first && further ? 0x11 : 0), -1, 0x12F);
        y += 7;
        if (first) { y += 5; first = false; }
        line.clear();
    }
    if (y == 0xD4) c.text_native(kFont, 0x10, 0xD4, "Keine Nachricht f}r Sie vorhanden.", 0, -1, 0x12F);
    if (pressed_ >= 0) c.blit(6, 0x12F, 0xD1 + 10 * pressed_, 0x18 + 12 * pressed_, 0x27, 12, 9, true, kKey);
}

bool ClassicMessageField::press(const Bytes& state, int x, int y) {
    if (!active_) return false;
    if (y < 0xC9) { close(); return false; }
    if (x > 0x12C && y > 0xD0 && y < 0xE6 && immediate_.empty()) {
        pressed_ = y > 0xDB ? 1 : 0;
        const int next = native_next_message(state, index_, pressed_ == 0, manager_);
        if (next >= 0) index_ = next;
    }
    return true;
}

}  // namespace em::ui
