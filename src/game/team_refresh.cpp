// SPDX-License-Identifier: GPL-3.0-only
// Team record refresh before strength screens and its Game entry points; see team_refresh.hpp.
#include "game/team_refresh.hpp"
#include "game/game.hpp"
#include <algorithm>

namespace em {
TeamRefreshCounts refresh_menu_team(HokFile& h,int manager){
    if(manager<0||manager>=6)throw FormatError("Team refresh: invalid manager");
    const int id=h.state.at(0x0DCD+size_t(manager)*0x1AF+0x14);
    TeamRefreshCounts counts;
    if(id>=150)return counts;
    auto& team=h.teams.at(size_t(id));
    const int owner=team.raw[0x16];
    std::vector<HokSquadPlayer> squad;
    auto players=h.players;
    HumanSquad human{&squad,&players};
    if(owner){
        if(owner>6)throw FormatError("Team refresh: invalid owner");
        const int roster=owner-1;
        const size_t base=0x696D+size_t(roster)*0x618;
        int occupied=0;
        for(int slot=0;slot<(roster==4?12:29);slot++)
            occupied+=h.state.at(base+size_t(slot)*52+15)!=0;
        // The original does not reject ID 0 inside this prefix. A removal can leave old
        // role/line/skill bytes there; ID 0 uses the dummy record before player 1.
        HokPlayer zero;zero.position=h.state.at(0x4FA5+24);
        const int zero_index=int(players.size());players.push_back(zero);
        for(int slot=0;slot<occupied;slot++){
            const size_t at=base+size_t(slot)*52;const int player=h.state.at(at+15);
            HokSquadPlayer row;row.player=zero_index;
            if(player){
                row.player=-1;
                for(size_t p=0;p<h.players.size();p++)if(h.players[p].raw==h.state.data()+0x4FC6+size_t(player-1)*33){row.player=int(p);break;}
                if(row.player<0)throw FormatError("Team refresh: invalid roster player");
            }
            row.role=h.state.at(at+10);row.line=h.state.at(at+21);
            row.skill_a=h.state.at(at+16);row.skill_b=h.state.at(at+17);row.form=h.state.at(at+18);
            row.nibbles=h.state.at(at+25);squad.push_back(row);
        }
        const auto count=[&](int kind){
            int n=0;
            for(const auto& p:squad){
                if(!p.role||!(p.line&127))continue;
                const int natural=std::min(players.at(size_t(p.player)).position/25,2);
                if(kind==0){if(natural==0&&!(p.line&128))n++;}
                else if(natural!=0&&(p.line>>7)==kind-1&&((p.line&127)-1)==team.current_line[kind-1])n++;
            }
            return n;
        };
        const int goalies=count(0);
        if(!goalies)team.current_line[0]=0;
        counts.defence=count(1);counts.attack=count(2);
        const int timer=team.raw[0x33];
        const int penalties=int((timer&7)!=0)+int(((timer>>4)&7)!=0);
        const int limit=!goalies&&counts.defence+counts.attack<6?5:6;
        const int excess=counts.defence+counts.attack+goalies+penalties-limit;
        if(excess>0)counts.attack=uint8_t(3-excess);
        team.skaters=uint8_t(counts.defence+counts.attack);
        team.flags=uint8_t((team.flags&~8)|(counts.defence+counts.attack==6?8:0));
        human.per_line[0]=counts.defence;human.per_line[1]=counts.attack;
    }
    for(int kind=0;kind<2;kind++){
        int a=0,b=0,f=0;
        for(int line=0;line<3;line++){
            const auto v=owner?line_strength(team,human,line,kind,false,0):line_strength(team,line,kind,false,0);
            a+=v.skill1;b+=v.skill2;f+=v.form;
        }
        team.skill1[kind]=uint8_t(a/3);team.skill2[kind]=uint8_t(b/3);team.form[kind]=uint8_t(f/3);
    }
    // Keep the nine potential serialized fields synchronized with the model.
    const size_t at=0x17E7+size_t(id)*61;
    for(int kind=0;kind<2;kind++){
        h.state.at(at+0x18+kind)=uint8_t(team.skill1[kind]);
        h.state.at(at+0x1A+kind)=uint8_t(team.skill2[kind]);
        h.state.at(at+0x1C+kind)=uint8_t(team.form[kind]);
    }
    if(owner){h.state.at(at+0x2C)=uint8_t(team.flags);h.state.at(at+0x2E)=uint8_t(team.current_line[0]);h.state.at(at+0x32)=uint8_t(team.skaters);}
    return counts;
}

// Runs refresh_menu_team on a decoded copy and copies the refreshed team fields back into the save and its decoded
// model; then the cached line counts and team strength of every human manager follow.
void Game::refresh_menu_manager(int manager){
    HokFile next=decode_hok(save_bytes());
    const int id=next.state.at(0x0DCD+size_t(manager)*0x1AF+0x14);
    if(id>=150)return;
    const auto counts=refresh_menu_team(next,manager);
    const int owner=next.teams.at(size_t(id)).raw[0x16];
    if(owner)line_counts_.at(size_t(owner-1))={uint8_t(counts.defence),uint8_t(counts.attack)};
    const auto& from=next.teams.at(size_t(id));auto& to=save_.teams.at(size_t(id));
    for(int kind=0;kind<2;kind++){to.skill1[kind]=from.skill1[kind];to.skill2[kind]=from.skill2[kind];to.form[kind]=from.form[kind];}
    to.current_line[0]=from.current_line[0];to.flags=from.flags;to.skaters=from.skaters;
    const size_t at=0x17E7+size_t(id)*61;
    for(size_t field:{size_t(0x18),size_t(0x19),size_t(0x1A),size_t(0x1B),size_t(0x1C),size_t(0x1D),size_t(0x2C),size_t(0x2E),size_t(0x32)})
        save_.state.at(at+field)=next.state.at(at+field);
    for(int m=0;m<managers_;m++){  // the cached values of every human manager follow (one manager: manager 0 as before)
        auto& mg=mgr_[size_t(m)];
        const auto& team=save_.teams.at(size_t(mg.team));
        for(int kind=0;kind<2;kind++)mg.squad.per_line[kind]=line_counts_[size_t(m)][size_t(kind)];
        mg.budget.team_strength=team.skill1[0]+team.skill1[1]+team.skill2[0]+team.skill2[1];
    }
}
// Entering the strengths screen refreshes the teams of all human managers and of the national coach (block 5).
void Game::enter_strengths(){
    const int managers=save_.state.at(0x0DC6);
    if(managers<1||managers>4)throw FormatError("Team refresh: invalid human count");
    for(int manager=0;manager<6;manager++)if(manager<managers||manager==5)refresh_menu_manager(manager);
}
// Entering the club info of `team` refreshes it if a manager holds it.
void Game::enter_club_info(int team){
    if(team<0||team>=150)throw FormatError("Team info: invalid team");
    const int owner=save_.teams.at(size_t(team)).raw[0x16];
    if(owner>6)throw FormatError("Team info: invalid owner");
    if(owner)refresh_menu_manager(owner-1);
}
} // namespace em
