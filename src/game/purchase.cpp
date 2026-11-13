// SPDX-License-Identifier: GPL-3.0-only
// Buying a player from the transfer list (the pool, squad block 4): the seller's decision, the
// provisional squad row while the contract is negotiated, cancelling or completing the purchase,
// and bids for list players owned by another human manager. These are Game members declared in
// game/game.hpp.
#include "game/game.hpp"
#include "game/manager_layout.hpp"
#include "game/native_roster.hpp"
#include <algorithm>

namespace em {
namespace {
// State offsets: pool (squad block 4) rows, squad record size, player records (ID 1 first).
constexpr size_t pool=0x81CD,record=52,players=0x4FC6;
// Pool row +0x1A: bit 7 together with the manager's bit marks an offer that was refused to that
// manager (bit 0 = manager 0; the bits of the other managers follow by analogy).
uint8_t manager_bit(int manager){return uint8_t(1u<<unsigned(manager));}
// Stores a little-endian 32-bit value.
void put32(Bytes& bytes,size_t at,uint32_t value){for(int n=0;n<4;n++)bytes.at(at+size_t(n))=uint8_t(value>>(8*n));}
// Checks that a purchase draft still matches the state: the player sits in the given pool row and
// club row, is still owned by the seller (pool = 4 for a computer player) and plays for `team`.
bool valid(const HokFile& h,const PurchaseDraft& draft,int team){
    if(draft.manager<0||draft.manager>=kMaxManagers)return false;
    if(draft.pool_slot<0||draft.pool_slot>=11||draft.club_slot<0||draft.club_slot>=29||
       draft.player_id<1||draft.player_id>kHokPlayers||draft.old_team<0||draft.old_team>255||draft.fee<=0)return false;
    const size_t p=players+size_t(draft.player_id-1)*33;
    return h.state.at(pool+size_t(draft.pool_slot)*record+15)==draft.player_id&&
           h.state.at(squad_block(draft.manager)+size_t(draft.club_slot)*record+15)==draft.player_id&&
           h.state.at(p+0x1A)==(draft.seller<0?4:draft.seller)&&h.state.at(p+0x1D)==team;
}
// Deletes the message linked to player `id` (player record +0x1E) and clears the link.
void clear_notification(HokFile& h,int id){
    const size_t p=players+size_t(id-1)*33;const int index=h.state.at(p+0x1E);
    if(index>0&&index<25){
        std::fill_n(h.state.begin()+long(0xAEB7+size_t(index-1)*113),3,uint8_t(0));h.state.at(p+0x1E)=0;
    }
}
}
// A bid of `fee` for the computer player in pool row `slot`. The purchase acts for the active manager; the draft
// remembers him. Order of checks: national coach or no club -> Unavailable; player not a computer-owned list player ->
// Unavailable; offer already refused to this manager -> Blocked; the seller's random decision on the bid against the
// market value (pool row +0x2C) -> SellerRefused, which also sets the refusal bits; balance below the fee ->
// InsufficientFunds. Otherwise the player gets a provisional squad row (insert_purchase) and the contract talks begin.
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
    return insert_purchase(slot,fee,draft);
}
// The bought list player gets a squad row of the active manager (native_insert_player with a one-year contract),
// the bid is kept at +0x2C (its bit 31 is preserved); the contract panel follows. The player's team becomes the
// buyer's club and the manager's injured-role list is shifted like the squad rows. Returns SquadFull when only the
// scratch slot 29 was left; the random generator advances only on success.
PurchaseResult Game::insert_purchase(size_t slot,long fee,PurchaseDraft& draft){
    const int a=active_manager_;
    const size_t source=pool+slot*record;const int id=save_.state.at(source+15);
    HokPlayer* player=player_by_exe_index(id);
    HokFile prepared=decode_hok(save_bytes());
    BorlandRandom next=brng_;
    const int old_team=player->raw[0x1D];
    // the same insertion as native_insert_cpu_purchase, for manager a: one contract year, no loan, salary valued
    const int inserted=native_insert_player(prepared,contracts_,[&](int lo,int hi){return next.range(lo,hi);},uint8_t(id),a,1).slot;
    if(inserted>=29)return PurchaseResult::SquadFull; // the scratch slot 29 is not a safe ordinary purchase slot
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
// Takes a provisional purchase back: removes the squad row, returns the player to his old team and closes up the
// injured-role list. Returns false when the draft no longer matches the state.
bool Game::cancel_transfer_purchase(const PurchaseDraft& draft){
    const int a=draft.manager;
    if(a<0||a>=managers_||!valid(save_,draft,mgr_[a].team))return false;
    native_remove_player(save_,a,draft.club_slot);
    const size_t player=players+size_t(draft.player_id-1)*33;
    save_.state.at(player+0x1D)=uint8_t(draft.old_team);
    player_by_exe_index(draft.player_id)->team=draft.old_team==255?-1:draft.old_team;
    // the refusal bits only for a computer player (the owner of a human list player had already agreed)
    if(draft.seller<0)save_.state.at(pool+size_t(draft.pool_slot)*record+0x1A)|=uint8_t(0x80|manager_bit(a));
    auto& injured_role=mgr_[a].injured_role;
    std::move(injured_role.begin()+draft.club_slot+1,injured_role.end(),injured_role.begin()+draft.club_slot);
    injured_role.back()=0;save_.squad_of(a)=hok_squad(save_,a);
    return true;
}
// Completes the contract talks of a provisional purchase with `years` years at `salary` (internal money unit, DM x 3).
// More than 4 years: the player refuses. A one-year contract at no less than the initial salary is always accepted;
// otherwise the player decides (player_accepts). A refusal draws rand(10, 18) and cancels the purchase. On success
// the row gets years and salary, a human seller's pool row passes on its status bytes (+0x00..+0x02, +0x09, +0x0D,
// +0x17), the pool row is removed, the buyer becomes the owner and pays the fee. A human seller receives the fee and no
// purchase sponsor is drawn; for a computer seller a sponsor subsidy may be drawn and is added if `accept_subsidy`.
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
    if(draft.seller>=0){  // bought from a human manager: the squad row keeps these bytes of the seller's list row
        const size_t from=pool+size_t(draft.pool_slot)*record;
        for(const size_t k:{size_t(0x09),size_t(0x17),size_t(0x0D),size_t(0x00),size_t(0x01),size_t(0x02)})save_.state.at(destination+k)=save_.state.at(from+k);
    }
    clear_notification(save_,draft.player_id);remove_squad_record(4,draft.pool_slot,10);
    save_.state.at(players+size_t(draft.player_id-1)*33+0x1A)=uint8_t(a);
    save_.squad_of(a)=hok_squad(save_,a);mgr_[a].budget.balance-=draft.fee;
    log_.push_back(player->name+" bought for "+std::to_string(draft.fee/3)+" DM, "+std::to_string(years)+" years at "+std::to_string(salary/3)+" DM");
    if(draft.seller>=0&&draft.seller<managers_){
        // the owner gets the fee, the buyer pays it, no purchase sponsor is drawn
        mgr_[draft.seller].budget.balance+=draft.fee;last_purchase_subsidy_=0;
        if(prepare_lineup)prepare_human_lineup(false,a);
        refresh_budget_roster(a);refresh_budget_roster(draft.seller);return PurchaseResult::Purchased;
    }
    const long subsidy=purchase_subsidy(draft.fee,brng_);last_purchase_subsidy_=subsidy;
    if(subsidy>0){if(accept_subsidy)mgr_[a].budget.balance=long(int32_t(uint32_t(mgr_[a].budget.balance)+uint32_t(subsidy)));log_.push_back("purchase sponsor subsidy "+std::to_string(subsidy/3)+(accept_subsidy?" DM accepted":" DM declined"));}
    if(prepare_lineup)prepare_human_lineup(false,a);
    refresh_budget_roster(a);return PurchaseResult::Purchased;
}

