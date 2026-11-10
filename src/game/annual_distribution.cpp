// SPDX-License-Identifier: GPL-3.0-only
#include "game/annual_distribution.hpp"
#include <bit>
#include <cstdio>
#include <cstdlib>

namespace em {
namespace {
constexpr size_t player_base=0x4fa5,team_base=0x17e7;
constexpr std::array<int,3> starts{0,12,24}; //DS07C0
constexpr std::array<int,8> buckets{0,11,12,23,24,35,36,42}; //DS07C6
constexpr std::array<int,17> cycle{0,0,1,1,2,2,3,3,4,4,5,6,7,8,9,10,11}; //DS48FE
size_t player_at(int id){return player_base+size_t(id)*33;}
size_t team_at(int id){return team_base+size_t(id)*61;}
void validate(const Bytes& s){
    if(s.size()<47761||s.at(0x0dc6)>4)throw FormatError("annual distribution: invalid native state");
}
int abroad_count(const Bytes& s){
    int result=0;for(int id=1;id<180;++id){const int club=s.at(player_at(id)+29);result+=club>63&&club!=255;}return result;
}
}
int annual_cpu_target(const Bytes& state,uint8_t id,const ContractRandomDraw& draw){
    validate(state);if(!id||id>199)throw FormatError("annual distribution: invalid player");
    const size_t p=player_at(id);
    const int mean=(int(state.at(p+21))+state.at(p+22)+state.at(p+23))/3;
    int tries=0,tier=1;
    for(;;){
        const int club=draw(0,42);const size_t team=team_at(club);bool accepted=false;
        if(state.at(team+22)==0){
            const int quality=(int(state.at(team+24))+state.at(team+25)+state.at(team+26)+state.at(team+27)+100)/6;
            accepted=tries>300||draw(2,tier*10)>std::abs(quality-mean);
        }
        tries=std::bit_cast<int16_t>(uint16_t(uint16_t(tries)+1));
        if(tries==100)tier=2;if(tries==200)tier=3;
        if(accepted)return club;
    }
}
int annual_cyclic_target(const Bytes& state,int league,DistributionCursors& cursors,const ContractRandomDraw& draw){
    validate(state);
    if(league==3)return draw(64,150); //unreached by the annual caller's zero bucket3 quota
    if(league<0||league>2||cursors[size_t(league)]>=17)throw FormatError("annual distribution: invalid cycle");
    auto& cursor=cursors[size_t(league)];
    for(;;){
        const int club=starts[size_t(league)]+cycle[cursor];
        if(++cursor==17)cursor=0;
        if(!state.at(team_at(club)+22))return club;
    }
}
std::array<int,4> annual_distribution_quotas(const Bytes& state){
    validate(state);std::array<int,4> human{};int total=0;
    for(size_t league=0;league<3;++league){
        for(size_t manager=0;manager<state.at(0x0dc6);++manager){
            if(state.at(0x0dcd+manager*0x1af+0xeb)!=league)continue;
            int count=0;for(size_t slot=0;slot<29;++slot)count+=state.at(0x696d+manager*0x618+slot*52+15)!=0;
            human[league]=uint8_t(human[league]+count);
        }
        total=uint8_t(total+human[league]);
    }
    total=uint8_t(total+abroad_count(state));const int remaining=uint8_t(179-total),denominator=total?total:1;
    std::array<int,4> need{};
    for(size_t league=0;league<3;++league)need[league]=remaining*(100*human[league]/denominator)/100;
    return need;
}
void annual_random_club_changes(Bytes& state,const ContractRandomDraw& draw){
    validate(state);int abroad=abroad_count(state);const int requested=draw(5,15);
    for(int change=0;change<requested;++change){
        int id=0;
        for(;;){
            id=draw(1,179);const int old=state.at(player_at(id)+29);
            if(abroad>4&&old>63&&old!=255){
                --abroad;const int target=annual_cpu_target(state,uint8_t(id),draw);
                state.at(player_at(id)+29)=uint8_t(target);id=0;
            }
            if(old!=255&&state.at(team_at(old)+22)!=0)continue;
            if(!id||abroad>4)continue;
            break;
        }
        const int target=annual_cpu_target(state,uint8_t(id),draw);
        if(const char* pc=std::getenv("EM_DEBUG_PCLUB");pc&&std::atoi(pc)==id)std::fprintf(stderr,"pclub random-change %d\n",target);
        state.at(player_at(id)+29)=uint8_t(target);
    }
}
void annual_rebalance_clubs(Bytes& state,std::array<int,4> need,DistributionCursors& cursors,const ContractRandomDraw& draw){
    validate(state);for(auto cursor:cursors)if(cursor>=17)throw FormatError("annual distribution: invalid cursor");
    std::array<std::vector<uint8_t>,4> pool;
    for(size_t kind=0;kind<4;++kind)for(int id=1;id<180;++id){
        const size_t p=player_at(id);const int club=state.at(p+29);
        if(club<buckets[2*kind]||club>buckets[2*kind+1])continue;
        if(need[kind])--need[kind];
        if(state.at(p+26)>3)pool[kind].push_back(uint8_t(id));
    }
    std::array<int,4> remaining{};
    for(size_t i=0;i<4;++i)remaining[i]=int(pool[i].size());
    for(size_t dest=0;dest<4;++dest){
        int tries=0;
        while(need[dest]>0&&tries++<1000){
            for(size_t donor=0;donor<4;++donor){
                if(donor==dest)continue;
                while(remaining[donor]>0&&need[dest]>0){
                    int index=0,retries=0;
                    do{index=draw(0,int(pool[donor].size())-1);if(pool[donor][size_t(index)]!=255)break;}while(retries++<1000);
                    const uint8_t id=pool[donor][size_t(index)];pool[donor][size_t(index)]=255;
                    if(id<180)state.at(player_at(id)+29)=uint8_t(annual_cyclic_target(state,int(dest),cursors,draw));
                    if(const char* pc=std::getenv("EM_DEBUG_PCLUB");pc&&std::atoi(pc)==id)std::fprintf(stderr,"pclub rebalance %d\n",state.at(player_at(id)+29));
                    --remaining[donor];--need[dest];
                }
            }
        }
    }
}
void annual_distribute_players(Bytes& state,DistributionCursors& cursors,const ContractRandomDraw& draw){
    annual_random_club_changes(state,draw);
    const auto quotas=annual_distribution_quotas(state);
    annual_rebalance_clubs(state,quotas,cursors,draw);
}
} // namespace em
