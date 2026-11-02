// SPDX-License-Identifier: GPL-3.0-only
#include "game/calendar.hpp"

namespace em {

namespace {
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;  // see schedule.cpp
constexpr size_t kEventTable = 0x0828;
constexpr size_t kEventTableMax = 300;
constexpr int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};  // DS:0393
constexpr int kDayOfYearOffset = 252;  // ovl06_1271: counter + 0xFC
}  // namespace

GameDate date_of_season_day(int season_day, int start_year) {
    GameDate d;
    int doy = season_day + kDayOfYearOffset;
    d.year = start_year;
    while (doy > 365) {
        doy -= 365;
        d.year++;
    }
    int m = 0;
    while (doy > kMonthDays[m]) {
        doy -= kMonthDays[m];
        m = (m + 1) % 12;
    }
    d.day = doy;
    d.month = m + 1;
    d.weekday = ((season_day - 1) % 7 + 7) % 7;
    return d;
}

int SeasonCalendar::nth_day(uint8_t code, int n) const {
    for (size_t d = 0; d < events.size(); d++)
        if (events[d] == code && --n == 0) return int(d);
    return -1;
}

int SeasonCalendar::league_matchday_day(int n) const {
    for (size_t d = 0; d < events.size(); d++)
        if (league_day(int(d)) && --n == 0) return int(d);
    return -1;
}

SeasonCalendar load_season_calendar(const Bytes& exe) {
    SeasonCalendar c;
    int section_ends = 0;
    for (size_t i = 0; i < kEventTableMax && section_ends < 2; i++) {
        size_t o = kDgroupFile + kEventTable + i;
        if (o >= exe.size()) throw FormatError("EM.EXE: calendar beyond file");
        c.events.push_back(exe[o]);
        section_ends += exe[o] == kEventSectionEnd;
    }
    if (section_ends != 2) throw FormatError("EM.EXE: calendar not found (not V1.00?)");
    return c;
}

}  // namespace em
