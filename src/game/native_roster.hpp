// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Squad roster operations on the raw save state: counting a manager's players (including those
// he owns in other blocks), finding a player in the squad blocks, inserting a newly signed player
// into a club squad and removing rows again. These work on HokFile::state only; keeping the decoded
// models (HokFile::players, injury backups) in sync is the caller's job.
//
// Squad blocks: 6 x 0x618 bytes from state 0x696D, 30 records of 0x34 bytes each (blocks 0..3 the
// human clubs, 4 the pool of 12 slots, 5 the national squad); record +0x0F is the 1-based player ID
// (0 = free). Slot 29 is a scratch slot that is not counted as a regular place.

#include "game/contracts.hpp"
#include <optional>

namespace em {
// Result of native_insert_player: the slot used (0..29), or 127 when the squad is full.
struct NativeRosterInsertion {
    int slot = 127;
    bool shifted = false;  // true when later rows were moved down one place to make room
};
// The number of places `manager` (0..3) has taken: the occupied regular slots of his own block plus
// the players he owns (player record +0x1A) in the other active blocks. Quirk kept from the
// original: in the other blocks only the first <occupied count> rows are examined, so a block with
// holes can hide owned players behind the hole.
int native_reserved_players(const HokFile&,int manager);
// Block (0..5) and slot of a player found by native_find_roster_player.
struct NativeRosterLocation { int manager=0,slot=0; };
// The first row holding `player_id` (1..199), scanning blocks 0..5 in order (inactive human blocks
// are skipped) and in each only the first <occupied count> rows; nullopt when not found.
std::optional<NativeRosterLocation> native_find_roster_player(const HokFile&,uint8_t player_id);
// Deletes the message linked to the player (player record +0x1E) and clears the link.
void native_clear_player_notification(HokFile&,uint8_t player_id);
// Removes the row `slot` of club block `manager` (0..3); see native_remove_storage_row.
void native_remove_player(HokFile&,int manager,int slot);
// Removes the row `slot` of block `storage` (0..5) with `last_slot` as the block's last row: if the
// row is occupied, the player's message is deleted, rows slot+1..last_slot move up one place and
// only the player ID of the last row is cleared (its other bytes remain). Empty rows are left alone.
void native_remove_storage_row(HokFile&,int storage,int slot,int last_slot);
// Inserts player `player_id` into club block `manager` (0..3) with a contract of `years` years.
// Refused (slot 127, no state change, no draws) when the player is not already at the buyer's club
// and the manager has more than 28 reserved places. The new row goes before the first free row or
// the first player of a higher position class (position value / 25, at most 2), or into slot 29;
// an occupied target row and the rows after it are shifted down.
// The fresh 52-byte record gets five random draws in this order: exhaustion 80..120, experience
// 35..65, then trend 1..13, direction 1..3 and a high bit 0..1 packed into +0x14. Goals, games,
// skills and form are copied from the player record, the salary is valued while the player still
// belongs to his old team, and only then is the player's team (+0x1D) set to the buyer's club.
// The fee, cash and owner are not touched.
NativeRosterInsertion native_insert_player(HokFile&,const ContractTables&,const ContractRandomDraw&,
                                           uint8_t player_id,int manager,int years);

// A normal computer-side purchase into club block 0 with a one-year contract: native_insert_player
// with draws from `rng`. Returns the slot 0..29, or 127 on the capacity refusal. `playerId` is the
// 1-based player ID, 1..199.
int native_insert_cpu_purchase(HokFile& h, const ContractTables& tables,
                               BorlandRandom& rng, uint8_t playerId);

// Removes row `slot` of club block 0 (native_remove_player with manager 0). No random draws. This
// does not restore the player's old team or set the pool retry bits; the negotiation code does that.
void native_remove_purchase_row(HokFile& h, int slot);

} // namespace em
