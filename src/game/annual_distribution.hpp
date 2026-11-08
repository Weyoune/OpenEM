// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/contracts.hpp"

namespace em {
using DistributionCursors=std::array<uint8_t,3>;
// 1238:1838 (Amiga 0x1372E8) annual mode0: CPU club0..42, widening quality
// tolerance; the distance is sub_24D51 = labs(quality - mean).
int annual_cpu_target(const Bytes&,uint8_t player,const ContractRandomDraw&);
// 146E:1A11: ranking argument is unused; shared17-entry weighted cycle.
int annual_cyclic_target(const Bytes&,int league,DistributionCursors&,const ContractRandomDraw&);
// 1238:15A0: quotas after random changes, with a fresh abroad count.
std::array<int,4> annual_distribution_quotas(const Bytes&);
// 1238:14AB..159D (Amiga 0x13620A after its skill loop): the season-change
// player shift inside 1238:13A1, right after the398 global skill draws of
// annual_global_skills. Borland rand(5,15) changes; while more than four IDs
// 1..179 play abroad (club 64..254), each drawn foreign player moves to a CPU
// club without counting as a change; otherwise any drawn player of an
// unmanaged club (or without club) moves. Targets come from 1238:1838.
void annual_random_club_changes(Bytes&,const ContractRandomDraw&);
// 146E:1807..1A10, using the quotas computed after random club changes.
void annual_rebalance_clubs(Bytes&,std::array<int,4>,DistributionCursors&,const ContractRandomDraw&);
// Raw bytes only. The caller supplies current team ratings/manager leagues,
// synchronizes global club DTOs, and preserves the cursors across live loads.
void annual_distribute_players(Bytes&,DistributionCursors&,const ContractRandomDraw&);
} // namespace em
