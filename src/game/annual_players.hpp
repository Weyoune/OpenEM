// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Season change, per player: the review of a human manager's own players (retirement, injured-for-good players)
// and the yearly random change of every player's skills. Works on the raw savegame state (formats/hok.hpp);
// player ids are 1-based.
#include "game/native_roster.hpp"

namespace em {
// Outcome of annual_owned_player: player not found in any squad, found in another manager's squad, stays,
// retires (contract over and flagged), or leaves because he is injured for good.
enum class AnnualPlayerStatus { Missing,OtherStorage,Stayed,Retired,Disabled };
struct AnnualPlayerReview {
    AnnualPlayerStatus status=AnnualPlayerStatus::Missing;
    int slot=-1;                 // squad slot where the player was found
    int32_t insurance_payment=0; // Disabled: the insurance payout, 60 times the monthly premium (squad +0x30, raw x3)
    int notice_back=0;  // the immediate message's date draw rand(0,3): days dated back
};
// Second review pass of the season change for player `player_id` of manager `manager` (0..3), with a fresh
// lookup of the squad slot. Age, contract years and the global statistics have already been processed.
// A player injured for good (squad +0x0D = 0xFF) leaves with an insurance payout; any other player draws an age
// threshold rand(37, 42) as the original does (the result does not matter for an existing row) and stays unless
// his contract has run out (+0x0B = 0) and flag bit 0x80 of +0x18 is set. A leaving player is removed from the
// squad and his global record is reborn as a new young player: age rand(18, 25), both skills
// rand(base - 5, base + 5) with base = rand(40, 92), byte +0x1A = 6, caps 0. Only rows of `manager` change.
AnnualPlayerReview annual_owned_player(HokFile&,uint8_t player_id,int manager,
                                       const ContractRandomDraw&);
// The late yearly skill change for all players, after the review and the summer break: for ids 1..199 the global
// skills +0x15 and then +0x16 each change by rand(1, 7) - 4 and are clamped to 30..99 (398 draws in this order).
// The same step of the original then moves players between clubs, see annual_random_club_changes in
// annual_distribution.hpp. Throws FormatError for a truncated state.
void annual_global_skills(Bytes&,const ContractRandomDraw&);
} // namespace em
