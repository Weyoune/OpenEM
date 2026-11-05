// SPDX-License-Identifier: GPL-3.0-only
#include "game/game.hpp"
#include "game/native_roster.hpp"
#include <algorithm>

namespace em {
namespace {
constexpr size_t squad=0x696D,pool=0x81CD,record=52,players=0x4FC6;
void put32(Bytes& bytes,size_t at,uint32_t value){for(int n=0;n<4;n++)bytes.at(at+size_t(n))=uint8_t(value>>(8*n));}
bool valid(const HokFile& h,const PurchaseDraft& draft,int team){
    if(draft.pool_slot<0||draft.pool_slot>=11||draft.club_slot<0||draft.club_slot>=29||
       draft.player_id<1||draft.player_id>kHokPlayers||draft.old_team<0||draft.old_team>255||draft.fee<=0)return false;
    const size_t p=players+size_t(draft.player_id-1)*33;
    return h.state.at(pool+size_t(draft.pool_slot)*record+15)==draft.player_id&&
           h.state.at(squad+size_t(draft.club_slot)*record+15)==draft.player_id&&
           h.state.at(p+0x1A)==4&&h.state.at(p+0x1D)==team;
}
void clear_notification(HokFile& h,int id){
    const size_t p=players+size_t(id-1)*33;const int index=h.state.at(p+0x1E);
    if(index>0&&index<25){
        std::fill_n(h.state.begin()+long(0xAEB7+size_t(index-1)*113),3,uint8_t(0));h.state.at(p+0x1E)=0;
    }
}
}
PurchaseResult Game::begin_transfer_purchase(size_t slot,long fee,PurchaseDraft& draft){
    draft={};
    if(slot>=11||fee<=0)return PurchaseResult::Invalid;
    if(national_coach_>=0||human_team_>=52)return PurchaseResult::Unavailable;
    const size_t source=pool+slot*record;const int id=save_.state.at(source+15);
    HokPlayer* player=id?player_by_exe_index(id):nullptr;
    if(!player||player->raw[0x1A]!=4)return PurchaseResult::Unavailable;
    if(save_.state.at(source+0x1A)&1)return PurchaseResult::Blocked;
    if(!seller_accepts(int32_t(le32(save_.state,source+0x2C)),fee,brng_)){
        save_.state.at(source+0x1A)|=0x81;return PurchaseResult::SellerRefused;
    }
    if(budget_.balance<fee)return PurchaseResult::InsufficientFunds;
    HokFile prepared=decode_hok(save_bytes());
    BorlandRandom next=brng_;
    const int old_team=player->raw[0x1D];
    const int inserted=native_insert_cpu_purchase(prepared,contracts_,next,uint8_t(id));
    if(inserted>=29)return PurchaseResult::SquadFull; // scratch29 is not a safe ordinary purchase slot
    const size_t destination=squad+size_t(inserted)*record;
    const bool shifted=save_.state.at(destination+15)!=0;
    put32(prepared.state,destination+0x2C,(le32(prepared.state,destination+0x2C)&0x80000000u)|uint32_t(fee));
    std::copy_n(prepared.state.begin()+squad,30*record,save_.state.begin()+squad);
    save_.state.at(players+size_t(id-1)*33+0x1D)=uint8_t(human_team_);
    player->team=human_team_;
    if(shifted)std::move_backward(injured_role_.begin()+inserted,injured_role_.end()-1,injured_role_.end());
    injured_role_[size_t(inserted)]=0;
    save_.squad=hok_squad(save_,0);brng_=next;
    draft={int(slot),id,inserted,old_team,fee,long(int32_t(le32(save_.state,destination+0x28)))};
    return PurchaseResult::Negotiating;
}
bool Game::cancel_transfer_purchase(const PurchaseDraft& draft){
    if(!valid(save_,draft,human_team_))return false;
    native_remove_purchase_row(save_,draft.club_slot);
    const size_t player=players+size_t(draft.player_id-1)*33;
    save_.state.at(player+0x1D)=uint8_t(draft.old_team);
    player_by_exe_index(draft.player_id)->team=draft.old_team==255?-1:draft.old_team;
    save_.state.at(pool+size_t(draft.pool_slot)*record+0x1A)|=0x81;
    std::move(injured_role_.begin()+draft.club_slot+1,injured_role_.end(),injured_role_.begin()+draft.club_slot);
    injured_role_.back()=0;save_.squad=hok_squad(save_,0);
    return true;
}
PurchaseResult Game::finish_transfer_purchase(const PurchaseDraft& draft,int years,long salary,bool accept_subsidy,bool prepare_lineup){
    if(!valid(save_,draft,human_team_)||years<1||salary<=0)return PurchaseResult::Invalid;
    if(years>4){(void)brng_.range(10,18);cancel_transfer_purchase(draft);return PurchaseResult::PlayerRefused;}
    const size_t destination=squad+size_t(draft.club_slot)*record;
    const auto contract=contract_player(save_,0,draft.club_slot);
    const bool raise=years==1&&salary>=draft.initial_salary;
    if(!raise&&!player_accepts(contracts_,contract,years,salary,matchday_[size_t(std::clamp(budget_.league,0,2))],44,day_+1,brng_)){
        (void)brng_.range(10,18);cancel_transfer_purchase(draft);return PurchaseResult::PlayerRefused;
    }
    save_.state.at(destination+0x0B)=uint8_t(years);put32(save_.state,destination+0x28,uint32_t(salary));
    HokPlayer* player=player_by_exe_index(draft.player_id);
    clear_notification(save_,draft.player_id);remove_squad_record(4,draft.pool_slot,10);
    save_.state.at(players+size_t(draft.player_id-1)*33+0x1A)=0;
    save_.squad=hok_squad(save_,0);budget_.balance-=draft.fee;
    log_.push_back(player->name+" bought for "+std::to_string(draft.fee/3)+" DM, "+std::to_string(years)+" years at "+std::to_string(salary/3)+" DM");
    const long subsidy=purchase_subsidy(draft.fee,brng_);
    if(subsidy>0){if(accept_subsidy)budget_.balance=long(int32_t(uint32_t(budget_.balance)+uint32_t(subsidy)));log_.push_back("purchase sponsor subsidy "+std::to_string(subsidy/3)+(accept_subsidy?" DM accepted":" DM declined"));}
    if(prepare_lineup)prepare_human_lineup(false);
    refresh_budget_roster();return PurchaseResult::Purchased;
}
} // namespace em
