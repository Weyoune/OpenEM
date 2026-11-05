// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include <cstdint>

namespace em::ui {

struct ClassicOpponent {
    uint16_t calendarIndex = 0; // native A969, serialized B953
    uint8_t calendarEvent = 0;  // header reads full cursor, even if selection suppressed
    uint8_t selectedEvent = 0;  // menu's byte index; native argumentFF forces event0
    uint8_t team = 0xFF;
    uint8_t side = 0xFF;       // 1 home,0 away; unknown/irrelevant without opponent
    bool seasonEnd = false;   // current calendar byteFF, not the menu argumentFF
    bool has_match() const { return team != 0xFF; }
};

// Read-only5CB4:1CD2/18F8 opponent selection for this event, with lookup-mode
// 4996:27A5 groups. Reads raw serialized fields, including pending/100 guards.
// Does not search future dates, generate draws, rebuild schedules, or use RNG.
// suppressMatchSelection corresponds to passing FF to the native menu.
ClassicOpponent classic_opponent(const HokFile&, const SeasonCalendar&,
                                 int activeManager = 0, bool suppressMatchSelection = false);

} // namespace em::ui
