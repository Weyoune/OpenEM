// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The "Taktik" dialog of a human manager: lineup mode, line rotation mode with ice-time quotas,
// effort ("Einsatz") and offence settings per game situation, and four stored presets.
//
// Save state fields (m = manager 0..3):
//   0x078A + 2m      automatic lineup (1) / manual (0)
//   0x01F9 + m       line mode 0..5 (4 = manual quotas)
//   0x01FF + m       effort mode 0..2 (1 = manual values)
//   0x01F3 + m       offence mode 0..2 (1 = manual values)
//   0x028B + 8m      raw quotas as edited: four defence lines, then four attack lines
//   0x025B + 8m      normalized quotas used in matches (same order)
//   0xB96B + 48m     four presets: 4 x 6 effort bytes, then 4 x 6 offence bytes
//   0xBA8B + m       selected preset 0..3
//   team record +0x26 current effort, +0x27..+0x2B effort additions for the five situations,
//   +0x2D effort base, +0x35 offence base, +0x36..+0x3A offence for the five situations
#include "formats/hok.hpp"
#include <array>
#include <cstdint>

namespace em {

// One preset: base value and five situation additions for effort and offence.
struct TacticsProfile {
    std::array<uint8_t, 6> effort{};   // base, five situation additions
    std::array<uint8_t, 6> offense{};
};

// Transient dialog state. Opening/previewing never writes the source HOK.
// The original commits on every exit of the dialog, including right-click and the switch to
// "Aufstellung"; this draft does not introduce an Apply/Cancel interaction.
struct TacticsPlan {
    bool autoLineup = true;  // the saved convention (true = automatic), not the inverted UI selector
    uint8_t lineMode = 0, effortMode = 0, offenseMode = 0;
    std::array<uint8_t, 8> rawQuotas{}; // four defence, four attack
    std::array<uint8_t, 6> effort{}, offense{};  // base, five situation additions
    std::array<TacticsProfile, 4> profiles{};
    uint8_t preset = 0;
};

// Reads the plan of `manager` from the save state; throws FormatError for invalid stored values.
TacticsPlan load_tactics(const HokFile&, int manager = 0);

// control is the arrow index 0..3 of the dialog: lineup, lines, effort, offense.
// delta is the enum change: -1 upper arrow, +1 lower arrow. No wrap. Entering mode 1 of the line,
// effort or offence control stores the current values into the selected preset. Returns false
// when the value is at its bound.
bool edit_mode(TacticsPlan&, int control, int delta);

// arrow is the dialog's arrow index 4..23: interleaved defence/attack quotas 4..11 (only in line
// mode 4; each kind's four quotas sum to at most 20), then effort/offense rows 12..23 (only in
// manual effort/offence mode; effort base + addition <= 100, offence base + addition <= 7; the
// base's limit ignores the fifth situation addition, as in the original). delta is +1 upper arrow,
// -1 lower arrow. True means the dialog accepts the event; an event at a bound still copies the
// values into the selected preset even though its numeric value stays fixed.
bool edit_numeric(TacticsPlan&, int arrow, int delta);
// Switches to preset 0..3 and loads its values; false if it is already selected.
bool select_preset(TacticsPlan&, int preset);

// Turns four raw quotas into the active ones used in matches: raw / 2, for rows 0..2 also OR'ed
// with the lowest bit (an unusual but original rule), then rows are adjusted one step at a time
// (starting with row 3, then 2, 1, 0, 3, ...) until the sum is 8; a row is lowered only while
// above 1, and row 3 is raised only while above 1.
std::array<uint8_t, 4> normalize_tactics_quotas(const std::array<uint8_t, 4>&);

// The dialog exit: writes the plan, clamps effort additions so base + addition <= 100,
// normalizes the quotas, restores the current effort as base + the "draw" addition, and runs
// auto_lineup only on a switch from manual to automatic. Returns true if this transition invoked
// the automatic lineup. No random numbers are used.
bool commit_tactics(HokFile&, const TacticsPlan&, int manager = 0);

// Independent deep copy with the plan written in, for drawing the classic dialog. No exit clamps,
// quota normalization, or automatic lineup are performed on this preview.
HokFile preview_tactics(const HokFile&, const TacticsPlan&, int manager = 0);

} // namespace em
