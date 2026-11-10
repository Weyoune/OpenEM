// SPDX-License-Identifier: GPL-3.0-only
#include "game/game.hpp"
#include "game/manager_layout.hpp"

namespace em {
namespace {
void put32(Bytes& b,size_t at,uint32_t n){for(int i=0;i<4;++i)b.at(at+size_t(i))=uint8_t(n>>(8*i));}
}
// The stadium actions act for the active manager (docs/MEHRSPIELER_PLAN.md step 5).
bool Game::set_ticket_price(int price){
    const int a=active_manager_;const size_t manager=manager_block(a);
    if(price<10||price>30||price==mgr_[a].budget.ticket_price)return false;
    mgr_[a].budget.ticket_price=price;save_.state.at(manager+0xe4)=uint8_t(price);return true;
}
StadiumBuildResult Game::begin_stadium_build(int item,int target,StadiumBuildDraft& draft){
    const int a=active_manager_;const size_t manager=manager_block(a);
    if(item<0||item>=11)return StadiumBuildResult::Invalid;
    if(stadium_refusals_[size_t(a)][size_t(item)])return StadiumBuildResult::Blocked;
    auto h=clone_hok(save_);store_budget(h,mgr_[a].budget,a);
    const auto quote=stadium_target(h,stadium_tables_,item,target,a);
    if(!quote.available)return StadiumBuildResult::Unavailable;
    if(!quote.affordable)return StadiumBuildResult::InsufficientFunds;
    const auto& bounds=stadium_tables_.weeks[size_t(item)];
    const int weeks=brng_.range(bounds[0],bounds[1]);
    save_.state.at(manager+0x128+size_t(item))=uint8_t(weeks);
    stadium_refusals_[size_t(a)][size_t(item)]=1;
    int discount=brng_.range(0,10);
    if(brng_.range(0,2)==0)discount=0;
    draft={item,quote.target,weeks,discount,quote.nominal_cost};
    return StadiumBuildResult::Prepared;
}
bool Game::finish_stadium_build(const StadiumBuildDraft& draft,bool accept){
    const int a=active_manager_;const size_t manager=manager_block(a);
    if(draft.item<0||draft.item>=11||draft.discount<0||draft.discount>10)return false;
    const size_t item=size_t(draft.item),week=manager+0x128+item;
    const auto& bounds=stadium_tables_.weeks[item];
    if(draft.weeks<bounds[0]||draft.weeks>bounds[1]||
       !stadium_refusals_[size_t(a)][item]||save_.state.at(week)!=draft.weeks)return false;
    auto h=clone_hok(save_);store_budget(h,mgr_[a].budget,a);
    const auto quote=stadium_target(h,stadium_tables_,draft.item,draft.target,a);
    if(!quote.available||quote.nominal_cost!=draft.nominal_cost)return false;
    if(!accept){save_.state.at(week)=0;return false;}
    stadium_refusals_[size_t(a)][item]=0;
    const int32_t cost=stadium_discounted_cost(draft.nominal_cost,draft.discount);
    mgr_[a].budget.balance=int32_t(uint32_t(mgr_[a].budget.balance)-uint32_t(cost));
    put32(save_.state,manager+0x18f,uint32_t(mgr_[a].budget.balance));
    save_.state.at(week)=uint8_t(draft.weeks+brng_.range(0,4)-2);
    if(item<9)save_.state.at(manager+0x117+item*2)=uint8_t(draft.target);
    else{
        const size_t at=manager+0x135+(item-9)*4;
        save_.state.at(at)=uint8_t(quote.capacity);
        save_.state.at(at+1)=uint8_t(unsigned(quote.capacity)>>8);
    }
    return true;
}
} // namespace em
