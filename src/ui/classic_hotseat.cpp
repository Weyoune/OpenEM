// SPDX-License-Identifier: GPL-3.0-only
// Hot seat: up to four human managers share the menu (docs/MEHRSPIELER_PLAN.md step 10, docs/CLASSIC_UI.md).
//
// The original's day loop (sub_6153F, Amiga FUN_0012504c lines 17630..17660) runs, for every manager 0..N-1 in index
// order on a menu day, the transfer-list roll and the main menu sub_24F2A (Amiga FUN_0010615a) with that manager as
// DS:A70E. The menu draws the header of that manager (name, face DS:0178 + m, club, finances) and opens his message
// field when his flag DS:5A50 + m is set; the crest ("weiter") ends his turn. There is no screen between two managers
// (DOSBox: anna -> bert on 10.09.1993 directly). After the last manager the day is played and the next menu day
// starts again with manager 0. The draws of the menu pass are made by Game::menu_pass at the end of the day step.
#include "game/manager_layout.hpp"
#include "ui/classic_app.hpp"

namespace em::ui {

int ClassicApplication::active_manager() const { return session_.loaded() ? session_.active_manager() : 0; }

std::string ClassicApplication::manager_name(int manager) {
    const Bytes& s = snapshot().state;
    return iso646de_to_utf8(reinterpret_cast<const char*>(s.data() + manager_block(manager)), 0x13);
}

// The crest of manager m < N - 1 hands the menu to manager m + 1 on the same day; the last one plays the day.
bool ClassicApplication::hand_over_menu() {
    const int next = session_.active_manager() + 1;
    if (next < session_.game().managers()) {
        session_.select_manager(next);
        category_ = -1;
        selected_player_ = selected_pool_ = -1;
        lineup_cells_.reset();
        invalidate();
        return true;
    }
    session_.select_manager(0);  // the day loop and the first menu of the next day belong to manager 0
    invalidate();
    return false;
}

}  // namespace em::ui
