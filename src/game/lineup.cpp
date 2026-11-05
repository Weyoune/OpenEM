// SPDX-License-Identifier: GPL-3.0-only
#include "game/lineup.hpp"

#include <algorithm>
#include <array>

namespace em {
namespace {
constexpr size_t kPlayers = 0x4FC6, kPlayerSize = 33, kRecord = 52;
constexpr int kPlaces = 29;
}  // namespace

bool auto_lineup(HokFile& h, int manager) {
    if (manager < 0 || manager >= kHokManagers) throw FormatError("HOK: invalid lineup manager");
    const size_t mode = 0x078A + size_t(manager) * 2;
    if (h.state.at(mode) == 0) return false;
    const size_t base = 0x696D + size_t(manager) * 0x618;
    for (int slot = 0; slot < kPlaces; slot++) h.state.at(base + size_t(slot) * kRecord + 0x0A) = 0;
    auto pick = [&](int requested) {
        const int direction = requested == 2 && h.state.at(mode) < 4 ? -1 : 1;
        bool saw_goalkeeper = false;
        int kind = requested;
        for (int pass = 0; pass < 8; pass++) {
            int best = -1, strength = 0;
            bool any_available = false;
            for (int slot = 0; slot < kPlaces; slot++) {
                const size_t at = base + size_t(slot) * kRecord;
                const int id = h.state.at(at + 15);
                if (!id || h.state.at(at + 10) || (h.state.at(at + 9) & 3)) continue;
                any_available = true;
                const int pos = h.state.at(kPlayers + size_t(id - 1) * kPlayerSize + 24);
                const int natural = pos == 0 ? 0 : pos < 60 ? 1 : 2;
                if (natural != kind) continue;
                int candidate = manager == 0 && size_t(slot) < h.squad.size()
                    ? h.squad[size_t(slot)].strength()
                    : (h.state.at(at + 16) + h.state.at(at + 17) + h.state.at(at + 18)) / 3;
                if (natural == 0 && requested != 0) {
                    candidate = saw_goalkeeper ? 1 : -1;
                    saw_goalkeeper = true;
                }
                if (candidate >= strength) { strength = candidate; best = slot; }  // native ties: last record
            }
            if (best >= 0 || !any_available) return best;
            kind = (kind + direction + 4) % 4;
            // ovl09_2858 stops after one full class cycle once it has
            // rejected a lone goalkeeper for an outfield position. A second
            // cycle would mistake that same keeper for another candidate.
            if (kind == requested && saw_goalkeeper) break;
        }
        return -1;
    };
    auto assign = [&](int kind, int line) {
        const int slot = pick(kind);
        if (slot < 0) return false;
        const size_t at = base + size_t(slot) * kRecord;
        h.state.at(at + 10) = 1;
        h.state.at(at + 21) = uint8_t(line);
        return true;
    };
    int selected = 0;
    for (int kind = 0; kind < 3; kind++)
        for (int line = 0; line < (kind == 0 ? 1 : 3); line++)
            for (int member = 0; member < kind + 1; member++)
                if (assign(kind, line + 1 + (kind == 2 ? 128 : 0))) selected++;
    bool found = true;
    while (selected < 22 && found) {
        found = false;
        for (int kind = 2; kind >= 0 && selected < 22; kind--)
            if (assign(kind, 0)) { selected++; found = true; }
    }
    if (manager == 0)
        for (size_t slot = 0; slot < h.squad.size() && slot < size_t(kPlaces); slot++) {
            if (h.squad[slot].player < 0) continue;
            h.squad[slot].role = h.state.at(base + slot * kRecord + 10);
            h.squad[slot].line = h.state.at(base + slot * kRecord + 21);
        }
    return true;
}

namespace {
using ManualCells = std::array<std::array<int, 6>, 5>;

const uint8_t* manual_record(const HokFile& h, size_t offset, size_t length) {
    if (offset > h.state.size() || length > h.state.size() - offset)
        throw FormatError("HOK: truncated manual lineup record");
    return h.state.data() + offset;
}

int manual_role(const HokFile& h, int id) {
    if (id < 1 || id > kHokPlayers) throw FormatError("HOK: invalid manual lineup player id");
    const uint8_t* p = manual_record(h, kPlayers + size_t(id - 1) * kPlayerSize, kPlayerSize);
    return std::min(int(p[0x18]) / 25, 2); //1238:0000, not the automatic picker's categories
}

struct ManualDiagram {
    ManualCells cells{};
    bool six = false;
};

ManualDiagram manual_diagram(const HokFile& h, size_t base, int count) {
    ManualDiagram diagram;
    int first = 0;
    //59B5:03FB..0503: reproduce the local diagram, including assigned
    // suspended players with0A=0; injury status2 is excluded independently.
    for (int slot = 0; slot < count; ++slot) {
        const uint8_t* r = h.state.data() + base + size_t(slot) * kRecord;
        const int status = r[9] & 3, line = r[0x15] & 0x7f;
        if ((!r[0x0a] && status != 1) || status == 2 || !line) continue;
        const int role = manual_role(h, r[0x0f]);
        if (line == 1) ++first;
        if (!role) {
            if (!diagram.cells[4][4]) diagram.cells[4][4] = slot + 1;
            continue;
        }
        if (line > 5) throw FormatError("HOK: invalid manual lineup line");
        const int half = (r[0x15] & 0x80) ? 0 : 3;
        for (int cell = half; cell < half + 3; ++cell) {
            int& destination = diagram.cells[size_t(line - 1)][size_t(cell)];
            if (!destination) { destination = slot + 1; break; }
        }
    }
    diagram.six = first == 6 && !diagram.cells[4][4];
    return diagram;
}

void sync_manual_player(HokFile& h, int manager, size_t slot, size_t at) {
    if (manager || slot >= h.squad.size() || h.squad[slot].player < 0) return;
    auto& player = h.squad[slot];
    player.role = h.state[at + 0x0a];
    player.line = h.state[at + 0x15];
    // Keep the same status meaning as hok_squad():1A is appearance/team
    // state, not the chosen line. Ordinary menu actions do not change1A.
    player.status = !player.role ? SquadStatus::None
        : (h.state[at + 0x1a] & 0x80) ? SquadStatus::Team : SquadStatus::Reserve;
}
}

ManualLineupResult manual_lineup(HokFile& h, int manager, size_t slot,
                                int selectedLine, bool reserveToggle) {
    return manual_lineup(h, manager, slot, selectedLine,
        reserveToggle ? ManualLineupAction::ReserveToggle : ManualLineupAction::Select);
}

ManualLineupResult manual_lineup(HokFile& h, int manager, size_t slot,
                                int selectedLine, ManualLineupAction action,const bool* sixOverride,
                                const std::array<std::array<int,6>,5>* cellsOverride) {
    if (manager < 0 || manager >= kHokManagers)
        throw FormatError("HOK: invalid manual lineup manager");
    if (selectedLine < 0 || selectedLine > 5)
        throw FormatError("HOK: invalid selected manual lineup line");
    if (action != ManualLineupAction::Select && action != ManualLineupAction::ReserveToggle &&
        action != ManualLineupAction::RemoveNumber)
        throw FormatError("HOK: invalid manual lineup action");
    const size_t base = 0x696d + size_t(manager) * 0x618;
    (void)manual_record(h, base, 0x618);
    const int available = manager == 4 ? 12 : kPlaces;
    int count = 0, members = 0;
    for (int i = 0; i < available; ++i) {
        const uint8_t* r = h.state.data() + base + size_t(i) * kRecord;
        if (r[0x0f]) ++count;
        if (r[0x0a]) ++members;
    }
    if (slot >= size_t(count)) throw FormatError("HOK: manual lineup slot outside native row range");
    const size_t at = base + slot * kRecord, mode = 0x078a + size_t(manager) * 2;
    uint8_t* r = h.state.data() + at;
    const int role = manual_role(h, r[0x0f]);
    ManualDiagram diagram = manual_diagram(h, base, count);
    if(sixOverride)diagram.six=*sixOverride;
    if(cellsOverride)diagram.cells=*cellsOverride;
    int old_block = -1, old_cell = -1;
    for (int block = 0; block < 5 && old_block < 0; ++block)
        for (int cell = 0; cell < 6; ++cell)
            if (diagram.cells[size_t(block)][size_t(cell)] == int(slot + 1)) {
                old_block = block; old_cell = cell; break;
            }

    ManualLineupResult result;
    result.selected_line = selectedLine;
    //0ACC..0AE4: an empty/non-visible number plate never reaches the
    // automatic-disable branch. It is not equivalent to a roster left click.
    if (action == ManualLineupAction::RemoveNumber) {
        const bool visible = old_block >= 0 && (old_block == 4 ? old_cell == 4
            : old_cell < 5 || (old_block == 0 && diagram.six));
        if (!visible) return result;
        result.selected_line = old_block + 1;
    }
    if (h.state[mode]) { h.state[mode] = 0; result.automatic_disabled = true; }
    const auto fail = [&](ManualLineupMessage message) {
        result.message = message;
        return result;
    };
    const uint8_t before_role = r[0x0a], before_line = r[0x15];
    const auto finish = [&]() {
        result.changed = r[0x0a] != before_role || r[0x15] != before_line;
        if (result.changed) sync_manual_player(h, manager, slot, at);
        return result;
    };

    const int status = r[9] & 3;
    if (action == ManualLineupAction::RemoveNumber) {
        //0B57..0B7D always uses "noch gesperrt", even for status3. A
        // zero remaining-duration byte permits this diagram-only removal.
        if (r[0x0d] && status) return fail(ManualLineupMessage::Suspended);
        r[0x0a] = 0;
        r[0x15] = 0;
        return finish();
    }

    //0E21..0F22 refusal order is observable. In particular automatic mode
    // has already been disabled before any of these messages are returned.
    if (status == 1) return fail(ManualLineupMessage::Suspended);
    if (status == 2) return fail(ManualLineupMessage::Injured);
    if (!r[0x0a] && members == 22) return fail(ManualLineupMessage::TeamFull);
    const bool reserve = action == ManualLineupAction::ReserveToggle;
    if (!reserve && !r[0x15]) {
        if (!selectedLine) return fail(ManualLineupMessage::NoLineSelected);
        if ((role == 0) != (selectedLine == 5)) return fail(ManualLineupMessage::WrongRole);
    }
    if (!reserve && !role && selectedLine == 5 && diagram.six)
        return fail(ManualLineupMessage::SixFieldPlayers);

    //0F2D..1293: left removes an assigned player; right toggles between
    // reserve and no function. Selecting a different block does not move an
    // already assigned player in one click: the first click removes him.
    if (reserve || (r[0x0a] && r[0x15])) {
        if (r[0x0a] && !r[0x15]) {
            r[0x0a] = 0;
        } else {
            if (r[0x0a] && r[0x15] && old_block >= 0) result.selected_line = old_block + 1;
            r[0x0a] = reserve ? 1 : 0;
            r[0x15] = 0;
        }
        return finish();
    }

    if (!selectedLine) return finish(); // only reachable for stale nonzero15
    const int block = selectedLine - 1;
    int half = role < 2 ? 3 : 0;
    if (!half && block == 4) return finish(); //native12D6..12E2
    const auto assign = [&](int cell) {
        r[0x0a] = 1;
        r[0x15] = uint8_t(block == 4 ? 1 : block + 1 + (cell < 3 ? 0x80 : 0));
        result.assigned_cell = cell;
        return finish();
    };
    //12E9..14FC: preferred natural half first, then the other half. A
    // third lower slot exists only after pulling TW from the first block.
    for (int pass = 0; pass < 2; ++pass, half = 3 - half) {
        const bool three = half == 0 || (block == 0 && diagram.six) || block == 4;
        const int first = half + (block == 4 ? 1 : 0);
        const int end = half + (three ? 3 : 2) - (block == 4 ? 1 : 0);
        for (int cell = first; cell < end; ++cell)
            if (!diagram.cells[size_t(block)][size_t(cell)]) return assign(cell);
        if (block == 4) break;
    }
    if (block == 0 && !diagram.six && !diagram.cells[4][4]) return assign(5);
    return fail(ManualLineupMessage::NoSpace);
}

}  // namespace em
