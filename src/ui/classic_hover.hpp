// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ui/classic_roster.hpp"
#include <array>
#include <map>

namespace em::ui {

using ClassicRosterKind = ClassicRosterView;

struct ClassicRosterHover {
    int row = -1;
    int field = -1;
    std::string native_help;
};

// Original DS039F widths, DS0984 help table and dynamic fragments, loaded
// once from EM.EXE. Original DOS bytes go straight through text_native.
class ClassicRosterHelp {
public:
    explicit ClassicRosterHelp(const Bytes& exe);

private:
    Bytes exe_;
    std::array<int, 18> widths_{};
    std::map<int, std::string> strings_;
    std::string literal(int offset) const;
    std::string pointer(int offset) const;
    friend ClassicRosterHover classic_roster_hover(const HokFile&, int,
        ClassicRosterKind, int, int, const ClassicRosterHelp&);
};

// Native 59B5:16A3 candidate lookup and footer help, without click handling
// or cached previous-row state. Uses occupied count, preserving slot gaps.
ClassicRosterHover classic_roster_hover(const HokFile& h, int manager,
    ClassicRosterKind kind, int x, int y, const ClassicRosterHelp& help);

// Clears the native footer help rectangle; paints help and optional current
// rules. Caller erases previous rules with palette4 and retains help on click.
void draw_classic_roster_hover(ClassicCanvas& canvas, ClassicRosterKind kind,
    const ClassicRosterHover& hover, bool drawRules = true);

} // namespace em::ui
