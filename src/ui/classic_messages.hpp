// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The message field of the main menu (ovr036:0504, sub_58264), game/messages.hpp holds the entries.
//
// Opened by a click on the manager's face (ovr034:234D: 0xE8 < x < 0x110, 0x21 < y < 0x48) or, when the
// manager's "new message" flag DS:5A50 is set, as soon as the main menu appears (ovr034:2120, the flag is
// cleared). It starts at the manager's first (newest) entry. KB6 is drawn at (3, 201), 314 x 38 pixels,
// transparent colour 0xC0. Lines (font 3, centred between x 16 and 303, no shadow) start at baseline 212 and
// move down 7 pixels per '^'; the first line (the date) is followed by 5 more pixels and is drawn in colour
// 0x11 while a further entry exists, else in 0. Without any entry the routine reads the text of "entry -1"
// (DS:D226, the end of the share block) and then writes "Keine Nachricht f}r Sie vorhanden." when no line
// was drawn. The arrows at x > 300: the upper one (208 < y <= 219) moves to the next (older) entry, the lower
// one (219 < y < 230) to the previous; the pressed arrow (KB6 at 0x18 + 12k, 0x27, 12 x 9) is drawn at
// (303, 209 + 10k) until the button is released. A click above the field (y < 201) or a key closes it and
// restores the menu.
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {

class ClassicMessageField {
public:
    void open(const Bytes& state, int manager);
    // Entries the original shows at once and deletes again (season review): native texts, shown one after the other.
    void show_immediate(std::vector<std::string> texts);
    void close();  // with immediate texts queued: the next one
    bool active() const { return active_; }
    int index() const { return index_; }
    int pressed_arrow() const { return pressed_; }
    void draw(ClassicCanvas& canvas, const Bytes& state) const;
    // Left button pressed at (x, y); false when the field closed.
    bool press(const Bytes& state, int x, int y);
    void release() { pressed_ = -1; }

private:
    bool active_ = false;
    std::vector<std::string> immediate_;
    int manager_ = 0, index_ = -1, pressed_ = -1;
};

}  // namespace em::ui
