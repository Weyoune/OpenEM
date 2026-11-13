// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Team lineup of a manager: the automatic lineup and the manual lineup screen's click actions.
// Both work directly on the squad records in the HOK state (role byte +0x0A: 1 = in the
// squad for the match; line byte +0x15: 0 = reserve/none, 1..4 = block, bit 0x80 = forward
// half of the block) and keep the decoded squad of a human manager in sync.
#include "formats/hok.hpp"

namespace em {

// Automatic lineup, if the manager's automatic-lineup mode (state 0x078A + manager * 2) is not
// zero; mode zero leaves a manual lineup untouched and returns false. Otherwise all roles are
// cleared and the best available players are picked: one goalkeeper, three pairs of defenders
// and three trios of forwards, then reserves up to 22 players, excluding injured/suspended
// players. When a position runs out of natural players the picker cycles through the other
// kinds (direction depending on the mode). Returns true when the lineup was rebuilt.
// The decoded squad of a human manager (0..3) is kept in sync with its serialized records.
bool auto_lineup(HokFile& h, int manager);

// Click actions of the manual lineup screen.
enum class ManualLineupAction {
    Select,         // left click on a roster row
    ReserveToggle,  // right click on a roster row (outside the details band)
    RemoveNumber    // click on a number plate in the line diagram
};

// Refusals; each one maps to one of the original game's two-line notices.
enum class ManualLineupMessage {
    None, Injured, Suspended, TeamFull, NoLineSelected, WrongRole,
    SixFieldPlayers, NoSpace
};

// Outcome of manual_lineup().
struct ManualLineupResult {
    ManualLineupMessage message = ManualLineupMessage::None;
    bool changed = false;             // role (+0x0A) or line (+0x15) changed; excludes automatic_disabled
    bool automatic_disabled = false;  // automatic mode was switched off (also when the move is refused)
    int selected_line = 0;            // 0 none, 1..4 blocks, 5 goalkeeper; a removal can select its old block
    int assigned_cell = -1;           // diagram cell 0..5 of the block, only for a new assignment
};

// One click of the manual lineup screen, without random numbers, appearance booking or
// refresh of the derived team strengths. `slot` is the roster row, selectedLine: 0 none,
// 1..4 blocks, 5 goalkeeper. Any action first switches the automatic lineup off.
// `sixOverride` / `cellsOverride` replace the diagram state derived from the records (used
// when the screen's diagram differs from it).
// inMatch: the lineup opened from the board of a running match: a player taken out of a line
// keeps his role byte (+0x0A = 1), the reserve toggle does nothing and only a squad member
// without a line (role != 0, block 0) can be placed into a line.
// Refusals of the original game return a message; malformed arguments/state throw FormatError.
// The decoded squad of a human manager is synchronized after a change.
ManualLineupResult manual_lineup(HokFile& h, int manager, size_t slot,
                                int selectedLine, ManualLineupAction action,const bool* sixOverride=nullptr,
                                const std::array<std::array<int,6>,5>* cellsOverride=nullptr,bool inMatch=false);
// Shorthand: `reserveToggle` selects ReserveToggle, else Select.
ManualLineupResult manual_lineup(HokFile& h, int manager, size_t slot,
                                int selectedLine, bool reserveToggle);

}  // namespace em
