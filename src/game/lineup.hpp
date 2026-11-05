// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"

namespace em {

// ovl09_26dc / ovl09_2858: honour the manager's current automatic-lineup mode
// (DS:0375 + manager * 2; zero leaves a manual lineup untouched). Select a
// goalkeeper, three lines and reserves, excluding injured/suspended players.
// Manager 0's decoded squad is kept in sync with its serialized records.
bool auto_lineup(HokFile& h, int manager);

enum class ManualLineupAction { Select, ReserveToggle, RemoveNumber };

// Causes map to the original two-line notices (59B5:0E21..0F22/14DC).
enum class ManualLineupMessage {
    None, Injured, Suspended, TeamFull, NoLineSelected, WrongRole,
    SixFieldPlayers, NoSpace
};

struct ManualLineupResult {
    ManualLineupMessage message = ManualLineupMessage::None;
    bool changed = false;             // roster0A/15 changed; excludes automatic_disabled
    bool automatic_disabled = false; // retain this even when message rejects the move
    int selected_line = 0;            //0 none,1..4 blocks,5 TW; removal can select its old block
    int assigned_cell = -1;           // native local cell0..5, only for a new assignment
};

// Ordinary pre-match menu actions, with no RNG, appearance booking or derived
// team-strength refresh. selectedLine:0 none,1..4 blocks,5 TW. Select is the
// roster's left click; ReserveToggle its right click outside the details band.
// RemoveNumber is a diagram-plate click, with its distinct duration/status check.
// Native refusals return a message; malformed arguments/state throw FormatError.
// Manager0's existing decoded squad mirror is synchronized after a mutation.
ManualLineupResult manual_lineup(HokFile& h, int manager, size_t slot,
                                int selectedLine, ManualLineupAction action,const bool* sixOverride=nullptr,
                                const std::array<std::array<int,6>,5>* cellsOverride=nullptr);
ManualLineupResult manual_lineup(HokFile& h, int manager, size_t slot,
                                int selectedLine, bool reserveToggle);

}  // namespace em
