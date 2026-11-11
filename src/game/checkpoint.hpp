// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// OpenEM-only envelope. Native HOK bytes remain an independent payload; no
// unused native bytes are repurposed for runtime state.
#include <array>
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"
#include "game/match.hpp"
#include "game/save_history.hpp"

namespace em {

struct CheckpointOffer {
    int player = -1, bidder = -1;
    bool listed = false;
    long amount = 0;
};

// v10: the runtime fields of one more human manager (managers 1..3; manager 0 keeps the fields of GameCheckpoint).
struct CheckpointManager {
    LineRotation rotation;
    PenaltyTimers timers;
    std::array<int, 30> injured_roles{};
    std::array<uint8_t, 30> squad_status{};
    int team_strength = 0, players = 0;
    long squad_costs = 0, pool_costs = 0;
    std::vector<CheckpointOffer> offers;
};

struct GameCheckpoint {
    Bytes hok;
    Bytes working_state;  // original raw/model synchronization phase, including inactive native bytes
    uint64_t exe_fingerprint = 0;
    uint32_t game_random = 0, borland_random = 0;
    LineRotation club_rotation, national_rotation;
    std::array<int, 30> injured_roles{};
    std::array<uint8_t, 30> squad_status{};  // model status can survive an injury with raw role zero
    int seasons = 0;
    // Derived budget inputs can be deliberately stale until the next native
    // refresh. Preserve their exact phase instead of recomputing on reload.
    int team_strength = 0, players = 0;
    long squad_costs = 0, pool_costs = 0;
    std::vector<CheckpointOffer> offers;
    // Version2: native transientCC37..42 counts, absent from HOK.
    std::array<std::array<uint8_t,2>,6> line_counts{{{2,3},{2,3},{2,3},{2,3},{2,3},{2,3}}};
    // Version3: process-local construction-refusal gates DSCC09..CC34.
    std::array<std::array<uint8_t,11>,4> stadium_refusals{};
    // Version4: native load/save identity history, absent from HOK.
    NativeSaveHistory save_history;
    // Version5: DS48FB..48FD weighted CPU-club distribution cursors.
    std::array<uint8_t,3> annual_distribution_cursors{};
    // Version6: DS:3277 (byte_33277), slot of the last scorer pick (ovl14_2349).
    uint8_t last_scorer_slot = 0;
    // Version7: the message sort/count of the next play step is still due (a game saved in the menu).
    bool message_step_pending = true;
    bool table_copies_frozen = false;  // v8: Game::table_copies_frozen_
    bool league_since_refresh = false;  // v8
    // v9: the penalty timers DS:D096 of the club manager and the national coach (role and slot outlive the match, not in HOK)
    PenaltyTimers club_timers, national_timers;
    // v10: the daily valuation factor DS:E1A4 of the last menu pass (85..115; 100 before the first), read by the
    // season-change contract talks; then managers 1..N-1 in index order (empty with one manager; a v1-9 checkpoint
    // loads into manager 0 only)
    int daily_luck = 100;
    std::vector<CheckpointManager> other_managers;
};

bool is_game_checkpoint(const Bytes& data);
uint64_t game_data_fingerprint(const Bytes& exe);
GameCheckpoint decode_game_checkpoint(const Bytes& data);
Bytes encode_game_checkpoint(const GameCheckpoint& checkpoint);
// Accept either a native HOK or a verified OpenEM checkpoint bound to this EXE.
Bytes game_save_hok(const Bytes& exe, const Bytes& data);

}  // namespace em
