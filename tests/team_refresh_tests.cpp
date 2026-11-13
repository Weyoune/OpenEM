// SPDX-License-Identifier: GPL-3.0-only
// Tests for refresh_menu_team (src/game/team_refresh.cpp), the recomputation of the club's team
// record that the original performs when the "Stärken" (team strengths) screen is entered.
// Part 1 (only when the reference saves exist) compares the complete state after entering the
// screen with a state recorded from the original game. Part 2 uses START.HOK with a synthetic
// squad of eight players to check the line counts, the strength arithmetic, the zero-ID row
// quirk, that only the documented team fields are written, and that the in-memory line counts
// survive an OpenEM checkpoint reload.
// Arguments: the game data folder, the reference folder with START.HOK and the folder holding
// the recorded saves.
#include "game/team_refresh.hpp"
#include "game/game.hpp"
#include "game/checkpoint.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
using namespace em;
// Throws `why` as a test failure unless `ok`.
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
        // Manager 0's club `team` (manager block +0x14), its 61-byte team record `t` and squad
        // block `base`. Team record fields set here: +0x16 managing human (1), +0x2C flags (0xE8,
        // bit 3 = six skaters / pulled goalie), +0x2E current defence line, +0x30, +0x33.
        auto raw=decode_hok(start);const int team=raw.state.at(0x0DCD+0x14);
        const size_t t=0x17E7+size_t(team)*61,base=0x696D;
        std::fill_n(raw.state.begin()+base,30*52,uint8_t(0));
        raw.state.at(t+0x16)=1;raw.state.at(t+0x2C)=0xE8;raw.state.at(t+0x2E)=0;raw.state.at(t+0x30)=0;raw.state.at(t+0x33)=0;
        // Independent arithmetic vector: one goalie, four defenders (block 1), three attackers
        // (block 1 forward half, line byte 0x81), all three skill bytes (+16..+18) set to 30.
        for(int slot=0;slot<8;slot++){
            const size_t r=base+size_t(slot)*52,p=0x4FC6+size_t(slot)*33;
            raw.state.at(r+15)=uint8_t(slot+1);raw.state.at(r+10)=1;
            raw.state.at(r+21)=uint8_t(slot<5?1:129);
            raw.state.at(p+24)=uint8_t(slot==0?0:slot<5?25:60);
            for(int k=16;k<=18;k++)raw.state.at(r+size_t(k))=30;
        }
        auto h=decode_hok(encode_hok(raw));const auto before=h.state;
        const auto counts=refresh_menu_team(h,0);
        require(counts.defence==4&&counts.attack==1,"original attack correction starts from 3, not old attack count");
        require(h.teams[size_t(team)].skill1[0]==23&&h.teams[size_t(team)].skill2[0]==0&&h.teams[size_t(team)].form[0]==23,"four defenders force zero technique; goalie contributes to all three lines");
        require(h.teams[size_t(team)].skill1[1]==10&&h.teams[size_t(team)].skill2[1]==15&&h.teams[size_t(team)].form[1]==10,"four defenders also select the original attack technique bonus");
        require(h.state.at(t+0x32)==5&&h.state.at(t+0x2C)==0xE0,"five skaters clear only pulled-goalie bit");
        HumanSquad attack_only{&h.squad,&h.players};attack_only.per_line[0]=2;attack_only.per_line[1]=4;
        require(line_strength(h.teams[size_t(team)],attack_only,0,1,false,0).skill2==30,"four attackers alone do not select the defence-cache bonus");
        for(size_t at=0;at<before.size();at++)if(before[at]!=h.state[at])
            require(at>=t&&at<t+61&&((at-t>=0x18&&at-t<=0x1D)||at-t==0x2C||at-t==0x2E||at-t==0x32),"entry writes only nine documented team fields");
        // Zero-ID row inside the occupied prefix: the original does not skip it but reads the dummy
        // player record just before player 1 (0x4FA5), here made a defender.
        auto hole=decode_hok(encode_hok(raw));
        hole.state.at(base+52+15)=0;hole.state.at(base+8*52+15)=9;
        hole.state.at(0x4FA5+24)=25; //the dummy record of ID 0 is a defender
        const auto hole_counts=refresh_menu_team(hole,0);
        require(hole_counts.defence==4&&hole_counts.attack==1,"prefix ID 0 retains its old role/line instead of becoming an empty model row");
        require(hole.teams[size_t(team)].skill1[0]==23&&hole.teams[size_t(team)].skill2[1]==15,"stale ID 0 skills remain visible to the strength calculation");
        // Manual lineup (active and saved mode 0): the line counts computed on entry are kept only
        // in memory by the original, so they must travel in the OpenEM checkpoint.
        raw.state.at(0x078A)=raw.state.at(0x078B)=0;
        Game manual(exe,encode_hok(raw),61);manual.enter_strengths();
        const auto checkpoint=manual.checkpoint_bytes();
        require(decode_game_checkpoint(checkpoint).line_counts[0]==(std::array<uint8_t,2>{4,1}),"transient count cache is included in OEMv2");
        Game resumed(exe,checkpoint,999);
        require(resumed.checkpoint_bytes()==checkpoint,"count-cache reload preserves the complete phase");
        manual.advance_day();resumed.advance_day();
        require(manual.checkpoint_bytes()==resumed.checkpoint_bytes(),"special lineup continues identically after count-cache reload");
        // Without a goalkeeper (row 0 taken out of the squad) the current defence line is reset first.
        raw.state.at(base+10)=0;raw.state.at(t+0x2E)=2;
        h=decode_hok(encode_hok(raw));const auto no_goalie=refresh_menu_team(h,0);
        require(h.state.at(t+0x2E)==0&&no_goalie.defence==4&&no_goalie.attack==2,"missing goalie resets current defence before counting");
        require(h.teams[size_t(team)].skill1[0]==13&&h.teams[size_t(team)].skill2[0]==0,"missing goalie strength arithmetic");
        std::puts("Native team entry state and special-lineup vectors passed");return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"Team entry: %s\n",e.what());return 1;}
}
