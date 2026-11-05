// SPDX-License-Identifier: GPL-3.0-only
#include "game/game.hpp"

namespace em {
namespace {
constexpr size_t manager=0x0dcd;
void put32(Bytes& b,size_t at,uint32_t n){for(int i=0;i<4;++i)b.at(at+size_t(i))=uint8_t(n>>(8*i));}
}
bool Game::set_ticket_price(int price){
    if(price<10||price>30||price==budget_.ticket_price)return false;
    budget_.ticket_price=price;save_.state.at(manager+0xe4)=uint8_t(price);return true;
}
StadiumBuildResult Game::begin_stadium_build(int item,int target,StadiumBuildDraft& draft){
    if(item<0||item>=11)return StadiumBuildResult::Invalid;
    if(stadium_refusals_[0][size_t(item)])return StadiumBuildResult::Blocked;
    auto h=clone_hok(save_);store_budget(h,budget_,0);
    const auto quote=stadium_target(h,stadium_tables_,item,target);
    if(!quote.available)return StadiumBuildResult::Unavailable;
    if(!quote.affordable)return StadiumBuildResult::InsufficientFunds;
    const auto& bounds=stadium_tables_.weeks[size_t(item)];
    const int weeks=brng_.range(bounds[0],bounds[1]);
    save_.state.at(manager+0x128+size_t(item))=uint8_t(weeks);
    stadium_refusals_[0][size_t(item)]=1;
    int discount=brng_.range(0,10);
    if(brng_.range(0,2)==0)discount=0;
    draft={item,quote.target,weeks,discount,quote.nominal_cost};
    return StadiumBuildResult::Prepared;
}
bool Game::finish_stadium_build(const StadiumBuildDraft& draft,bool accept){
    if(draft.item<0||draft.item>=11||draft.discount<0||draft.discount>10)return false;
    const size_t item=size_t(draft.item),week=manager+0x128+item;
    const auto& bounds=stadium_tables_.weeks[item];
    if(draft.weeks<bounds[0]||draft.weeks>bounds[1]||
       !stadium_refusals_[0][item]||save_.state.at(week)!=draft.weeks)return false;
    auto h=clone_hok(save_);store_budget(h,budget_,0);
    const auto quote=stadium_target(h,stadium_tables_,draft.item,draft.target);
    if(!quote.available||quote.nominal_cost!=draft.nominal_cost)return false;
    if(!accept){save_.state.at(week)=0;return false;}
    stadium_refusals_[0][item]=0;
    const int32_t cost=stadium_discounted_cost(draft.nominal_cost,draft.discount);
    budget_.balance=int32_t(uint32_t(budget_.balance)-uint32_t(cost));
    put32(save_.state,manager+0x18f,uint32_t(budget_.balance));
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
