// SPDX-License-Identifier: GPL-3.0-only
#include "game/tactics.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace em;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
constexpr int manager = 1, team_id = 2;
constexpr size_t m = manager, team = 0x17E7 + team_id * 61;
constexpr size_t profile = 0xB96B + 48 * m, squad = 0x696D + 0x618 * m;

// Synthetic independent raw fixture, with manager1 owning team2. No EXE,
// runtime RNG, native installation or UI is needed by these tests.
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
    h.state.at(team + 0x26) = 75; // saved current=base+tie, not editablebase
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
void normalization_goldens() {
    // 2F2D..2F75 uses OR, not ceil:3/2|1 stays1.
    require(normalize_tactics_quotas({0, 0, 0, 0}) == std::array<uint8_t, 4>{2, 3, 3, 0},
            "allzero quotas redistribute backwards and avoid fourth line");
    require(normalize_tactics_quotas({3, 3, 3, 7}) == std::array<uint8_t, 4>{1, 1, 2, 4},
            "odd raw quotas use bitwise OR rather than ceiling");
    require(normalize_tactics_quotas({20, 0, 0, 0}) == std::array<uint8_t, 4>{8, 0, 0, 0},
            "oversubscribed active tuple decrements eligible entries");
    require(normalize_tactics_quotas({4, 6, 6, 0}) == std::array<uint8_t, 4>{2, 3, 3, 0},
            "native saved four-six-six-zero tuple");
}
void opening_preview_and_identity() {
    auto h = fixture();
    const Bytes original = h.state;
    const auto p = load_tactics(h, manager);
    require(p.autoLineup && p.preset == 2, "saved lineup convention and preset selection");
    require(p.effort == std::array<uint8_t, 6>{60, 20, 10, 15, 10, 5},
            "opening loads base2D rather than current26");
    require(p.profiles[3].effort == std::array<uint8_t, 6>{43, 4, 2, 3, 4, 5},
            "fourth effort preset and manager stride");
    require(p.profiles[1].offense == std::array<uint8_t, 6>{1, 1, 2, 3, 4, 5},
            "offense presets follow all24 effort bytes");
    auto preview = preview_tactics(h, p, manager);
    require(h.state == original, "opening and preview never change source HOK");
    require(preview.state.at(team + 0x26) == 60 && preview.teams[team_id].effort_current == 60,
            "preview contains native temporary base without exit normalization");
    require(preview.teams[team_id].raw == preview.state.data() + team &&
            preview.teams[team_id].raw != h.teams[team_id].raw, "preview deeply rebases record pointers");
    require(!commit_tactics(h, p, manager), "automatic-to-automatic does not rerun lineup");
    require(h.state == original, "unchanged canonical dialog has byte-identical exit");
    require(h.teams[team_id].effort_base == 60 && h.teams[team_id].effort_current == 75,
            "commit synchronizes decoded team fields");
}
void mode_and_preset_rules() {
    auto h = fixture(); auto p = load_tactics(h, manager);
    require(!edit_mode(p, 0, -1) && p.autoLineup, "lineup upper bound does not wrap");
    require(edit_mode(p, 0, 1) && !p.autoLineup, "lower lineup arrow selects manual");
    require(!edit_mode(p, 0, 1), "lineup lower bound does not wrap");
    const auto old_profile = p.profiles[2].effort;
    require(!edit_numeric(p, 12, 1) && p.profiles[2].effort == old_profile,
            "AUTOM effort numeric is disabled and does not copy presets");
    require(edit_mode(p, 1, 1) && p.lineMode == 1 && p.profiles[2].effort == p.effort &&
            p.profiles[2].offense == p.offense, "entering line mode1 copies BOTH current profiles");
    require(edit_mode(p, 2, 1) && p.effortMode == 1, "effort enters SEMI");
    require(edit_mode(p, 2, 1) && p.effortMode == 2 && !edit_numeric(p, 12, 1),
            "manual effort mode disables profile numeric controls");
    require(!edit_mode(p, 2, 1) && edit_mode(p, 2, -1), "component modes stop at2 and return to SEMI");
    require(select_preset(p, 1), "select another stored tactic");
    require(p.effort == std::array<uint8_t, 6>{41, 2, 2, 3, 4, 5} &&
            p.offense == std::array<uint8_t, 6>{1, 1, 2, 3, 4, 5}, "preset loads exactly6+6 bytes");
    require(p.lineMode == 1 && p.effortMode == 1 && p.offenseMode == 0,
            "preset selection never changes control modes");
    p.effort[0] = 99;
    require(!select_preset(p, 1) && p.effort[0] == 99, "clicking selected preset does not reload it");
}
void numeric_bounds_and_copying() {
    auto h = fixture(); auto p = load_tactics(h, manager);
    p.effortMode = 1;
    p.effort = {80, 20, 5, 0, 0, 0};
    require(edit_numeric(p, 12, 1) && p.effort[0] == 80 && p.profiles[2].effort == p.effort,
            "base upper bound still performs selected preset copy");
    p.effort = {60, 40, 10, 15, 10, 5};
    require(edit_numeric(p, 14, 1) && p.effort[1] == 40, "addition capped by100-base");
    p.effort[1] = 0;
    require(edit_numeric(p, 14, -1) && p.effort[1] == 0, "addition lower bound0");
    p.offenseMode = 1; p.offense = {2, 1, 5, 0, 0, 0};
    require(edit_numeric(p, 13, 1) && p.offense[0] == 2, "offense base bounded by7-largest first4 addition");
    p.lineMode = 3;
    require(!edit_numeric(p, 10, 1), "quota edit disabled outside minutes mode4");
    p.lineMode = 4;
    for (int i = 0; i < 5; ++i) require(edit_numeric(p, 10, 1), "eligible fourth quota event");
    require(p.rawQuotas == std::array<uint8_t, 8>{4, 6, 6, 4, 4, 6, 6, 0},
            "defence quota sum cannot exceed20;attack tuple untouched");
    p.profiles[2].offense.fill(0);
    require(edit_numeric(p, 10, 1) && p.profiles[2].offense == p.offense,
            "quota event at maximum also copies both tactic profiles");
}
void fifth_addition_and_exit_goldens() {
    auto h = fixture(); auto p = load_tactics(h, manager);
    p.effortMode = 1; p.offenseMode = 1;
    p.effort = {90, 0, 0, 0, 0, 100};
    p.offense = {3, 0, 0, 0, 0, 7};
    require(edit_numeric(p, 12, 1) && p.effort[0] == 91,
            "native effort base bound omits fifth situation");
    require(edit_numeric(p, 13, 1) && p.offense[0] == 4,
            "native offense base bound also omits fifth situation");
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
        // The selected stored profile is copied BEFORE exit clamps:100 remains.
        expected.at(profile + 12 + i) = p.effort[i];
        expected.at(profile + 24 + 12 + i) = p.offense[i];
    }
    expected.at(team + 0x2D) = 91;
    for (size_t i = 0; i < 8; ++i) {
        expected.at(0x028B + 8 * m + i) = final_raw[i];
        expected.at(0x025B + 8 * m + i) = final_active[i];
    }
    require(!commit_tactics(h, p, manager), "no lineup transition in numeric exit");
    require(h.state == expected, "native exit full raw golden,including preserved unrelated bytes");
    require(h.state.at(profile + 17) == 100 && h.state.at(team + 0x2B) == 9,
            "exit clamps team fifth effort addition,not saved preset");
    require(h.teams[team_id].offence[4] == 7 && h.teams[team_id].offence_base == 4,
            "exit has no invented offense clamp");
    // A separate fixed tie case proves26 is reconstructed from editedbase+tie.
    p.effort = {80, 50, 40, 30, 20, 10};
    commit_tactics(h, p, manager);
    require(h.state.at(team + 0x26) == 100 && h.state.at(team + 0x29) == 20 &&
            h.state.at(team + 0x27) == 20 && h.state.at(team + 0x28) == 20,
            "all five effort additions clamp before reconstructing current effort");
}
void auto_lineup_transition() {
    auto h = fixture();
    h.state.at(0x078A + 2 * m) = 0;
    h.state.at(squad + 10) = 0x77; // empty slot flag proves whether helper ran
    auto p = load_tactics(h, manager);
    require(!commit_tactics(h, p, manager) && h.state.at(squad + 10) == 0x77,
            "manual-to-manual preserves explicit lineup");
    require(edit_mode(p, 0, -1) && p.autoLineup, "upper selector switches manual to automatic");
    require(commit_tactics(h, p, manager) && h.state.at(squad + 10) == 0 &&
            h.state.at(0x078A + 2 * m) == 1, "manual-to-automatic invokes native lineup helper");
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
