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
};

bool is_game_checkpoint(const Bytes& data);
uint64_t game_data_fingerprint(const Bytes& exe);
GameCheckpoint decode_game_checkpoint(const Bytes& data);
Bytes encode_game_checkpoint(const GameCheckpoint& checkpoint);
// Accept either a native HOK or a verified OpenEM checkpoint bound to this EXE.
Bytes game_save_hok(const Bytes& exe, const Bytes& data);

}  // namespace em
