// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include <cstdint>

// Opponent lookup for the main-menu header: which team the active manager's club meets on the current calendar
// day (league, play-off, DEB-Pokal, European cup or World Championship), read directly from the save state.

namespace em::ui {

// The current calendar position and the opponent of the active manager's club on that day.
struct ClassicOpponent {
    uint16_t calendarIndex = 0; // calendar cursor, save state 0xB953 (u16)
    uint8_t calendarEvent = 0;  // event byte of the full cursor, read even if the match selection is suppressed
    uint8_t selectedEvent = 0;  // event byte of the cursor's low byte; 0 when the selection is suppressed
    uint8_t team = 0xFF;        // opponent team index, 0xFF = no match today
    uint8_t side = 0xFF;        // 1 home, 0 away; 0xFF (meaningless) without opponent
    bool seasonEnd = false;     // the current calendar event is the section end marker (0xFF)
    bool has_match() const { return team != 0xFF; }
};

// Finds the opponent of the active manager's club for today's calendar event, the way the original main menu
// does. League days use the serialized league pairing arrays; the special competitions (WM, play-off, cup,
// European cup) are looked up in their group schedules and knockout lists, honouring the "100" score guards
// that mark a pairing as already decided. Purely read-only: it does not search future dates, generate draws,
// rebuild schedules or use the random generator. suppressMatchSelection makes the menu show no match (the
// original passes 0xFF as the menu argument in that case). Throws FormatError on an invalid manager or team.
ClassicOpponent classic_opponent(const HokFile&, const SeasonCalendar&,
                                 int activeManager = 0, bool suppressMatchSelection = false);

} // namespace em::ui
