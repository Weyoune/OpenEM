// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "formats/hok.hpp"
#include "game/new_game.hpp"
#include "ui/classic_canvas.hpp"

namespace em::ui {
// Manager -> Neuer: the native start menu (ovr027:0135) entered from the running game (argument 0: the header does
// not open Laden and the start settings on the right are only shown). KB150 is the background; four manager rows
// (y = 41 + 51 * row) hold a face (x 6), name and club (centred in 47..133, baselines 48/56 + 51 * row) and a crest
// (x 138). A left click in the face column (x < 45) or crest column (136 < x < 178) marks the cell with two yellow
// lines (colour 0x18; the previous mark is overdrawn in colour 8, which stays visible) and opens the face board
// (KB7 at 184,32 with KB0 at 193,39) or the club board (KB7, a colour 11 page of twelve crests KB80+ at 190,41 and the
// scroll bar at 310,39). Hovering a board cell frames it in colour 0x12; the club board also names the club. A
// chosen face starts the name field (19 letters, at least two); a chosen club closes the board. A right click in a
// row removes that new manager. "Los jetzt!" (253 < x < 309, 193 < y < 233) leaves. In a running game OpenEM does not add
// managers yet (a completed new row is reported to the caller); a new game takes all complete rows (classic_misc_input.cpp).
class ClassicNewManager {
public:
    enum class Result { None, Leave, LeaveWithManager };
    enum class Panel { Options, Faces, Clubs };
    struct Row {
        int face = -1;     // 0..11 (DS:0178 + row)
        int team = -1;     // team index (manager block +0x14)
        std::string name;  // native bytes
        bool existing = false;
    };

    void open(const HokFile& h);
    // Start of a new game (argument 1): no running managers; "Los jetzt!" needs one complete new row.
    void open_start(const HokFile& base);
    bool start_mode() const { return start_mode_; }
    // The two mode icons (menu variable var_57, game/wm_mode.hpp): bit 0 Liga/Cup, bit 1 WM; 2 = "Nur WM".
    int mode_bits() const { return options_.mode; }
    void draw(ClassicCanvas& canvas, const HokFile& h, int mouse_x, int mouse_y) const;
    Result click(const HokFile& h, int x, int y, bool right);
    void key(int key);
    void text(std::string_view text);

    const std::array<Row, 4>& rows() const { return rows_; }
    Panel panel() const { return panel_; }
    int active_row() const { return row_; }
    bool editing_name() const { return editing_; }
    int club_page() const { return page_; }
    const NewGameOptions& options() const { return options_; }

private:
    void close_panel();
    void finish_name();

    std::array<Row, 4> rows_{};
    Panel panel_ = Panel::Options;
    int row_ = -1;          // row of the open board (DS: di)
    bool editing_ = false;  // name field of row_ is active
    std::string draft_;     // the name being typed
    int page_ = 0;          // club page 0..3 (twelve German clubs each)
    std::pair<int, int> mark_{-1, -1};      // yellow mark: column (1 faces, 2 crests), row
    std::set<std::pair<int, int>> erased_;  // marks overdrawn in colour 8
    mutable std::pair<int, int> hover_{-1, -1};  // last framed board cell (stays when the pointer leaves the board)
    int pressed_arrow_ = 0;                  // scroll arrow held down: -1 up, +1 down
    NewGameOptions options_{};               // state 0x28 (DS:0118): level, duration, squad size, strength; var_57
    bool highscore_ = false;                 // DS:0256 != 0: notepad instead of the bare nail
    bool start_mode_ = false;                // ovr027:0135 argument 1 (new game)
};
}  // namespace em::ui
