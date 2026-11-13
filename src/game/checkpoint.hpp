// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// OpenEM checkpoint: an OpenEM-only save format that wraps an ordinary HOK savegame together with the runtime
// state the original game keeps only in memory (so a game saved and loaded in OpenEM continues exactly as an
// uninterrupted one would). The HOK bytes remain an independent payload that the original game could load; no
// unused HOK bytes are repurposed for runtime state.
//
// File layout (little endian):
//   0x00  "OPENEM\x1A\0" magic
//   0x08  u32 version (1..10; this build writes 10)
//   0x0C  u32 payload size (bytes after the 20-byte header)
//   0x10  u32 CRC-32 of the payload
//   0x14  payload: EM.EXE fingerprint (2 x u32), u32 length + HOK file, u32 length + working state, then the
//         fields of GameCheckpoint in declaration order (fields of later versions appended at the end).
#include <array>
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"
#include "game/match.hpp"
#include "game/save_history.hpp"

namespace em {

// A pending transfer offer for a player.
struct CheckpointOffer {
    int player = -1, bidder = -1;  // player index, bidding team index
    bool listed = false;           // the player was on the transfer list
    long amount = 0;               // offered sum
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

// Everything a checkpoint holds. Fields marked with a version exist from that version on and keep their defaults
// when an older checkpoint is loaded.
struct GameCheckpoint {
    Bytes hok;            // the HOK savegame file
    // The decrypted game state as the running game held it when saving (47 761 bytes, or empty in old
    // checkpoints). It can differ from the state inside `hok`, because exporting a HOK refreshes statistics and
    // cached values; it also carries bytes the game does not use.
    Bytes working_state;
    uint64_t exe_fingerprint = 0;  // game_data_fingerprint of the EM.EXE the game ran with
    uint32_t game_random = 0, borland_random = 0;  // states of the two random generators (game_random < 4 000 000)
    LineRotation club_rotation, national_rotation;
    std::array<int, 30> injured_roles{};     // role of each squad slot before an injury (0..255)
    std::array<uint8_t, 30> squad_status{};  // model status can survive an injury with raw role zero
    int seasons = 0;                         // completed seasons
    // Derived budget inputs can be deliberately stale until the game's next
    // refresh. Preserve their exact phase instead of recomputing on reload.
    int team_strength = 0, players = 0;
    long squad_costs = 0, pool_costs = 0;
    std::vector<CheckpointOffer> offers;     // at most 30
    // Version 2: two line counts per squad block that the original keeps only in memory (not in the HOK).
    std::array<std::array<uint8_t,2>,6> line_counts{{{2,3},{2,3},{2,3},{2,3},{2,3},{2,3}}};
    // Version 3: stadium construction refusal flags (0/1) per manager and building, kept only in memory by the original.
    std::array<std::array<uint8_t,11>,4> stadium_refusals{};
    // Version 4: the load/save identity history of the original, absent from HOK.
    NativeSaveHistory save_history;
    // Version 5: cursors of the weighted computer-club distribution (annual_distribution.hpp), each 0..16.
    std::array<uint8_t,3> annual_distribution_cursors{};
    // Version 6: squad slot of the last scorer pick (0..29).
    uint8_t last_scorer_slot = 0;
    // Version7: the message sort/count of the next play step is still due (a game saved in the menu).
    bool message_step_pending = true;
    bool table_copies_frozen = false;  // v8: Game::table_copies_frozen_
    bool league_since_refresh = false;  // v8
    // v9: the penalty timers of the club manager and the national coach (role and slot outlive the match, not in HOK)
    PenaltyTimers club_timers, national_timers;
    // v10: the daily mood factor of the last menu pass (85..115; 100 before the first), read by the
    // season-change contract talks; then managers 1..N-1 in index order (empty with one manager; a v1-9 checkpoint
    // loads into manager 0 only)
    int daily_luck = 100;
    std::vector<CheckpointManager> other_managers;
};

bool is_game_checkpoint(const Bytes& data);  // starts with the checkpoint magic
// 64-bit FNV-1a hash of EM.EXE: binds a checkpoint to the game data it was made with (not a security measure).
uint64_t game_data_fingerprint(const Bytes& exe);
// Parses and validates a checkpoint: header, size limit (1 MiB), version, payload size and CRC, and the range of
// every field. Throws FormatError on any violation or trailing data.
GameCheckpoint decode_game_checkpoint(const Bytes& data);
// Writes the current version. The result is decoded again before it is returned, so producers obey the same
// bounds as consumers (throws FormatError otherwise).
Bytes encode_game_checkpoint(const GameCheckpoint& checkpoint);
// Accept either a plain HOK or a verified OpenEM checkpoint bound to this EXE, and return the HOK file. For a
// checkpoint, the working state must exist and agree with the HOK in every squad player id and team owner, and the
// state it describes must decode cleanly; otherwise FormatError.
Bytes game_save_hok(const Bytes& exe, const Bytes& data);

}  // namespace em
