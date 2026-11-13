// SPDX-License-Identifier: GPL-3.0-only
// Tests for the "Taktik" dialog of src/game/tactics.cpp: quota normalization, opening and
// previewing the dialog without touching the save, the arrow rules of the mode and numeric
// controls, the copying into the selected preset, the exit (commit) with its clamps written
// byte for byte, and the automatic lineup that runs only on a manual-to-automatic switch.
// The save-state fields involved are listed in src/game/tactics.hpp.
#include "game/tactics.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace em;
namespace {
// Throws `message` as a test failure unless `condition`.
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// The fixture's manager and his club; `team` is the club's 61-byte team record, `profile` the
// manager's 48-byte preset block, `squad` his squad block.
constexpr int manager = 1, team_id = 2;
constexpr size_t m = manager, team = 0x17E7 + team_id * 61;
constexpr size_t profile = 0xB96B + 48 * m, squad = 0x696D + 0x618 * m;

// Synthetic state with manager 1 running team 2: automatic lineup, preset 2 selected, team
// effort base 60 with additions 20/10/15/10/5 (current effort 75 = base + the "draw"
// addition), offence 2 with 1/2/3/4/5, raw quotas 4/6/6/0 for both kinds, and four presets that
// differ in their first bytes. No EXE, random generator or UI is needed by these tests.
HokFile fixture() {
    HokFile h;
    h.state.assign(0xBB00, 0);
    h.state.at(0x0DCD + 0x1AF * m + 0x14) = team_id;
    h.state.at(0x078A + 2 * m) = 1;
    h.state.at(0xBA8B + m) = 2;
    constexpr std::array<uint8_t, 6> effort{60, 20, 10, 15, 10, 5};
    constexpr std::array<uint8_t, 6> offense{2, 1, 2, 3, 4, 5};
    for (size_t i = 0; i < 6; ++i) {
        h.state.at(team + 0x26 + i) = effort[i];
        h.state.at(team + 0x35 + i) = offense[i];
    }
    h.state.at(team + 0x26) = 75; // saved current = base + tie addition, not the editable base
    h.state.at(team + 0x2D) = 60;
    constexpr std::array<uint8_t, 8> raw{4, 6, 6, 0, 4, 6, 6, 0};
    constexpr std::array<uint8_t, 8> active{2, 3, 3, 0, 2, 3, 3, 0};
    for (size_t i = 0; i < 8; ++i) {
        h.state.at(0x028B + 8 * m + i) = raw[i];
        h.state.at(0x025B + 8 * m + i) = active[i];
    }
    for (size_t slot = 0; slot < 4; ++slot) {
        const std::array<uint8_t, 6> e{uint8_t(40 + slot), uint8_t(1 + slot), 2, 3, 4, 5};
        const std::array<uint8_t, 6> o{uint8_t(slot), 1, 2, 3, 4, 5};
        for (size_t i = 0; i < 6; ++i) {
            h.state.at(profile + slot * 6 + i) = e[i];
            h.state.at(profile + 24 + slot * 6 + i) = o[i];
        }
    }
    h.teams.resize(3);
    h.teams[team_id].raw = h.state.data() + team;
    return h;
}
// Quota normalization: the original halves with a bitwise OR of the lowest bit instead of
// rounding up, so 3 / 2 | 1 stays 1, and then adjusts rows until the sum is 8.
void normalization_goldens() {
    require(normalize_tactics_quotas({0, 0, 0, 0}) == std::array<uint8_t, 4>{2, 3, 3, 0},
            "all-zero quotas redistribute backwards and avoid fourth line");
    require(normalize_tactics_quotas({3, 3, 3, 7}) == std::array<uint8_t, 4>{1, 1, 2, 4},
            "odd raw quotas use bitwise OR rather than ceiling");
    require(normalize_tactics_quotas({20, 0, 0, 0}) == std::array<uint8_t, 4>{8, 0, 0, 0},
            "oversubscribed active tuple decrements eligible entries");
    require(normalize_tactics_quotas({4, 6, 6, 0}) == std::array<uint8_t, 4>{2, 3, 3, 0},
            "saved four-six-six-zero tuple");
}
// Opening reads the plan (effort base +0x2D, not the current effort +0x26), the preview is a deep
// copy with rebased team pointers, and an unchanged exit leaves every byte as it was.
void opening_preview_and_identity() {
    auto h = fixture();
    const Bytes original = h.state;
    const auto p = load_tactics(h, manager);
    require(p.autoLineup && p.preset == 2, "saved lineup convention and preset selection");
    require(p.effort == std::array<uint8_t, 6>{60, 20, 10, 15, 10, 5},
            "opening loads base +0x2D rather than current +0x26");
    require(p.profiles[3].effort == std::array<uint8_t, 6>{43, 4, 2, 3, 4, 5},
            "fourth effort preset and manager stride");
    require(p.profiles[1].offense == std::array<uint8_t, 6>{1, 1, 2, 3, 4, 5},
            "offense presets follow all 24 effort bytes");
    auto preview = preview_tactics(h, p, manager);
    require(h.state == original, "opening and preview never change source HOK");
    require(preview.state.at(team + 0x26) == 60 && preview.teams[team_id].effort_current == 60,
            "preview contains the temporary base without exit normalization");
    require(preview.teams[team_id].raw == preview.state.data() + team &&
            preview.teams[team_id].raw != h.teams[team_id].raw, "preview deeply rebases record pointers");
    require(!commit_tactics(h, p, manager), "automatic-to-automatic does not rerun lineup");
    require(h.state == original, "unchanged canonical dialog has byte-identical exit");
    require(h.teams[team_id].effort_base == 60 && h.teams[team_id].effort_current == 75,
            "commit synchronizes decoded team fields");
}
// Mode arrows stop at their bounds, entering mode 1 copies the current values into the selected
// preset, numeric controls are disabled outside their manual modes, and selecting a preset
// loads its 6 + 6 bytes without changing the modes.
void mode_and_preset_rules() {
    auto h = fixture(); auto p = load_tactics(h, manager);
    require(!edit_mode(p, 0, -1) && p.autoLineup, "lineup upper bound does not wrap");
    require(edit_mode(p, 0, 1) && !p.autoLineup, "lower lineup arrow selects manual");
    require(!edit_mode(p, 0, 1), "lineup lower bound does not wrap");
    const auto old_profile = p.profiles[2].effort;
    require(!edit_numeric(p, 12, 1) && p.profiles[2].effort == old_profile,
            "AUTOM effort numeric is disabled and does not copy presets");
    require(edit_mode(p, 1, 1) && p.lineMode == 1 && p.profiles[2].effort == p.effort &&
            p.profiles[2].offense == p.offense, "entering line mode 1 copies BOTH current profiles");
    require(edit_mode(p, 2, 1) && p.effortMode == 1, "effort enters SEMI");
    require(edit_mode(p, 2, 1) && p.effortMode == 2 && !edit_numeric(p, 12, 1),
            "manual effort mode disables profile numeric controls");
    require(!edit_mode(p, 2, 1) && edit_mode(p, 2, -1), "component modes stop at 2 and return to SEMI");
    require(select_preset(p, 1), "select another stored tactic");
    require(p.effort == std::array<uint8_t, 6>{41, 2, 2, 3, 4, 5} &&
            p.offense == std::array<uint8_t, 6>{1, 1, 2, 3, 4, 5}, "preset loads exactly 6+6 bytes");
    require(p.lineMode == 1 && p.effortMode == 1 && p.offenseMode == 0,
            "preset selection never changes control modes");
    p.effort[0] = 99;
    require(!select_preset(p, 1) && p.effort[0] == 99, "clicking selected preset does not reload it");
}
// Numeric bounds: effort base + addition <= 100, offence base + addition <= 7, quotas only in
// line mode 4 with each kind summing to at most 20; an event at a bound still copies both
// current profiles into the selected preset.
void numeric_bounds_and_copying() {
    auto h = fixture(); auto p = load_tactics(h, manager);
    p.effortMode = 1;
    p.effort = {80, 20, 5, 0, 0, 0};
    require(edit_numeric(p, 12, 1) && p.effort[0] == 80 && p.profiles[2].effort == p.effort,
            "base upper bound still performs selected preset copy");
    p.effort = {60, 40, 10, 15, 10, 5};
    require(edit_numeric(p, 14, 1) && p.effort[1] == 40, "addition capped by 100-base");
    p.effort[1] = 0;
    require(edit_numeric(p, 14, -1) && p.effort[1] == 0, "addition lower bound 0");
    p.offenseMode = 1; p.offense = {2, 1, 5, 0, 0, 0};
    require(edit_numeric(p, 13, 1) && p.offense[0] == 2, "offense base bounded by 7-largest first 4 addition");
    p.lineMode = 3;
    require(!edit_numeric(p, 10, 1), "quota edit disabled outside minutes mode 4");
    p.lineMode = 4;
    for (int i = 0; i < 5; ++i) require(edit_numeric(p, 10, 1), "eligible fourth quota event");
    require(p.rawQuotas == std::array<uint8_t, 8>{4, 6, 6, 4, 4, 6, 6, 0},
            "defence quota sum cannot exceed 20; attack tuple untouched");
    p.profiles[2].offense.fill(0);
    require(edit_numeric(p, 10, 1) && p.profiles[2].offense == p.offense,
            "quota event at maximum also copies both tactic profiles");
}
// The base limits ignore the fifth situation addition (original quirk). The exit then writes
// the complete expected state: modes, team effort/offence with additions clamped to
// base + addition <= 100, raw and normalized quotas, and the preset copied before the clamps.
void fifth_addition_and_exit_goldens() {
    auto h = fixture(); auto p = load_tactics(h, manager);
    p.effortMode = 1; p.offenseMode = 1;
    p.effort = {90, 0, 0, 0, 0, 100};
    p.offense = {3, 0, 0, 0, 0, 7};
    require(edit_numeric(p, 12, 1) && p.effort[0] == 91,
            "effort base bound omits fifth situation");
    require(edit_numeric(p, 13, 1) && p.offense[0] == 4,
            "offense base bound also omits fifth situation");
    p.rawQuotas = {0, 0, 0, 0, 3, 3, 3, 7};
    const Bytes before = h.state;
    auto expected = before;
    expected.at(0x01FF + m) = 1; expected.at(0x01F3 + m) = 1;
    constexpr std::array<uint8_t, 6> final_effort{91, 0, 0, 0, 0, 9};
    constexpr std::array<uint8_t, 6> final_offense{4, 0, 0, 0, 0, 7};
    constexpr std::array<uint8_t, 8> final_raw{0, 0, 0, 0, 3, 3, 3, 7};
    constexpr std::array<uint8_t, 8> final_active{2, 3, 3, 0, 1, 1, 2, 4};
    for (size_t i = 0; i < 6; ++i) {
        expected.at(team + 0x26 + i) = final_effort[i];
        expected.at(team + 0x35 + i) = final_offense[i];
        // The selected stored profile is copied BEFORE the exit clamps: 100 remains.
        expected.at(profile + 12 + i) = p.effort[i];
        expected.at(profile + 24 + 12 + i) = p.offense[i];
    }
    expected.at(team + 0x2D) = 91;
    for (size_t i = 0; i < 8; ++i) {
        expected.at(0x028B + 8 * m + i) = final_raw[i];
        expected.at(0x025B + 8 * m + i) = final_active[i];
    }
    require(!commit_tactics(h, p, manager), "no lineup transition in numeric exit");
    require(h.state == expected, "exit full raw golden, including preserved unrelated bytes");
    require(h.state.at(profile + 17) == 100 && h.state.at(team + 0x2B) == 9,
            "exit clamps team fifth effort addition, not saved preset");
    require(h.teams[team_id].offence[4] == 7 && h.teams[team_id].offence_base == 4,
            "exit has no invented offense clamp");
    // A separate fixed tie case proves +0x26 is reconstructed from the edited base + tie addition.
    p.effort = {80, 50, 40, 30, 20, 10};
    commit_tactics(h, p, manager);
    require(h.state.at(team + 0x26) == 100 && h.state.at(team + 0x29) == 20 &&
            h.state.at(team + 0x27) == 20 && h.state.at(team + 0x28) == 20,
            "all five effort additions clamp before reconstructing current effort");
}
// The automatic lineup runs only when the exit switches from manual to automatic; manual to
// manual, a repeated commit and automatic to manual leave the lineup alone.
void auto_lineup_transition() {
    auto h = fixture();
    h.state.at(0x078A + 2 * m) = 0;
    h.state.at(squad + 10) = 0x77; // role byte of an empty slot; cleared only if the lineup ran
    auto p = load_tactics(h, manager);
    require(!commit_tactics(h, p, manager) && h.state.at(squad + 10) == 0x77,
            "manual-to-manual preserves explicit lineup");
    require(edit_mode(p, 0, -1) && p.autoLineup, "upper selector switches manual to automatic");
    require(commit_tactics(h, p, manager) && h.state.at(squad + 10) == 0 &&
            h.state.at(0x078A + 2 * m) == 1, "manual-to-automatic invokes the automatic lineup");
    h.state.at(squad + 10) = 0x77;
    require(!commit_tactics(h, p, manager) && h.state.at(squad + 10) == 0x77,
            "repeated commit does not retrigger automatic lineup");
    require(edit_mode(p, 0, 1), "automatic to manual selector");
    require(!commit_tactics(h, p, manager) && h.state.at(squad + 10) == 0x77,
            "automatic-to-manual does not rewrite lineup");
}
} // namespace

int main() {
    try {
        normalization_goldens(); opening_preview_and_identity(); mode_and_preset_rules();
        numeric_bounds_and_copying(); fifth_addition_and_exit_goldens(); auto_lineup_transition();
        std::printf("Native tactics draft/edit/exit raw golden tests passed\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Tactics: %s\n", e.what());
        return 1;
    }
}
