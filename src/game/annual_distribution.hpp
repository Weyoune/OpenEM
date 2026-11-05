// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/contracts.hpp"

namespace em {
using DistributionCursors=std::array<uint8_t,3>;
// 1238:1838 annual mode0: CPU club0..42, widening quality tolerance.
int annual_cpu_target(const Bytes&,uint8_t player,const ContractRandomDraw&);
// 146E:1A11: ranking argument is unused; shared17-entry weighted cycle.
int annual_cyclic_target(const Bytes&,int league,DistributionCursors&,const ContractRandomDraw&);
// 1238:15A0: quotas after random changes, with a fresh abroad count.
std::array<int,4> annual_distribution_quotas(const Bytes&);
// 1238:14AB..159D, called after the398 global skill draws.
void annual_random_club_changes(Bytes&,const ContractRandomDraw&);
// 146E:1807..1A10, using the quotas computed after random club changes.
void annual_rebalance_clubs(Bytes&,std::array<int,4>,DistributionCursors&,const ContractRandomDraw&);
// Raw bytes only. The caller supplies current team ratings/manager leagues,
// synchronizes global club DTOs, and preserves the cursors across live loads.
void annual_distribute_players(Bytes&,DistributionCursors&,const ContractRandomDraw&);
} // namespace em