// A bid of the active manager for a list player of another human manager.
// The click and the fee editor are the same as for a computer player (x > 250: loan); there is no seller check of the
// computer market. Order: refused before (pool +0x1A bit 7 with the bidder's bit) -> "Angebot wurde bereits abgelehnt."
// (the bidder's bit alone does not stop the click here); cash >= bid; the market value of the pool record
// (/ 3 for a loan); a bid below 60 % or above 140 % of it (computed in signed 32-bit arithmetic)
// is unfair: the owner's dialog "Dieser Betrag ist / nicht erlaubt (unfair) !" and "Ihr Angebot wurde abgelehnt.", no bit.
// Otherwise the owner is asked at the same seat, with his name: "Nehmen Sie das Angebot (<bid> DM)" /
// "für <name> an ?". `offer` is filled for answer_manager_offer.
PurchaseResult Game::offer_to_manager(size_t slot,long fee,bool loan,PurchaseDraft& offer){
    offer={};
    const int a=active_manager_;
    if(slot>=11||fee<=0)return PurchaseResult::Invalid;
    if(national_coach_>=0||mgr_[a].team>=52)return PurchaseResult::Unavailable;
    const size_t source=pool+slot*record;const int id=save_.state.at(source+15);
    HokPlayer* player=id?player_by_exe_index(id):nullptr;
    const int owner=player?player->raw[0x1A]:-1;
    if(!player||owner<0||owner>=managers_||owner==a)return PurchaseResult::Unavailable;
    const uint8_t flags=save_.state.at(source+0x1A);
    if((flags&0x80)&&(flags&manager_bit(a)))return PurchaseResult::Blocked;
    if(mgr_[a].budget.balance<fee)return PurchaseResult::InsufficientFunds;
    long value=player_worth(contracts_,contract_player(save_,4,int(slot)),Worth::Value,brng_);
    if(loan)value/=3;
    offer.pool_slot=int(slot);offer.player_id=id;offer.fee=fee;offer.manager=a;offer.seller=owner;offer.loan=loan;
    const int32_t low=int32_t(uint32_t(value)*60u)/100,high=int32_t(uint32_t(value)*140u)/100;
    if(low>fee||high<fee)return PurchaseResult::Unfair;
    return PurchaseResult::OwnerQuestion;
}
// The owner's answer. "Nein !": "Ihr Angebot wurde abgelehnt." and pool +0x1A |= 0x80 | bidder's bit.
// "Na gut.": the player record takes the pool row's +0x10..+0x12 at +0x15..+0x17, then the purchase or
// loan continues as for a computer player (squad row, contract panel); see finish_transfer_purchase for the payment.
PurchaseResult Game::answer_manager_offer(const PurchaseDraft& offer,bool accept,PurchaseDraft& draft,bool prepare_lineup){
    draft={};
    const int a=active_manager_;
    if(offer.manager!=a||offer.pool_slot<0||offer.pool_slot>=11||offer.seller<0||offer.seller>=managers_)return PurchaseResult::Invalid;
    const size_t source=pool+size_t(offer.pool_slot)*record;
    const size_t p=players+size_t(offer.player_id-1)*33;
    if(offer.player_id<1||offer.player_id>kHokPlayers||save_.state.at(source+15)!=offer.player_id||save_.state.at(p+0x1A)!=offer.seller)
        return PurchaseResult::Invalid;
    if(!accept){save_.state.at(source+0x1A)|=uint8_t(0x80|manager_bit(a));return PurchaseResult::SellerRefused;}
    for(size_t k=0;k<3;k++)save_.state.at(p+0x15+k)=save_.state.at(source+0x10+k);
    if(HokPlayer* player=player_by_exe_index(offer.player_id)){
        player->skill_a=save_.state.at(p+0x15);player->skill_b=save_.state.at(p+0x16);player->form=save_.state.at(p+0x17);
    }
    if(offer.loan)return insert_loan(size_t(offer.pool_slot),offer.fee,offer.seller,false,prepare_lineup);
    const PurchaseResult result=insert_purchase(size_t(offer.pool_slot),offer.fee,draft);
    draft.seller=offer.seller;
    return result;
}
} // namespace em
