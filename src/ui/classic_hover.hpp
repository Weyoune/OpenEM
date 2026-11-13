// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/classic_roster.hpp"
#include <array>
#include <map>

// Footer help of the classic squad lists (hover over a player row's column).
namespace em::ui {

using ClassicRosterKind = ClassicRosterView;

// Result of a hover lookup.
struct ClassicRosterHover {
    int row = -1;             // list row under the mouse, -1 if none
    int field = -1;           // column under the mouse (0..17), -1 if none
    std::string native_help;  // help text in the game's encoding
};

// The column widths, the help-text table and the text fragments used to
// build the dynamic parts, loaded once from EM.EXE. The texts keep the
// game's own encoding and are drawn with text_native.
class ClassicRosterHelp {
public:
    // Throws FormatError if EM.EXE does not have the expected layout.
    explicit ClassicRosterHelp(const Bytes& exe);

private:
    Bytes exe_;                           // copy of EM.EXE
    std::array<int, 18> widths_{};        // pixel width per column
    std::map<int, std::string> strings_;  // texts by pointer-table offset
    std::string literal(int offset) const;
    std::string pointer(int offset) const;
    friend ClassicRosterHover classic_roster_hover(const HokFile&, int,
        ClassicRosterKind, int, int, const ClassicRosterHelp&);
};

// Finds the row and column under (x,y) in the squad list of manager and
// builds the game's help text for it. No click handling and no memory of
// the previous row. Rows are counted over occupied slots only, as in the
// game, so gaps in the squad shift the rows. Returns {} outside the list.
ClassicRosterHover classic_roster_hover(const HokFile& h, int manager,
    ClassicRosterKind kind, int x, int y, const ClassicRosterHelp& help);

// Clears the footer help rectangle and paints the help text and, if
// drawRules, the rules of the current row. The caller erases previous
// rules with colour 4 and keeps the help when a row is clicked.
void draw_classic_roster_hover(ClassicCanvas& canvas, ClassicRosterKind kind,
    const ClassicRosterHover& hover, bool drawRules = true);

} // namespace em::ui
