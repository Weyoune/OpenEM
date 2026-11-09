// SPDX-License-Identifier: GPL-3.0-only
#include "game/game.hpp"
#include "game/youth.hpp"
#include <bit>

namespace em {
bool Game::adjust_youth_funding(int kind,int x){
    if(kind<0||kind>=3)return false;
    const auto current=youth_funding_amount(save_,kind);
    const auto next=youth_funding_target(current,finance_,kind,x);
    if(next==current)return false;
    const size_t at=0x06c1+size_t(kind)*4;
    for(int n=0;n<4;++n)save_.state.at(at+size_t(n))=uint8_t(uint32_t(next)>>(8*n));
    // This cache supplies monthly_expenses; the individual grants remain native HOK fields.
    uint32_t total=0;
    for(int row=0;row<3;++row)total+=uint32_t(youth_funding_amount(save_,row));
    mgr_[0].budget.open_a07e=std::bit_cast<int32_t>(total);
    return true;
}
} // namespace em
