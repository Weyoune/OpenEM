// SPDX-License-Identifier: GPL-3.0-only
#include "game/team_refresh.hpp"
#include "game/game.hpp"
#include "game/checkpoint.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
using namespace em;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char**argv){
    try{
        if(argc!=4)throw std::runtime_error("team_refresh_tests DATA REFERENCES NATIVE_FOLDER");
        const std::filesystem::path data=argv[1],refs=argv[2],native=argv[3];
        const auto exe=read_file(data/"EM.EXE"),start=read_file(refs/"START.HOK");
        if(std::filesystem::exists(native/"strengths-start-entry.hok")){
            Game g(exe,read_file(native/"gui-h0-reload.hok"));const auto before=g.checkpoint_bytes();
            g.enter_strengths();
            require(decode_hok(g.save_bytes()).state==decode_hok(read_file(native/"strengths-start-entry.hok")).state,"all47761 native Stärken entry bytes");
            require(g.checkpoint_bytes()==before,"already synchronized START entry is state/RNG neutral");
        }
        auto raw=decode_hok(start);const int team=raw.state.at(0x0DCD+0x14);
        const size_t t=0x17E7+size_t(team)*61,base=0x696D;
        std::fill_n(raw.state.begin()+base,30*52,uint8_t(0));
        raw.state.at(t+0x16)=1;raw.state.at(t+0x2C)=0xE8;raw.state.at(t+0x2E)=0;raw.state.at(t+0x30)=0;raw.state.at(t+0x33)=0;
        // Independent162C:146C/4F6E:34F6 arithmetic vector: one goalie,
        // four defenders, three attackers, all three skill bytes30.
        for(int slot=0;slot<8;slot++){
            const size_t r=base+size_t(slot)*52,p=0x4FC6+size_t(slot)*33;
            raw.state.at(r+15)=uint8_t(slot+1);raw.state.at(r+10)=1;
            raw.state.at(r+21)=uint8_t(slot<5?1:129);
            raw.state.at(p+24)=uint8_t(slot==0?0:slot<5?25:60);
            for(int k=16;k<=18;k++)raw.state.at(r+size_t(k))=30;
        }
        auto h=decode_hok(encode_hok(raw));const auto before=h.state;
        const auto counts=refresh_menu_team(h,0);
        require(counts.defence==4&&counts.attack==1,"native attack correction starts from3, not old attack count");
        require(h.teams[size_t(team)].skill1[0]==23&&h.teams[size_t(team)].skill2[0]==0&&h.teams[size_t(team)].form[0]==23,"four defenders force zero technique; goalie contributes to all three lines");
        require(h.teams[size_t(team)].skill1[1]==10&&h.teams[size_t(team)].skill2[1]==15&&h.teams[size_t(team)].form[1]==10,"four defenders also select the native attack technique bonus");
        require(h.state.at(t+0x32)==5&&h.state.at(t+0x2C)==0xE0,"five skaters clear only pulled-goalie bit");
        HumanSquad attack_only{&h.squad,&h.players};attack_only.per_line[0]=2;attack_only.per_line[1]=4;
        require(line_strength(h.teams[size_t(team)],attack_only,0,1,false,0).skill2==30,"four attackers alone do not select the defence-cache bonus");
        for(size_t at=0;at<before.size();at++)if(before[at]!=h.state[at])
            require(at>=t&&at<t+61&&((at-t>=0x18&&at-t<=0x1D)||at-t==0x2C||at-t==0x2E||at-t==0x32),"entry writes only nine documented team fields");
        auto hole=decode_hok(encode_hok(raw));
        hole.state.at(base+52+15)=0;hole.state.at(base+8*52+15)=9;
        hole.state.at(0x4FA5+24)=25; //native ID0's biased global record is a defender
        const auto hole_counts=refresh_menu_team(hole,0);
        require(hole_counts.defence==4&&hole_counts.attack==1,"native prefix ID0 retains its old role/line instead of becoming an empty model row");
        require(hole.teams[size_t(team)].skill1[0]==23&&hole.teams[size_t(team)].skill2[1]==15,"stale ID0 skills remain visible to native16BF");
        raw.state.at(0x078A)=raw.state.at(0x078B)=0;
        Game manual(exe,encode_hok(raw),61);manual.enter_strengths();
        const auto checkpoint=manual.checkpoint_bytes();
        require(decode_game_checkpoint(checkpoint).line_counts[0]==(std::array<uint8_t,2>{4,1}),"native transient count cache is included in OEMv2");
        Game resumed(exe,checkpoint,999);
        require(resumed.checkpoint_bytes()==checkpoint,"count-cache reload preserves the complete phase");
        manual.advance_day();resumed.advance_day();
        require(manual.checkpoint_bytes()==resumed.checkpoint_bytes(),"special lineup continues identically after count-cache reload");
        raw.state.at(base+10)=0;raw.state.at(t+0x2E)=2;
        h=decode_hok(encode_hok(raw));const auto no_goalie=refresh_menu_team(h,0);
        require(h.state.at(t+0x2E)==0&&no_goalie.defence==4&&no_goalie.attack==2,"missing goalie resets current defence before counting");
        require(h.teams[size_t(team)].skill1[0]==13&&h.teams[size_t(team)].skill2[0]==0,"missing goalie strength arithmetic");
        std::puts("Native team entry state and special-lineup vectors passed");return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"Team entry: %s\n",e.what());return 1;}
}
