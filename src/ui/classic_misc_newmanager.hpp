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

// New-manager / start menu of the classic frontend ("Manager -> Neuer" in a running game, and the start menu of a
// new game). The class holds the menu state and the face/club boards; its methods are implemented in
// classic_misc*.cpp.

namespace em::ui {
// Manager -> Neuer: the original start menu, entered from the running game (in this mode the header does not
// open Laden and the start settings on the right are only shown). KB150 is the background; four manager rows
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
    // Outcome of a click: stay in the menu, leave it, or leave with a completed new manager row.
    enum class Result { None, Leave, LeaveWithManager };
    // What the right half of the page shows: the start settings, the face board or the club board.
    enum class Panel { Options, Faces, Clubs };
    // One of the four manager rows.
    struct Row {
        int face = -1;     // face 0..11 (save state 0x01EE + row), -1 = none chosen yet
        int team = -1;     // team index (manager block +0x14), -1 = none chosen yet
        std::string name;  // name in the game's own byte encoding
        bool existing = false;  // row belongs to a manager already in the running game (cannot be removed)
    };

    // Opens the menu from a running game: the existing managers fill their rows, the settings are read-only.
    void open(const HokFile& h);
    // Start of a new game: no running managers; "Los jetzt!" needs one complete new row.
    void open_start(const HokFile& base);
    bool start_mode() const { return start_mode_; }
    // The two mode icons (game/wm_mode.hpp): bit 0 Liga/Cup, bit 1 WM; 2 = "Nur WM".
    int mode_bits() const { return options_.mode; }
    // Paints the whole page; the mouse position selects the hover frame on an open board.
    void draw(ClassicCanvas& canvas, const HokFile& h, int mouse_x, int mouse_y) const;
    // Handles a left or right click at (x, y) in 320x240 page coordinates.
    Result click(const HokFile& h, int x, int y, bool right);
    // Key press while the name field is active: Return/Enter accepts, Backspace deletes, Escape clears the draft.
    void key(int key);
    // Typed text for the name field (letters 'A'..'z' and the space, at most 19 characters).
    void text(std::string_view text);

    const std::array<Row, 4>& rows() const { return rows_; }
    Panel panel() const { return panel_; }
    int active_row() const { return row_; }
    bool editing_name() const { return editing_; }
    int club_page() const { return page_; }
    const NewGameOptions& options() const { return options_; }

private:
    // Closes the face/club board and returns to the settings panel.
    void close_panel();
    // Accepts the typed name (at least two letters) into the active row.
    void finish_name();

    std::array<Row, 4> rows_{};
    Panel panel_ = Panel::Options;
    int row_ = -1;          // row the open board belongs to
    bool editing_ = false;  // name field of row_ is active
    std::string draft_;     // the name being typed
    int page_ = 0;          // club page 0..3 (twelve German clubs each)
    std::pair<int, int> mark_{-1, -1};      // yellow mark: column (1 faces, 2 crests), row
    std::set<std::pair<int, int>> erased_;  // marks overdrawn in colour 8
    mutable std::pair<int, int> hover_{-1, -1};  // last framed board cell (stays when the pointer leaves the board)
    int pressed_arrow_ = 0;                  // scroll arrow held down: -1 up, +1 down
    NewGameOptions options_{};               // save state 0x28: level, duration, squad size, strength; game mode
    bool highscore_ = false;                 // a high-score file exists for these settings: notepad instead of the bare nail
    bool start_mode_ = false;                // opened for a new game (open_start) rather than from a running game
};
}  // namespace em::ui
