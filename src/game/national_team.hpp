// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/contracts.hpp"

namespace em {

struct NationalTeamTables {
    std::array<int, 3> places{};  // DS:4EDE, goalkeeper / defence / forward
};
NationalTeamTables load_national_team_tables(const Bytes& exe);
// DS:029C is the grant used for the next trainer appointment. It is absent from
// HOK; recover it from the nation's persisted tournament history (initially 500000 DM).
long national_team_grant(const HokFile& h);

// The native national squad occupies manager block 5. Public player arguments are
// indices into HokFile::players; the serialized squad keeps the original EXE ids.
bool call_up_national_player(HokFile& h, size_t player, const ContractTables& contracts, BorlandRandom& rng);
bool release_national_player(HokFile& h, size_t player);
void release_national_squad(HokFile& h);
// ovl09_26dc: goalkeeper, three lines (2 defence/3 forwards), then six reserves.
void auto_national_lineup(HokFile& h);
void auto_national_squad(HokFile& h, const NationalTeamTables& tables,
                         const ContractTables& contracts, BorlandRandom& rng);

}  // namespace em
