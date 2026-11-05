// SPDX-License-Identifier: GPL-3.0-only
#include "game/annual_players.hpp"
#include "game/game.hpp"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace em;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void put32(Bytes& bytes,size_t at,uint32_t value){for(size_t n=0;n<4;++n)bytes.at(at+n)=uint8_t(value>>(8*n));}
struct Gate{int age,threshold,years,flags;bool retires;};
// The seven gates are executed from original branch opcodes in
// annual_retirement/verify.cjs, independently of this implementation.
constexpr Gate gates[]={{19,42,0,128,true},{60,37,0,128,true},{19,37,1,128,false},
    {60,37,1,128,false},{60,37,0,0,false},{19,42,0,0,false},{255,37,255,128,false}};
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("annual_players_tests REFERENCES DATA");
    const std::filesystem::path refs=argv[1];const auto start=read_file(refs/"START.HOK");
    struct Draw{int low,high,value;};
    for(const auto gate:gates)for(int disability=0;disability<3;++disability){
        auto h=decode_hok(start);const size_t row=0x696d;
        const uint8_t id=h.state.at(row+15);const size_t global=0x4fa5+size_t(id)*33;
        h.state.at(global+0x14)=uint8_t(gate.age);h.state.at(global+0x19)=9;
        h.state.at(row+11)=uint8_t(gate.years);h.state.at(row+0x18)=uint8_t(gate.flags);
        h.state.at(row+13)=disability?0xff:0;
        const uint32_t premium=disability==1?3000:disability==2?0xfffffff0u:0;
        put32(h.state,row+0x30,premium);
        std::fill_n(h.state.begin()+std::ptrdiff_t(row+29*52),52,uint8_t(0xa5));
        h.state.at(row+29*52+15)=0;
        auto expected=h.state;const bool removed=disability||gate.retires;
        std::vector<Draw> transcript;if(!disability)transcript.push_back({37,42,gate.threshold});
        if(removed){
            transcript.insert(transcript.end(),{{0,3,2},{18,25,18},{40,92,40},{35,45,35},{35,45,45}});
            std::memmove(expected.data()+row,expected.data()+row+52,29*52);expected.at(row+29*52+15)=0;
            expected.at(global+0x1a)=6;expected.at(global+0x14)=18;
            expected.at(global+0x15)=35;expected.at(global+0x16)=45;expected.at(global+0x19)=0;
        }
        size_t cursor=0;const auto result=annual_owned_player(h,id,0,[&](int low,int high){
            require(cursor<transcript.size(),"no extra annual RNG draw");const auto d=transcript[cursor++];
            require(low==d.low&&high==d.high,"original annual RNG order and inclusive bounds");return d.value;
        });
        require(result.status==(disability?AnnualPlayerStatus::Disabled:gate.retires?AnnualPlayerStatus::Retired:AnnualPlayerStatus::Stayed),"native own-row career gate independent of age");
        require(result.slot==0&&result.insurance_payment==(disability?std::bit_cast<int32_t>(uint32_t(premium*60u)):0),"insurance low32 or no career payment");
        require(cursor==transcript.size()&&h.state==expected,"full raw state: shift through scratch, club/name/form retained, only documented globals regenerated");
    }
    { // Native15C6 uses occupied-count prefixes and skips inactive humans.
        auto h=decode_hok(start);const uint8_t id=h.state.at(0x696d+15);
        h.state.at(0x696d+15)=0;h.state.at(0x696d+5*0x618+15)=id;
        const auto before=h.state;int calls=0;
        require(annual_owned_player(h,id,0,[&](int,int){++calls;return 0;}).status==AnnualPlayerStatus::OtherStorage&&calls==0&&h.state==before,"other storage is skipped without a threshold draw");
        // Put the ID after an initial hole, outside this block's count prefix.
        std::fill_n(h.state.begin()+std::ptrdiff_t(0x696d+5*0x618),0x618,uint8_t(0));
        h.state.at(0x696d+5*0x618+2*52+15)=id;
        require(!native_find_roster_player(h,id),"raw hole retains native occupied-prefix lookup quirk");
    }
    { // Replay saved original skill deltas using compatible controlled returns.
        // No pre-pass native seed was captured: this proves byte effects and
        // bounds/order, rather than claiming a full native season RNG replay.
        const auto before=decode_hok(read_file(refs/"SAISEND.HOK"));
        const auto after=decode_hok(read_file(refs/"NEUSAIS.HOK"));
        auto state=before.state,expected=before.state;std::vector<int> draws;
        for(size_t id=1;id<200;++id)for(size_t field:{size_t(21),size_t(22)}){
            const size_t at=0x4fa5+id*33+field;const int delta=int(after.state.at(at))-before.state.at(at);
            require(delta>=-3&&delta<=3,"original annual fixture skill deltas");
            draws.push_back(delta+4);expected.at(at)=after.state.at(at);
        }
        size_t cursor=0;annual_global_skills(state,[&](int low,int high){require(low==1&&high==7&&cursor<398,"398 ordered native skill draws");return draws[cursor++];});
        require(cursor==398&&state==expected,"every original global skill pair, no squad writes");
    }
    for(int draw:{1,4,7}){
        Bytes state(47761,0xa5);const std::array<int,8> old{0,5,11,27,30,96,99,255};
        const std::array<std::array<int,8>,3> answers{{{{30,30,30,30,30,93,96,99}},{{30,30,30,30,30,96,99,99}},{{30,30,30,30,33,99,99,99}}}};
        for(size_t i=0;i<old.size();++i)state.at(0x4fc6+i*33+21)=uint8_t(old[i]);
        const auto squad=Bytes(state.begin()+0x696d,state.end());int calls=0;
        annual_global_skills(state,[&](int low,int high){require(low==1&&high==7,"skill bounds at clamps");++calls;return draw;});
        for(size_t i=0;i<old.size();++i)require(state.at(0x4fc6+i*33+21)==answers[size_t((draw-1)/3)][i],"native lower/upper skill clamp including youth5..11");
        require(calls==398&&std::equal(squad.begin(),squad.end(),state.begin()+0x696d),"skill pass leaves all squad bytes unchanged");
    }
    { // End-to-end manager0 annual review: year gate, youth DTOs, byte
        // contract wrap, retirement before talks, then checkpoint continuation.
        const auto exe=read_file(std::filesystem::path(argv[2])/"EM.EXE");
        auto source=decode_hok(read_file(refs/"SAISEND.HOK"));
        source.year=source.season_year=1994;source.date.year=1995;
        source.header.at(10)=uint8_t(1994);source.header.at(11)=uint8_t(1994>>8);
        for(size_t i=0;i<3;++i)source.state.at(0x06b8+i)=10;
        const uint8_t wraps=source.state.at(0x696d+15),leaves=source.state.at(0x696d+52+15);
        source.state.at(0x696d+11)=0;source.state.at(0x696d+0x18)=0x80;
        source.state.at(0x696d+52+11)=1;source.state.at(0x696d+52+0x18)=0x80;
        source.state.at(0x696d+13)=source.state.at(0x696d+52+13)=0;
        source.squad=hok_squad(source,0);store_hok(source);const auto input=encode_hok(source);
        int youths=0;
        for(uint32_t seed=1;seed<=12;++seed){
            Game game(exe,input,seed);int target_talks=0;
            game.set_contract_decider([&](const ContractPlayer& player,long){
                const auto& name=source.players.at(size_t(player.player)).name;
                target_talks+=name==source.players.at(size_t(wraps-1)).name||name==source.players.at(size_t(leaves-1)).name;
                return 2;
            });
            for(int day=0;day<20&&game.seasons_completed()==0;++day)game.advance_day();
            require(game.seasons_completed()==1,"annual manager review reaches September");
            const auto saved=decode_hok(game.save_bytes());
            // 6 = free player; 4 = the same player listed again by the daily transfer-list refresh on the same day
            // (ovl11_34ef; seed 3 after the RNG fixes of 05.11.2026, see docs/DEVIATIONS.md).
            require(saved.state.at(0x4fa5+size_t(leaves)*33+0x1a)==6||saved.state.at(0x4fa5+size_t(leaves)*33+0x1a)==4,"years1 plus career flag leaves before contract talk");
            require(saved.players.at(size_t(leaves-1)).team==game.human_team(),"career departure preserves old global club");
            const auto wrap_row=native_find_roster_player(saved,wraps);
            require(wrap_row&&wrap_row->manager==0&&saved.state.at(0x696d+size_t(wrap_row->slot)*52+11)==255&&target_talks==0,"zero-year byte wraps255 and both special rows bypass renewal talks");
            for(const auto& line:game.log()){
                constexpr std::string_view suffix=" joins from the youth team";
                if(!line.ends_with(suffix))continue;
                const std::string name=line.substr(0,line.size()-suffix.size());
                const auto player=std::find_if(saved.players.begin(),saved.players.end(),[&](const auto& p){return p.name==name;});
                require(player!=saved.players.end(),"youth name survives whole season transition");
                const size_t index=size_t(player-saved.players.begin());const auto row=native_find_roster_player(saved,uint8_t(index+1));
                require(source.players.at(index).raw[0x1a]==6&&player->raw[0x1a]==0&&row&&row->manager==0,"existing free global ID becomes an owned roster member");
                const size_t at=0x696d+size_t(row->slot)*52;
                require(player->age>=19&&player->age<=22&&saved.state.at(at+11)>=1&&saved.state.at(at+11)<=2,"new youth birthday and contract decrement in same review");
                require(player->skill_a==30&&player->skill_b==30&&saved.state.at(at+16)<30&&saved.state.at(at+17)<30,"late global skill clamp preserves independently generated low squad skills");
                ++youths;
            }
            Game resumed(exe,game.checkpoint_bytes(),999);
            game.set_contract_decider({});
            for(int day=0;day<3;++day){game.advance_day();resumed.advance_day();}
            require(game.checkpoint_bytes()==resumed.checkpoint_bytes(),"annual youth/retirement state continues byte-identically after OEM reload");
            if(seed==1){
                for(int day=0;day<250&&game.seasons_completed()<2;++day){game.advance_day();resumed.advance_day();}
                require(game.seasons_completed()==2&&game.checkpoint_bytes()==resumed.checkpoint_bytes(),"distribution cursors survive a checkpoint and the next full annual allocation");
            }
        }
        require(youths>0,"controlled seed set exercises actual annual youth generation");
        std::printf("annual integration: %d youth arrivals across12 seeds\n",youths);
    }
    std::puts("Annual own-player gates, cleanup, insurance and398 global skill draws passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Annual players: %s\n",e.what());return 1;}}
