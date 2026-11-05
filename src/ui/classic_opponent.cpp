// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_opponent.hpp"
#include <array>

namespace em::ui {
namespace {
struct Match {
    uint8_t team = 0xFF, side = 0xFF;
    explicit operator bool() const { return team != 0xFF; }
};

bool controlled(const Bytes& s, uint8_t team, int manager) {
    if (team == 0xFF) return false;
    if (team >= kHokTeams) throw FormatError("classic opponent: invalid native team");
    return s.at(0x17E7 + size_t(team) * 61 + 0x16) == manager + 1;
}
Match adjacent(const Bytes& s, size_t base, size_t participant) {
    const auto team = s.at(base + (participant ^ 1));
    if (team != 0xFF && team >= kHokTeams)
        throw FormatError("classic opponent: invalid native opponent");
    return {team, uint8_t(1 - participant % 2)};
}

// Lookup-only27A5(kind,group,1,round,round+1,0,0). Entrant lookup
// checks indices0..2 and otherwise uses index3, exactly2BB6..2CC5.
Match group_match(const Bytes& s, int manager, int kind, int group, int round) {
    if (kind == 1 && s.at(0x03B6) == 0xFF) return {};
    if (kind == 3 && s.at(0x8F86) < 6) return {};
    if (round < 0 || round >= 6) return {};
    const size_t entrants = 0x039C + size_t(kind - 1) * 8 + size_t(group) * 4;
    const size_t schedule = 0x03B8 + size_t(kind) * 72 + size_t(group) * 36 + size_t(round % 3) * 6;
    const auto entrant = [&](uint8_t scheduled) {
        size_t index = 0;
        while (index < 3 && s.at(entrants + index) != scheduled) ++index;
        return s.at(entrants + index);
    };
    for (size_t pair = 0; pair < 2; ++pair) {
        const auto first = entrant(s.at(schedule + pair * 2));
        const auto second = entrant(s.at(schedule + pair * 2 + 1));
        if (controlled(s, first, manager)) return {second, uint8_t(round < 3 ? 1 : 0)};
        if (controlled(s, second, manager)) return {first, uint8_t(round < 3 ? 0 : 1)};
    }
    return {};
}

Match world_match(const Bytes& s, int manager) {
    Match result;
    const int round = s.at(0x8F85);
    if (round < 5) {
        for (size_t group = 0; group < 2; ++group) {
            const size_t base = 0x03B8 + group * 36 + size_t(round) * 6;
            for (size_t i = 0; i < 6; ++i)
                if (controlled(s, s.at(base + i), manager)) result = adjacent(s, base, i);
        }
    } else {
        // Native5D5B list is the32-byte tail of its serialized5CFB block.
        const size_t cursor = s.at(0x03B7);
        if (cursor == 0xFF) return {};
        for (size_t i = cursor; i < 32 && s.at(0x8EB1 + i) != 0xFF; ++i)
            if (controlled(s, s.at(0x8EB1 + i), manager)) result = adjacent(s, 0x8EB1, i);
    }
    return result;
}

Match playoff_match(const Bytes& s, int manager) {
    const int game = s.at(0x0259);
    const int round = 2 * s.at(0x8F87) + (game > 0) + (game > 2);
    if (game == 0 || game == 2)
        for (int group = 0; group < 2; ++group)
            if (const auto match = group_match(s, manager, 1, group, round)) return match;

    Match result;
    const size_t cursor = s.at(0x03B6);
    if (cursor == 0xFF || (!s.at(0x077D) && game != 0)) return result;
    // Four quarterfinals,two semifinals,one final:14 participant bytes.
    // The next byte is FF. Both100 score-column guards address this same index.
    for (size_t i = cursor; i < 14 && s.at(0x8E91 + i) != 0xFF; ++i)
        if (controlled(s, s.at(0x8E91 + i), manager) &&
            s.at(0x0205 + 42 + i) != 100 && s.at(0x0205 + 56 + i) != 100)
            result = adjacent(s, 0x8E91, i);
    return result;
}

Match cup_match(const Bytes& s, int manager) {
    // Original DS03B1 table, not inferred from the decoded draw vector.
    constexpr std::array<int, 7> pairs{32, 16, 8, 4, 2, 1, 0};
    const size_t round = s.at(0x8ED1);
    if (round >= pairs.size() || !s.at(0x077C)) return {};
    Match result;
    for (size_t i = 0; i < size_t(pairs[round] * 2); ++i)
        if (controlled(s, s.at(0x8E51 + i), manager) && s.at(0x035C + i) != 100)
            result = adjacent(s, 0x8E51, i);
    return result;
}
} // namespace

ClassicOpponent classic_opponent(const HokFile& h, const SeasonCalendar& calendar,
                                 int activeManager, bool suppressMatchSelection) {
    if (activeManager < 0 || activeManager >= kHokManagers)
        throw FormatError("classic opponent: invalid active manager");
    const Bytes& s = h.state;
    ClassicOpponent result;
    result.calendarIndex = le16(s, 0xB953);
    result.calendarEvent = calendar.event(result.calendarIndex);
    const auto menu_index = uint8_t(result.calendarIndex);
    result.selectedEvent = suppressMatchSelection || menu_index == 0xFF ? 0 : calendar.event(menu_index);
    result.seasonEnd = result.calendarEvent == kEventSectionEnd;
    const size_t block = 0x0DCD + size_t(activeManager) * 0x1AF;
    const int league = s.at(block + 0xEB);
    const uint8_t own = s.at(block + 0x14);
    Match match;
    // The three serialized normal league pairing arrays are already rebuilt
    // by Game's export; this pure helper does not mutate them as1CD2 does.
    if (!result.seasonEnd && league < 3 && ((result.selectedEvent >> league) & 1)) {
        const size_t base = 0x8DFD + size_t(league) * 12;
        for (size_t i = 0; i < 12; ++i)
            if (s.at(base + i) == own) { match = adjacent(s, base, i); break; }
    }
    if (!match) {
        // 18F8 compares these whole bytes exactly; combined event flags do not
        // independently activate the special competitions.
        switch (result.selectedEvent) {
        case kEventWorldChampionship: match = world_match(s, activeManager); break;
        case kEventPlayoff: match = playoff_match(s, activeManager); break;
        case kEventCup: match = cup_match(s, activeManager); break;
        case kEventEuroCup: {
            const int round = s.at(0x8F86);
            if (round < 6) {
                match = group_match(s, activeManager, 2, 0, round);
                if (!match) match = group_match(s, activeManager, 2, 1, round);
                if (!match) match = group_match(s, activeManager, 3, 0, round);
            } else match = group_match(s, activeManager, 3, 0, round - 6);
            break;
        }
        default: break;
        }
    }
    result.team = match.team;
    if (match) result.side = match.side;
    return result;
}
} // namespace em::ui
