// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// National team mode: the squad of the national coach (squad block 5 of the save state), calling
// players up and releasing them back to their clubs, the automatic squad and lineup, and the grant
// the federation pays the coach.
#include "game/contracts.hpp"

namespace em {

// Squad size per position kind, read from EM.EXE.
struct NationalTeamTables {
    std::array<int, 3> places{};  // goalkeeper / defence / forward; 3, 10 and 16 in V1.00
};
// Reads the squad sizes from EM.EXE; throws FormatError if they are not {3, 10, 16}.
NationalTeamTables load_national_team_tables(const Bytes& exe);
// The grant the federation pays for the next coach appointment, in the internal money unit (DM x 3,
// so 1500000 = 500000 DM). The original keeps it in a variable that is not part of the HOK file,
// so it is reconstructed from the nation's persisted world championship placements (state
// 0x3BD9 + nation * 0x50): the most recent placement p gives 900000 + (8 - p) * 300000 for p <= 8,
// otherwise 300000; no placement yet (or no national team selected) gives the initial 1500000.
long national_team_grant(const HokFile& h);

// The national squad occupies squad block 5 (29 usable slots, sorted goalkeepers, defenders,
// forwards). Public player arguments are indices into HokFile::players; the serialized squad
// keeps the 1-based player IDs of the save.
//
// Calls a player up: refused (false) for foreigners, IDs >= 180, players already at the national
// team, when no nation is coached or the squad is full. The new record is inserted at the end of
// the player's position group and gets random exhaustion (80..120), experience (35..65) and trait
// nibbles, one contract year, the player's goals/games/skills/form, and a salary (valued at the
// club, then scaled by 90..115 %). The old club is remembered as +0x0C = club + 128, and the
// player's team (player record +0x1D) becomes the nation.
bool call_up_national_player(HokFile& h, size_t player, const ContractTables& contracts, BorlandRandom& rng);
// Removes the player from the national squad and returns him to the club stored in +0x0C;
// false when he is not in the squad.
bool release_national_player(HokFile& h, size_t player);
// Releases every player of the national squad; throws FormatError if a squad ID has no player.
void release_national_squad(HokFile& h);
// Automatic lineup of the national squad: goalkeeper, three lines (2 defence/3 forwards), then six
// reserves (the general auto_lineup on block 5).
void auto_national_lineup(HokFile& h);
// Rebuilds the squad automatically: releases everyone, then for each position kind calls up the
// strongest eligible players (skill 1 + skill 2 + form, first in player order on a tie) until the
// table's quota is filled, and finally sets the automatic lineup.
void auto_national_squad(HokFile& h, const NationalTeamTables& tables,
                         const ContractTables& contracts, BorlandRandom& rng);

}  // namespace em
