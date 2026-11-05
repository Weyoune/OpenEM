// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "game/contracts.hpp"
#include <optional>

namespace em {
struct NativeRosterInsertion { int slot=127;bool shifted=false; };
// 5396:331B reserved count, including own players reserved in other blocks.
int native_reserved_players(const HokFile&,int manager);
struct NativeRosterLocation { int manager=0,slot=0; };
// 645B:15C6, first ID match in occupied-count prefixes of active blocks0..5.
std::optional<NativeRosterLocation> native_find_roster_player(const HokFile&,uint8_t player_id);
void native_clear_player_notification(HokFile&,uint8_t player_id);
void native_remove_player(HokFile&,int manager,int slot);
void native_remove_storage_row(HokFile&,int storage,int slot,int last_slot);
// Shared normal59B5:2CD1 branch: loan0, suppliedSalary0, maxSlot29.
// Five profile draws, full salary valuation, then the global club is changed.
NativeRosterInsertion native_insert_player(HokFile&,const ContractTables&,const ContractRandomDraw&,
                                           uint8_t player_id,int manager,int years);

// Raw-state specialization of 59B5:2CD1 for a normal CPU purchase by manager0:
// (playerId, years1, loan0, suppliedSalary0, maxSlot29). Returns0..29 or127
// on the native capacity refusal. A refusal neither mutates state nor draws RNG.
// Successful insertion consumes five draws, initializes a fresh52-byte record,
// values its salary using the old team, then moves globalPlayer+1D to manager0's
// club. It does not copy the pool record, set the fee, debit cash, or change owner.
// playerId is the native1-based ID,1..199. Uses only HokFile::state; decoded
// models and injury backups remain the caller's responsibility.
int native_insert_cpu_purchase(HokFile& h, const ContractTables& tables,
                               BorlandRandom& rng, uint8_t playerId);

// Raw-state 59B5:25D6(slot, force1, maxSlot29), manager0. If occupied, clears
// the player's notification0180, moves rows(slot+1..29) upward and zeros only
// scratch29's ID. Empty rows are untouched. No RNG. This does not restore the
// old global team or set the pool retry bits: the negotiation caller does that.
void native_remove_purchase_row(HokFile& h, int slot);

} // namespace em
