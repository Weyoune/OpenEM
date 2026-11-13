// SPDX-License-Identifier: GPL-3.0-only
// Round-robin schedule tables read from EM.EXE and the matchday pairings; see schedule.hpp.
#include "game/schedule.hpp"

#include <utility>

namespace em {

namespace {
// The data segment starts at file offset 0x1E00 (size of the EXE header) + 0x14FD0 (its position in
// the program image).
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;
// Offsets of the three round tables within the data segment.
constexpr size_t kTable12 = 0x03C0, kTable6 = 0x0450, kTable4 = 0x0474;

// Reads consecutive NUL-terminated strings starting at data segment offset `ds` until an empty string.
std::vector<std::string> read_list(const Bytes& exe, size_t ds) {
    std::vector<std::string> out;
    size_t p = kDgroupFile + ds;
    while (p < exe.size() && exe[p] != 0) {
        std::string s;
        while (p < exe.size() && exe[p] != 0) s += char(exe[p++]);
        out.push_back(std::move(s));
        p++;  // NUL
    }
    return out;
}
}  // namespace

const std::vector<std::string>& ScheduleTables::for_size(int n) const {
    if (n == 12) return teams12;
    if (n == 6) return teams6;
    if (n == 4) return teams4;
    throw FormatError("no schedule table for " + std::to_string(n) + " teams");
}

ScheduleTables load_schedule_tables(const Bytes& exe) {
    ScheduleTables t{read_list(exe, kTable12), read_list(exe, kTable6), read_list(exe, kTable4)};
    if (t.teams12.size() != 11 || t.teams6.size() != 5 || t.teams4.size() != 3)
        throw FormatError("EM.EXE: schedule tables not found (not V1.00?)");
    return t;
}

std::vector<Pairing> round_robin_pairings(const std::vector<std::string>& rounds, int matchday) {
    const int n = int(rounds.size()) + 1;  // teams; a single round robin has n - 1 rounds
    bool swap = false;
    while (matchday >= n - 1) {  // every further round robin swaps home and away
        matchday -= n - 1;
        swap = !swap;
    }
    const std::string& r = rounds[size_t(matchday)];
    std::vector<Pairing> out;
    for (size_t k = 0; k + 1 < r.size(); k += 2) {
        Pairing p{r[k] - 'a', r[k + 1] - 'a'};
        if (swap) std::swap(p.home, p.away);
        out.push_back(p);
    }
    return out;
}

}  // namespace em
