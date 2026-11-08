// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <string>
#include <string_view>

#include "formats/hok.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {
// Manager -> Neuer: the native start menu (ovr027:0135) in its "new manager" mode. The game settings on the
// right are only shown; the four manager rows take a face (KB0), a name and a club (crest KB80+, twelve per
// page, four leagues). OpenEM plays one human manager, so a completed new row is not added to the game.
class ClassicNewManager {
public:
    enum class Result { None, Leave, LeaveWithManager };
    struct Row {
        int face = -1;  // 0..11, DS:0178 + row
        int team = -1;  // manager block +0x14
        std::string name;  // native bytes
        bool existing = false;
    };
    enum class Panel { Options, Faces, Clubs };

    void open(const HokFile& h);
    void draw(ClassicCanvas& canvas, const HokFile& h, int mouse_x, int mouse_y) const;
    Result click(const HokFile& h, int x, int y, bool right);
    void key(int key);
    void text(std::string_view text);

    const std::array<Row, 4>& rows() const { return rows_; }
    Panel panel() const { return panel_; }
    int active_row() const { return row_; }
    bool editing_name() const { return editing_; }
    int club_page() const { return club_page_; }

private:
    std::array<Row, 4> rows_{};
    Panel panel_ = Panel::Options;
    int row_ = -1;
    bool editing_ = false;
    int club_page_ = 0;
    std::string typed_;  // native keyboard buffer the name field starts from
};
}  // namespace em::ui
