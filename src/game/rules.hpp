// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Version of the original program and the rules that differ between its releases.
//
// OpenEM reproduces version 1.00 of EM.EXE. The patch 1.30 replaced EM.EXE only (same size, the
// data files are unchanged) and changed these rules; every other rule is the same in both:
//   - A computer club that buys a player from a human manager gets stronger by an amount that
//     depends on the club's mean strength. Version 1.00 reads that mean from the wrong bytes
//     (an offset left over from the transfer screen); 1.30 takes the mean of the buyer's own
//     four skill values (defence/attack skill 1 and skill 2) / 4 (see sale_quote.cpp).
//   - The rating routine (new season, the twice-weekly drift and the world generator) re-rolls
//     the form of both kinds. In 1.00 its loop runs over three bytes, so the energy of the first
//     defence line (+0x1E) is overwritten with rand(45, 55) as well and one more random number is
//     drawn per team; 1.30 re-rolls the two form values only (see season_end.hpp).
//   - The attendance of a European cup home game in the final round: 1.00 gives it no
//     competition bonus at all (fewer spectators than in the group phase), 1.30 the same bonus
//     as the group phase (see finances.hpp, attendance_kind).
//   - The settings page shows "V1.30" instead of "V1.00".
//   - The copy-protection question is skipped; OpenEM never asks it, so nothing changes here.
// The version is recognised by the version text in the data segment of EM.EXE (file offset
// 0x19F43, "V1.00" or "V1.30"); anything else counts as 1.00.
#include <cstddef>
#include <string>

#include "formats/binary.hpp"

namespace em {

enum class GameVersion { V100, V130 };

// The rule set of the EM.EXE the game runs with; the default is version 1.00.
struct GameRules {
    GameVersion version = GameVersion::V100;
    bool v130() const { return version == GameVersion::V130; }
    // The version text of the settings page ("V1.00 - Ende: ..."): "V1.00" or "V1.30".
    const char* label() const { return v130() ? "V1.30" : "V1.00"; }
};

// File offset of the five-character version text in EM.EXE (data segment offset 0x3173).
constexpr size_t kExeVersionText = 0x19F43;

// Detects the version from the version text of EM.EXE; an unknown text or a short file gives 1.00.
inline GameRules detect_game_rules(const Bytes& exe) {
    GameRules rules;
    if (exe.size() >= kExeVersionText + 5 &&
        std::string(exe.begin() + std::ptrdiff_t(kExeVersionText), exe.begin() + std::ptrdiff_t(kExeVersionText + 5)) == "V1.30")
        rules.version = GameVersion::V130;
    return rules;
}

}  // namespace em
