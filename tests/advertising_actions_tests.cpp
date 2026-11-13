// SPDX-License-Identifier: GPL-3.0-only
// Advertising ("Werbung") actions of the game core: entering the advertising page, accepting a sponsor offer,
// renewing an expiring advertising contract in two steps (prepare, then confirm) and the ordering of the
// contract list after an acceptance. Each step is compared byte for byte with reference saves the original
// game wrote in the same situation, and checked for its effect on the two random streams and on reloads of
// an OpenEM checkpoint.
//
// Usage: advertising_actions_tests DATA REFERENCE NATIVE
//   DATA      folder with the user's EM.EXE
//   REFERENCE folder with the reference saves MONAT1.HOK and ADRENEW.HOK
//   NATIVE    folder with the saves the original wrote after entering advertising and after accepting the offer
#include "game/game.hpp"
#include "game/checkpoint.hpp"
#include <cstdio>
#include <stdexcept>
using namespace em;
// Throws with message `s` when `b` is false; the test stops at the first failed requirement.
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("advertising_actions_tests DATA REFERENCE NATIVE");
    const std::filesystem::path data=argv[1],refs=argv[2],native=argv[3];
    const auto exe=read_file(data/"EM.EXE");Game game(exe,read_file(refs/"MONAT1.HOK"),355618701u);
    const auto initial=decode_game_checkpoint(game.checkpoint_bytes());
    require(game.enter_advertising(),"native advertising entry refreshes saved income");
    require(decode_hok(game.save_bytes()).state==decode_hok(read_file(native/"advertising-monat1-entry.hok")).state,"all47761 native advertising entry bytes");
    const auto entered=decode_game_checkpoint(game.checkpoint_bytes());
    require(entered.borland_random==initial.borland_random&&entered.game_random==initial.game_random,"advertising entry consumes neither random stream");
    const auto stable=game.checkpoint_bytes();require(!game.enter_advertising()&&game.checkpoint_bytes()==stable,"reopening advertising is idempotent without random draws");
    require(game.sponsor_offers()[0][4].amount==64342&&game.sponsor_offers()[0][4].boards==2&&game.sponsor_offers()[0][4].months==24,"independent native Amiga offer vector");
    require(game.accept_sponsor_offer(0,4),"native two-board offer accepted");
    require(decode_hok(game.save_bytes()).state==decode_hok(read_file(native/"advertising-monat1-accepted.hok")).state,"all47761 native accepted contract bytes including serial/order/income");
    const auto accepted=decode_game_checkpoint(game.checkpoint_bytes());
    require(accepted.borland_random==initial.borland_random&&accepted.game_random==initial.game_random,"offer acceptance consumes neither random stream");
    const auto after=game.checkpoint_bytes();require(!game.accept_sponsor_offer(0,4)&&game.checkpoint_bytes()==after,"consumed offer cannot be accepted twice");
    Game resumed(exe,after);require(resumed.checkpoint_bytes()==after,"advertising contract and remaining offers survive OEM reload");
    game.advance_day();resumed.advance_day();require(game.checkpoint_bytes()==resumed.checkpoint_bytes(),"advertising action continues identically after checkpoint reload");
    for(bool accept:{false,true}){
        const auto renewal_source=read_file(refs/"ADRENEW.HOK");Game direct(exe,renewal_source,12),staged(exe,renewal_source,12);
        const auto before=staged.checkpoint_bytes();const auto raw=decode_hok(staged.save_bytes()).state;
        AdvertRenewalDraft draft;require(staged.begin_advert_renewal(5,draft)&&draft.sponsor_stays,"independent native renewal offer is prepared before confirmation");
        require(staged.checkpoint_bytes()!=before&&decode_hok(staged.save_bytes()).state==raw,"pending renewal consumes original RNG but books no contract or cache changes");
        const auto random=decode_game_checkpoint(staged.checkpoint_bytes());
        require(direct.renew_advert_contract(5,accept)==staged.finish_advert_renewal(draft,accept),"staged renewal decision matches one-shot result");
        require(direct.checkpoint_bytes()==staged.checkpoint_bytes(),"staged renewal matches full state and both random streams");
        require(decode_game_checkpoint(staged.checkpoint_bytes()).borland_random==random.borland_random,"confirmation does not repeat the sponsor's random decision");
        const auto resolved=staged.checkpoint_bytes();require(!staged.finish_advert_renewal(draft,accept)&&staged.checkpoint_bytes()==resolved,"prepared renewal cannot be resolved twice");
    }
    // Sort order after an acceptance: seven contracts (state 0xACFC, 10 bytes each: +0 monthly amount u32,
    // +4 sponsor, +5 months, +6 flags) with partly equal amounts, and one offer in the offer table (0xA52C).
    // Accepting it makes the original resort the list; its pairwise swaps put equal amounts in reverse order.
    auto tie=decode_hok(read_file(refs/"MONAT1.HOK"));
    const auto put32=[&](size_t at,uint32_t n){for(size_t i=0;i<4;++i)tie.state.at(at+i)=uint8_t(n>>(i*8));};
    constexpr std::array<uint32_t,7> amounts{2,2,1,3,4,5,6};
    for(size_t i=0;i<7;++i){const size_t at=0xacfc+i*10;put32(at,amounts[i]);tie.state[at+4]=uint8_t(10+i);tie.state[at+5]=12;tie.state[at+6]=i==6?128:0;}
    tie.state[0x01f2]=99;const size_t offer=0xa52c+40;put32(offer,7);tie.state[offer+4]=1;tie.state[offer+5]=12;tie.state[offer+6]=0;
    Game sorted(exe,encode_hok(tie),1);require(sorted.accept_sponsor_offer(0,4),"synthetic replacement enables native sort probe");
    const auto sorted_state=decode_hok(sorted.save_bytes());constexpr std::array<int,7> owner_order{12,11,10,13,14,15,99};
    for(size_t i=0;i<7;++i)require(sorted_state.state[0xacfc+i*10+4]==owner_order[i],"native indirect pair swaps reverse equal monthly amounts");
    std::puts("Native advertising actions passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Advertising actions: %s\n",e.what());return 1;}}
