// SPDX-License-Identifier: GPL-3.0-only
#include "game/game.hpp"
#include "game/youth.hpp"
#include <bit>

// Game action of the youth screen: changing one of the active manager's three youth grants.

namespace em {
// Applies a slider click at column `x` to grant `kind` (0..2) of the active manager (rules in
// youth_funding_target). Writes the new i32 into the save state and refreshes the manager's
// cached total of all three grants. Returns false (state untouched) for an invalid kind or when
// the click does not change the grant.
bool Game::adjust_youth_funding(int kind,int x){
    const int a=active_manager_;  // the grants are per manager: 0x06C1 + manager * 0x15 (youth.cpp)
    if(kind<0||kind>=3)return false;
    const auto current=youth_funding_amount(save_,kind,a);
    const auto next=youth_funding_target(current,finance_,kind,x);
    if(next==current)return false;
    const size_t at=0x06c1+size_t(a)*0x15+size_t(kind)*4;
    for(int n=0;n<4;++n)save_.state.at(at+size_t(n))=uint8_t(uint32_t(next)>>(8*n));
    // This cached sum feeds the monthly expenses; the individual grants stay in the save state.
    uint32_t total=0;
    for(int row=0;row<3;++row)total+=uint32_t(youth_funding_amount(save_,row,a));
    mgr_[a].budget.open_a07e=std::bit_cast<int32_t>(total);
    return true;
}
} // namespace em
