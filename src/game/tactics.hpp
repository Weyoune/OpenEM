// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include <array>
#include <cstdint>

namespace em {

struct TacticsProfile {
    std::array<uint8_t, 6> effort{};   // base, five situation additions
    std::array<uint8_t, 6> offense{};
};

// Transient native-dialog state. Opening/previewing never writes the source HOK.
// Commit on every native exit, including right-click and switch to Aufstellung;
// this draft does not introduce an Apply/Cancel interaction.
struct TacticsPlan {
    bool autoLineup = true;  // saved078A convention, not the inverted UI selector
    uint8_t lineMode = 0, effortMode = 0, offenseMode = 0;
    std::array<uint8_t, 8> rawQuotas{}; // four defence, four attack
    std::array<uint8_t, 6> effort{}, offense{};
    std::array<TacticsProfile, 4> profiles{};
    uint8_t preset = 0;
};

TacticsPlan load_tactics(const HokFile&, int manager = 0);

// control is native arrow index0..3: lineup, lines, effort, offense.
// delta is the enum change: -1 upper arrow, +1 lower arrow. No wrap.
bool edit_mode(TacticsPlan&, int control, int delta);

// arrow is native index4..23: interleaved defence/attack quotas4..11,
// then effort/offense rows12..23. delta is +1 upper arrow, -1 lower arrow.
// True means the native handler accepts the event; an event at a bound can
// still refresh the selected preset even though its numeric value stays fixed.
bool edit_numeric(TacticsPlan&, int arrow, int delta);
bool select_preset(TacticsPlan&, int preset);

// 5F7C:2EE2..3026, including the unusual (raw/2)|(raw&1) for rows0..2.
std::array<uint8_t, 4> normalize_tactics_quotas(const std::array<uint8_t, 4>&);

// Native exit: clamp effort additions, normalize quotas, restore current
// effort from base+tie, and run auto_lineup only on manual->automatic.
// Return true if this transition invoked automatic lineup. No RNG is used.
bool commit_tactics(HokFile&, const TacticsPlan&, int manager = 0);

// Independent deep snapshot for the frozen classic renderer. No exit clamps,
// quota normalization, or automatic lineup are performed on this preview.
HokFile preview_tactics(const HokFile&, const TacticsPlan&, int manager = 0);

} // namespace em
