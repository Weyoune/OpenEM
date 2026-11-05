// SPDX-License-Identifier: GPL-3.0-only
#include "game/reporting.hpp"
#include "game/game.hpp"
#include "game/checkpoint.hpp"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
using namespace em;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static size_t record(int team){return 0x17E7+size_t(team)*61;}
int main(int argc,char**argv){
    try{
        if(argc!=3)throw std::runtime_error("reporting_tests DATA NATIVE_FOLDER");
        const std::filesystem::path data=argv[1],native=argv[2];
        if(std::filesystem::exists(native/"clubinfo-esv-report2.hok")){
            Game g(read_file(data/"EM.EXE"),read_file(native/"gui-h0-reload.hok"),61);
            const auto seed=decode_game_checkpoint(g.checkpoint_bytes()).borland_random;
            require(g.set_team_reporting(4,1)==1,"ESV intermediate reporting");
            require(decode_hok(g.save_bytes()).state==decode_hok(read_file(native/"clubinfo-esv-report1.hok")).state,"all47761 native first symbol-change bytes");
            require(g.set_team_reporting(4,2)==2,"ESV camera reporting");
            require(decode_hok(g.save_bytes()).state==decode_hok(read_file(native/"clubinfo-esv-report2.hok")).state,"all47761 native second symbol-change bytes");
            require(decode_game_checkpoint(g.checkpoint_bytes()).borland_random==seed,"report changes consume no Borland draws");
            g.set_team_reporting(4,0);
            require(decode_hok(g.save_bytes()).state==decode_hok(read_file(native/"gui-h0-reload.hok")).state,"report cycle restores original HOK state");
        }
        HokFile h;h.state.resize(record(150));
        for(int t=0;t<4;t++)h.state[record(t)+0x34]=uint8_t(0x80+t+1);
        h.state[record(5)+0x34]=17;
        require(set_team_reporting(h,5,2)==0&&h.state[record(5)+0x34]==17,"fifth CPU camera request skips camera and preserves crest");
        h.state[record(5)+0x16]=1;
        require(set_team_reporting(h,5,2)==2&&h.state[record(0)+0x34]==0x41&&h.state[record(5)+0x34]==0x91,"human camera demotes first earlier unowned camera");
        for(int t=0;t<150;t++){h.state[record(t)+0x34]=0;h.state[record(t)+0x16]=0;}
        for(int t=0;t<4;t++){h.state[record(t)+0x34]=0x80;h.state[record(t)+0x16]=uint8_t(t+1);}
        h.state[record(6)+0x16]=1;
        require(set_team_reporting(h,6,2)==2,"four human cameras do not invent a denial when no CPU can be demoted");
        require(set_team_reporting(h,52,2)==2,"national camera scan excludes club cameras");
        for(int t=52;t<56;t++)h.state[record(t)+0x34]=0x80;
        h.state[record(7)+0x34]=0;
        require(set_team_reporting(h,7,2)==0,"club camera scan includes national cameras");
        std::puts("Native reporting full-state observations and camera arbitration passed");return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"Reporting: %s\n",e.what());return 1;}
}
