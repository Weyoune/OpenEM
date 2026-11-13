// SPDX-License-Identifier: GPL-3.0-only
// The tactics dialog model: loading, editing, normalizing and committing a plan; see tactics.hpp.
#include "game/tactics.hpp"
#include "game/lineup.hpp"
#include <algorithm>

namespace em {
namespace {
constexpr size_t kProfiles = 0xB96B, kSelected = 0xBA8B;  // presets and selected preset per manager
constexpr size_t kTeams = 0x17E7;                           // team records (61 bytes each)

// Validates `manager` and that the state is large enough; returns it as an index.
size_t manager_index(const HokFile& h, int manager) {
    if (manager < 0 || manager >= kHokManagers)
        throw FormatError("tactics: invalid manager");
    const size_t m = size_t(manager);
    // This last serialized tactics byte also bounds every earlier raw access.
    (void)h.state.at(kSelected + m);
    return m;
}
// State offset of the team record of manager m's club (manager block +0x14).
size_t team_offset(const HokFile& h, size_t m) {
    const size_t id = h.state.at(0x0DCD + m * 0x1AF + 0x14);
    if (id >= 150) throw FormatError("tactics: invalid manager team");
    return kTeams + id * 61;
}
// Throws FormatError for modes or a preset outside the dialog's ranges.
void validate(const TacticsPlan& p) {
    if (p.lineMode > 5 || p.effortMode > 2 || p.offenseMode > 2 || p.preset > 3)
        throw FormatError("tactics: invalid native mode or preset");
}
// Throws FormatError unless delta is -1 or +1.
void direction(int delta) {
    if (delta != -1 && delta != 1)
        throw FormatError("tactics: arrow delta must be -1 or +1");
}
// Copies the current effort/offence values into the selected preset.
void remember(TacticsPlan& p) {
    p.profiles[p.preset].effort = p.effort;
    p.profiles[p.preset].offense = p.offense;
}

// Only these decoded team fields change. Keep store_hok from reverting edits.
void sync_team(HokFile& h, size_t team) {
    const size_t id = (team - kTeams) / 61;
    if (id >= h.teams.size()) return;
    auto& t = h.teams[id];
    t.effort_current = h.state.at(team + 0x26);
    t.effort_base = h.state.at(team + 0x2D);
    t.offence_base = h.state.at(team + 0x35);
    for (size_t i = 0; i < 5; ++i) {
        t.effort[i] = h.state.at(team + 0x27 + i);
        t.offence[i] = h.state.at(team + 0x36 + i);
    }
}
// Writes all plan fields into the state (see the table in tactics.hpp); the effort base is
// written to +0x26 and +0x2D.
void write_plan(HokFile& h, const TacticsPlan& p, size_t m, size_t team) {
    h.state.at(0x078A + 2 * m) = uint8_t(p.autoLineup);
    h.state.at(0x01F9 + m) = p.lineMode;
    h.state.at(0x01FF + m) = p.effortMode;
    h.state.at(0x01F3 + m) = p.offenseMode;
    h.state.at(kSelected + m) = p.preset;
    for (size_t i = 0; i < 8; ++i) h.state.at(0x028B + 8 * m + i) = p.rawQuotas[i];
    for (size_t i = 0; i < 6; ++i) {
        h.state.at(team + 0x26 + i) = p.effort[i];
        h.state.at(team + 0x35 + i) = p.offense[i];
    }
    h.state.at(team + 0x2D) = p.effort[0];
    for (size_t slot = 0; slot < 4; ++slot)
        for (size_t i = 0; i < 6; ++i) {
            h.state.at(kProfiles + 48 * m + 6 * slot + i) = p.profiles[slot].effort[i];
            h.state.at(kProfiles + 48 * m + 24 + 6 * slot + i) = p.profiles[slot].offense[i];
        }
}
} // namespace

TacticsPlan load_tactics(const HokFile& h, int manager) {
    const size_t m = manager_index(h, manager), team = team_offset(h, m);
    if (h.state.at(0x078A + 2 * m) > 1)
        throw FormatError("tactics: lineup selector outside native dialog range");
    TacticsPlan p;
    p.autoLineup = h.state.at(0x078A + 2 * m) != 0;
    p.lineMode = h.state.at(0x01F9 + m);
    p.effortMode = h.state.at(0x01FF + m);
    p.offenseMode = h.state.at(0x01F3 + m);
    p.preset = h.state.at(kSelected + m);
    for (size_t i = 0; i < 8; ++i) p.rawQuotas[i] = h.state.at(0x028B + 8 * m + i);
    for (size_t i = 0; i < 6; ++i) {
        p.effort[i] = h.state.at(team + (i == 0 ? 0x2D : 0x26 + i));
        p.offense[i] = h.state.at(team + 0x35 + i);
    }
    for (size_t slot = 0; slot < 4; ++slot)
        for (size_t i = 0; i < 6; ++i) {
            p.profiles[slot].effort[i] = h.state.at(kProfiles + 48 * m + 6 * slot + i);
            p.profiles[slot].offense[i] = h.state.at(kProfiles + 48 * m + 24 + 6 * slot + i);
        }
    validate(p);
    return p;
}

bool edit_mode(TacticsPlan& p, int control, int delta) {
    validate(p); direction(delta);
    if (control < 0 || control > 3) throw FormatError("tactics: invalid mode control");
    if (control == 0) {
        const int next = (p.autoLineup ? 0 : 1) + delta;
        if (next < 0 || next > 1) return false;
        p.autoLineup = next == 0;
        return true;
    }
    uint8_t& mode = control == 1 ? p.lineMode : control == 2 ? p.effortMode : p.offenseMode;
    const int next = int(mode) + delta, maximum = control == 1 ? 5 : 2;
    if (next < 0 || next > maximum) return false;
    mode = uint8_t(next);
    // This also applies to lineMode entering 1, not only to the effort and offence modes.
    if (mode == 1) remember(p);
    return true;
}

bool edit_numeric(TacticsPlan& p, int arrow, int delta) {
    validate(p); direction(delta);
    if (arrow < 4 || arrow > 23) throw FormatError("tactics: invalid numeric control");
    uint8_t* value;
    uint8_t maximum;
    if (arrow < 12) {
        if (p.lineMode != 4) return false;
        const size_t index = size_t(arrow - 4), kind = index % 2, row = index / 2;
        value = &p.rawQuotas[kind * 4 + row];
        maximum = 20;
        for (size_t i = 0; i < 4; ++i)
            if (i != row) maximum = uint8_t(maximum - p.rawQuotas[kind * 4 + i]);
    } else {
        const size_t index = size_t(arrow - 12), kind = index % 2, row = index / 2;
        if ((kind == 0 ? p.effortMode : p.offenseMode) != 1) return false;
        auto& values = kind == 0 ? p.effort : p.offense;
        value = &values[row];
        const int limit = kind == 0 ? 100 : 7;
        maximum = uint8_t(limit);
        if (row == 0) {
            // The original deliberately excludes the fifth situation addition here.
            for (size_t i = 1; i < 5; ++i)
                if (limit - int(values[i]) < int(maximum)) maximum = uint8_t(limit - values[i]);
        } else maximum = uint8_t(limit - values[0]);
    }
    if (delta < 0 && *value != 0) --*value;
    else if (delta > 0 && *value < maximum) ++*value;
    // The original makes these two six-byte copies even if a bound prevented a change.
    remember(p);
    return true;
}

bool select_preset(TacticsPlan& p, int preset) {
    validate(p);
    if (preset < 0 || preset > 3) throw FormatError("tactics: invalid preset");
    if (p.preset == preset) return false;
    p.preset = uint8_t(preset);
    p.effort = p.profiles[size_t(preset)].effort;
    p.offense = p.profiles[size_t(preset)].offense;
    return true;
}

std::array<uint8_t, 4> normalize_tactics_quotas(const std::array<uint8_t, 4>& raw) {
    std::array<uint8_t, 4> active{};
    uint8_t sum = 0;
    for (size_t i = 0; i < 4; ++i) {
        active[i] = uint8_t(raw[i] / 2);
        if (i < 3) active[i] |= raw[i] & 1;
        sum = uint8_t(sum + active[i]);
    }
    const int delta = sum > 8 ? -1 : 1;
    int row = 3;
    while (sum != 8) {
        auto& value = active[size_t(row)];
        if (value > 1 || (delta == 1 && row != 3)) {
            value = uint8_t(value + delta);
            sum = uint8_t(sum + delta);
        }
        row = (row + 3) % 4;
    }
    return active;
}

bool commit_tactics(HokFile& h, const TacticsPlan& p, int manager) {
    validate(p);
    const size_t m = manager_index(h, manager), team = team_offset(h, m);
    const bool was_auto = h.state.at(0x078A + 2 * m) != 0;
    write_plan(h, p, m, team);
    for (size_t i = 1; i < 6; ++i)
        if (int(p.effort[0]) + p.effort[i] > 100)
            h.state.at(team + 0x26 + i) = uint8_t(100 - p.effort[0]);
    for (size_t kind = 0; kind < 2; ++kind) {
        std::array<uint8_t, 4> raw{};
        std::copy_n(p.rawQuotas.begin() + std::ptrdiff_t(kind * 4), 4, raw.begin());
        const auto active = normalize_tactics_quotas(raw);
        for (size_t i = 0; i < 4; ++i) h.state.at(0x025B + m * 8 + kind * 4 + i) = active[i];
    }
    h.state.at(team + 0x26) = uint8_t(p.effort[0] + h.state.at(team + 0x29));
    sync_team(h, team);
    if (!was_auto && p.autoLineup) return auto_lineup(h, manager);
    return false;
}

HokFile preview_tactics(const HokFile& h, const TacticsPlan& p, int manager) {
    validate(p);
    const size_t m = manager_index(h, manager), team = team_offset(h, m);
    auto copy = clone_hok(h);
    write_plan(copy, p, m, team);
    sync_team(copy, team);
    return copy;
}
} // namespace em
