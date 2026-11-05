// SPDX-License-Identifier: GPL-3.0-only
// Native read-only DOSBox RAM + seed observations, not a mirrored algorithm.
#include "game/game.hpp"
#include "game/checkpoint.hpp"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
using namespace em;
namespace fs=std::filesystem;
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void same_state(const Bytes& actual,const Bytes& expected,const char* message){
    const auto a=decode_hok(actual),b=decode_hok(expected);
    if(a.state!=b.state){int count=0;for(size_t n=0;n<a.state.size();n++)if(a.state[n]!=b.state[n]){if(count<12)std::fprintf(stderr,"state%04zx:actual%u expected%u\n",n,a.state[n],b.state[n]);count++;}std::fprintf(stderr,"%s:%d differing bytes\n",message,count);throw std::runtime_error(message);}
}
int main(int argc,char** argv){
    try{
        if(argc!=4)throw std::runtime_error("purchase_tests DATA REFERENCES NATIVE_CAPTURE_FOLDER");
        const fs::path data=argv[1],refs=argv[2],native=argv[3];
        const auto exe=read_file(data/"EM.EXE"),before=read_file(native/"nativepurchase-before.hok");
        Game game(exe,before,1617108858u);
        same_state(game.save_bytes(),before,"native starting phase");
        const auto pristine=game.checkpoint_bytes();PurchaseDraft draft;
        require(game.begin_transfer_purchase(3,0,draft)==PurchaseResult::Invalid,"zero fee has no negotiation");
        require(game.checkpoint_bytes()==pristine,"fee Escape/zero is RNG/state neutral");
        require(game.begin_transfer_purchase(3,1200000,draft)==PurchaseResult::Negotiating,"accepted native fee opens contract");
        require(draft.player_id==56&&draft.club_slot==12&&draft.initial_salary==36300,"native Frosch ID/insertion/salary golden");
        same_state(game.save_bytes(),read_file(native/"nativepurchase-pending.hok"),"all47761 native pending bytes");
        require(decode_game_checkpoint(game.checkpoint_bytes()).borland_random==2618852941u,"native seven-draw seed");
        Game continued(exe,game.checkpoint_bytes());
        require(continued.cancel_transfer_purchase(draft),"explicit native Abbruch cleanup");
        same_state(continued.save_bytes(),read_file(native/"nativepurchase-cancel.hok"),"all47761 native cancel bytes");
        require(decode_game_checkpoint(continued.checkpoint_bytes()).borland_random==2618852941u,"Abbruch adds no cooldown draw");
        const auto cancelled=continued.checkpoint_bytes();
        require(!continued.cancel_transfer_purchase(draft)&&continued.checkpoint_bytes()==cancelled,"stale cancel context is neutral");
        PurchaseDraft retry;
        require(continued.begin_transfer_purchase(3,1200000,retry)==PurchaseResult::Blocked,"cancelled native offer is blocked");
        require(continued.checkpoint_bytes()==cancelled,"blocked retry draws no RNG");
        require(continued.list_transfer_player(19,false)==TransferMoveResult::Moved,"native additional Dobryzinski listing");
        same_state(continued.save_bytes(),read_file(native/"nativepurchase-after-ack.hok"),"all47761 native additional listing bytes");
        require(decode_game_checkpoint(continued.checkpoint_bytes()).borland_random==1470177806u,"additional listing has five native draws");
        std::puts("Native purchase begin/Abbruch/retry/listing full-state and RNG observations passed");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"Purchase: %s\n",e.what());return 1;}
}
