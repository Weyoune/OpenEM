// SPDX-License-Identifier: GPL-3.0-only
#include "game/game.hpp"
#include "game/manager_layout.hpp"
#include "game/native_roster.hpp"
#include <algorithm>

namespace em {
namespace {
constexpr size_t pool=0x81CD,record=52,players=0x4FC6;
// pool +0x1A: bit 7 and the manager's bit mark a refused offer (bit 0 = manager 0; the bit of managers 1.. unverified)
uint8_t manager_bit(int manager){return uint8_t(1u<<unsigned(manager));}
void put32(Bytes& bytes,size_t at,uint32_t value){for(int n=0;n<4;n++)bytes.at(at+size_t(n))=uint8_t(value>>(8*n));}
bool valid(const HokFile& h,const PurchaseDraft& draft,int team){
    if(draft.manager<0||draft.manager>=kMaxManagers)return false;
    if(draft.pool_slot<0||draft.pool_slot>=11||draft.club_slot<0||draft.club_slot>=29||
       draft.player_id<1||draft.player_id>kHokPlayers||draft.old_team<0||draft.old_team>255||draft.fee<=0)return false;
    const size_t p=players+size_t(draft.player_id-1)*33;
    return h.state.at(pool+size_t(draft.pool_slot)*record+15)==draft.player_id&&
           h.state.at(squad_block(draft.manager)+size_t(draft.club_slot)*record+15)==draft.player_id&&
           h.state.at(p+0x1A)==4&&h.state.at(p+0x1D)==team;
}
void clear_notification(HokFile& h,int id){
    const size_t p=players+size_t(id-1)*33;const int index=h.state.at(p+0x1E);
    if(index>0&&index<25){
        std::fill_n(h.state.begin()+long(0xAEB7+size_t(index-1)*113),3,uint8_t(0));h.state.at(p+0x1E)=0;
    }
}
}
// The purchase acts for the active manager (docs/MEHRSPIELER_PLAN.md step 5); the draft remembers him.
PurchaseResult Game::begin_transfer_purchase(size_t slot,long fee,PurchaseDraft& draft){
    draft={};
    const int a=active_manager_;
    if(slot>=11||fee<=0)return PurchaseResult::Invalid;
    if(national_coach_>=0||mgr_[a].team>=52)return PurchaseResult::Unavailable;
    const size_t source=pool+slot*record;const int id=save_.state.at(source+15);
    HokPlayer* player=id?player_by_exe_index(id):nullptr;
    if(!player||player->raw[0x1A]!=4)return PurchaseResult::Unavailable;
    if(save_.state.at(source+0x1A)&manager_bit(a))return PurchaseResult::Blocked;
    if(!seller_accepts(int32_t(le32(save_.state,source+0x2C)),fee,brng_)){
        save_.state.at(source+0x1A)|=uint8_t(0x80|manager_bit(a));return PurchaseResult::SellerRefused;
    }
    if(mgr_[a].budget.balance<fee)return PurchaseResult::InsufficientFunds;
    HokFile prepared=decode_hok(save_bytes());
    BorlandRandom next=brng_;
    const int old_team=player->raw[0x1D];
    // native_insert_cpu_purchase for manager a: (playerId, years 1, loan 0, supplied salary 0, max slot 29)
    const int inserted=native_insert_player(prepared,contracts_,[&](int lo,int hi){return next.range(lo,hi);},uint8_t(id),a,1).slot;
    if(inserted>=29)return PurchaseResult::SquadFull; // scratch29 is not a safe ordinary purchase slot
    const size_t squad=squad_block(a);
    const size_t destination=squad+size_t(inserted)*record;
    const bool shifted=save_.state.at(destination+15)!=0;
    put32(prepared.state,destination+0x2C,(le32(prepared.state,destination+0x2C)&0x80000000u)|uint32_t(fee));
    std::copy_n(prepared.state.begin()+long(squad),30*record,save_.state.begin()+long(squad));
    save_.state.at(players+size_t(id-1)*33+0x1D)=uint8_t(mgr_[a].team);
    player->team=mgr_[a].team;
    auto& injured_role=mgr_[a].injured_role;
    if(shifted)std::move_backward(injured_role.begin()+inserted,injured_role.end()-1,injured_role.end());
    injured_role[size_t(inserted)]=0;
    save_.squad_of(a)=hok_squad(save_,a);brng_=next;
    draft={int(slot),id,inserted,old_team,fee,long(int32_t(le32(save_.state,destination+0x28)))};
    draft.manager=a;
    return PurchaseResult::Negotiating;
}
bool Game::cancel_transfer_purchase(const PurchaseDraft& draft){
    const int a=draft.manager;
    if(a<0||a>=managers_||!valid(save_,draft,mgr_[a].team))return false;
    native_remove_player(save_,a,draft.club_slot);
    const size_t player=players+size_t(draft.player_id-1)*33;
    save_.state.at(player+0x1D)=uint8_t(draft.old_team);
    player_by_exe_index(draft.player_id)->team=draft.old_team==255?-1:draft.old_team;
    save_.state.at(pool+size_t(draft.pool_slot)*record+0x1A)|=uint8_t(0x80|manager_bit(a));
    auto& injured_role=mgr_[a].injured_role;
    std::move(injured_role.begin()+draft.club_slot+1,injured_role.end(),injured_role.begin()+draft.club_slot);
    injured_role.back()=0;save_.squad_of(a)=hok_squad(save_,a);
    return true;
}
PurchaseResult Game::finish_transfer_purchase(const PurchaseDraft& draft,int years,long salary,bool accept_subsidy,bool prepare_lineup){
    const int a=draft.manager;
    if(a<0||a>=managers_||!valid(save_,draft,mgr_[a].team)||years<1||salary<=0)return PurchaseResult::Invalid;
    if(years>4){(void)brng_.range(10,18);cancel_transfer_purchase(draft);return PurchaseResult::PlayerRefused;}
    const size_t destination=squad_block(a)+size_t(draft.club_slot)*record;
    const auto contract=contract_player(save_,a,draft.club_slot);
    const bool raise=years==1&&salary>=draft.initial_salary;
    if(!raise&&!player_accepts(contracts_,contract,years,salary,matchday_[size_t(std::clamp(mgr_[a].budget.league,0,2))],44,day_+1,brng_)){
        (void)brng_.range(10,18);cancel_transfer_purchase(draft);return PurchaseResult::PlayerRefused;
    }
    save_.state.at(destination+0x0B)=uint8_t(years);put32(save_.state,destination+0x28,uint32_t(salary));
    HokPlayer* player=player_by_exe_index(draft.player_id);
    clear_notification(save_,draft.player_id);remove_squad_record(4,draft.pool_slot,10);
    save_.state.at(players+size_t(draft.player_id-1)*33+0x1A)=uint8_t(a);
    save_.squad_of(a)=hok_squad(save_,a);mgr_[a].budget.balance-=draft.fee;
    log_.push_back(player->name+" bought for "+std::to_string(draft.fee/3)+" DM, "+std::to_string(years)+" years at "+std::to_string(salary/3)+" DM");
    const long subsidy=purchase_subsidy(draft.fee,brng_);last_purchase_subsidy_=subsidy;
    if(subsidy>0){if(accept_subsidy)mgr_[a].budget.balance=long(int32_t(uint32_t(mgr_[a].budget.balance)+uint32_t(subsidy)));log_.push_back("purchase sponsor subsidy "+std::to_string(subsidy/3)+(accept_subsidy?" DM accepted":" DM declined"));}
    if(prepare_lineup)prepare_human_lineup(false,a);
    refresh_budget_roster(a);return PurchaseResult::Purchased;
}
} // namespace em
