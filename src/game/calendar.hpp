// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Season calendar and date arithmetic of EM.EXE.
//
// Day counter: DS:03B7 (u32, saved at HOK state 0x8E2D), 1 = first day of the season. The date
// routine ovl06_1271 turns it into a date: day of year = counter + 252, no leap years, months
// from the length table at DS:0393 - so day 1 is 10 September (a Sunday in game terms; the
// weekday is (counter - 1) % 7 with 0 = Sunday, the real 1993 calendar is not used). The start
// year is DS:03BB (1993).
//
// Event table: DS:0828, one byte per season day 0..232 (index = day counter, read by the day loop
// ovl14_2e0f through the counter at DS:A969). Codes seen in V1.00:
//   bits 0-2  league matchday of the first division / second division / Oberliga (always 7 =
//             all three; 44 days, Wednesdays and Sundays from 10.09. to 04.02.)
//   0x08      DEB cup (15 days, 3 per round; the Achtelfinale starts on 20.10.)
//   0x20      European cup (12 days from 04.11.: 6 group rounds, then the final round)
//   0x40      play-offs and relegation (15 days, 06.02. - 05.04. = three best-of-five rounds;
//             ovl05_2404 with DS:76B2 = 1, halved goal points)
//   0x10      world championship (8 days, 14.04. - 28.04.: 5 group rounds and the final round;
//             ovl07_3036 first calls up the national players)
//   0xFF      end of a season section (13.04. after the play-offs, 29.04. season end)
// Checked: matchday 10 falls on day 32 (Wednesday 11.10.), as in the reference save MONAT1;
// the European cup group days are 04./07./09./11./14./16.11. as on the "Europacup" screen.
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"

namespace em {

enum CalendarEvent : uint8_t {
    kEventLeagueMask = 0x07,
    kEventCup = 0x08,
    kEventWorldChampionship = 0x10,
    kEventEuroCup = 0x20,
    kEventPlayoff = 0x40,
    kEventSectionEnd = 0xFF,
};

struct GameDate {
    int day = 0, month = 0, year = 0;  // month 1..12
    int weekday = 0;                   // 0 = Sunday
};

// Date of a season day (1 = first day) in the season starting in `start_year`.
GameDate date_of_season_day(int season_day, int start_year);

struct SeasonCalendar {
    std::vector<uint8_t> events;  // index = season day, 0 = nothing happens

    uint8_t event(int day) const { return day >= 0 && size_t(day) < events.size() ? events[size_t(day)] : 0; }
    bool league_day(int day) const { return event(day) != kEventSectionEnd && (event(day) & kEventLeagueMask); }
    // Season day of the n-th (1-based) day with the given event code, or -1.
    int nth_day(uint8_t code, int n) const;
    // Season day of league matchday n (1-based), or -1.
    int league_matchday_day(int n) const;
};

// Reads the event table from EM.EXE (up to and including the second 0xFF section end).
SeasonCalendar load_season_calendar(const Bytes& exe);

}  // namespace em
