// SPDX-License-Identifier: GPL-3.0-only
// Reporting/camera mode of a team and its Game wrapper; see reporting.hpp.
#include "game/reporting.hpp"
#include "game/game.hpp"
namespace em {
int set_team_reporting(HokFile& h,int team,int mode){
    if(team<0||team>=150||mode<0||mode>2)throw FormatError("Reporting: invalid team or mode");
    const auto field=[](int t){return size_t(0x17E7)+size_t(t)*61+0x34;};
    const bool nation=team>=52&&team<64;
    const int begin=nation?52:0,end=nation?64:150;
    const bool owned=h.state.at(field(team)-0x34+0x16)!=0;
    int cameras=0;
    if(mode==2)for(int t=begin;t<end;t++){
        if((h.state.at(field(t))>>6)!=2)continue;
        cameras++;
        if(cameras!=4||mode!=2)continue;
        if(!owned)mode=0;
        else for(int previous=begin;previous<t;previous++){
            if((h.state.at(field(previous))>>6)==2&&h.state.at(field(previous)-0x34+0x16)==0){
                h.state.at(field(previous))=uint8_t((h.state.at(field(previous))&63)|64);
                cameras--;break;
            }
        }
    }
    h.state.at(field(team))=uint8_t((h.state.at(field(team))&63)|(mode<<6));
    return mode;
}
int Game::set_team_reporting(int team,int mode){return em::set_team_reporting(save_,team,mode);}
} // namespace em
