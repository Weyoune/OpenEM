// SPDX-License-Identifier: GPL-3.0-only
#include "game/annual_distribution.hpp"
#include "game/annual_players.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
using namespace em;
namespace {
void require(bool v,const char* text){if(!v)throw std::runtime_error(text);}
size_t p(int id){return 0x4fa5+size_t(id)*33;}
size_t t(int id){return 0x17e7+size_t(id)*61;}
Bytes blank(){
    Bytes state(47761,0);
    for(int id=1;id<=199;++id){state[p(id)+29]=255;state[p(id)+26]=6;state[p(id)+21]=state[p(id)+22]=state[p(id)+23]=60;}
    for(int team=0;team<150;++team)std::fill_n(state.begin()+std::ptrdiff_t(t(team)+24),4,uint8_t(65));
    return state;
}
struct Draw{int low,high,value;};
struct Script {
    std::vector<Draw> calls;size_t cursor=0;
    int draw(int low,int high){require(cursor<calls.size(),"no additional allocation draw");const auto d=calls[cursor++];require(low==d.low&&high==d.high,"exact original allocator range and order");return d.value;}
    void done()const{require(cursor==calls.size(),"complete allocation transcript consumed");}
};
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("annual_distribution_tests REFERENCES");
    { // Independent script: skip managed0, reject equal distance2 at club1.
        auto state=blank();state[t(0)+22]=1;std::fill_n(state.begin()+std::ptrdiff_t(t(1)+24),4,uint8_t(68));
        Script script{{{0,42,0},{0,42,1},{2,10,2},{0,42,2},{2,10,2}}};
        const auto before=state;require(annual_cpu_target(state,1,[&](int l,int h){return script.draw(l,h);})==2,"strict quality comparison and managed-target skip");script.done();require(state==before,"target lookup is state neutral");
    }
    {
        auto state=blank();state[p(1)+21]=state[p(1)+22]=state[p(1)+23]=0;
        for(int team=0;team<43;++team)std::fill_n(state.begin()+std::ptrdiff_t(t(team)+24),4,uint8_t(255));
        int count=0;std::array<int,3> tiers{};
        require(annual_cpu_target(state,1,[&](int low,int high){++count;if(low==2){require(high==10||high==20||high==30,"quality tier bounds");++tiers[size_t(high/10-1)];}else require(low==0&&high==42,"candidate club range");return low;})==0,"candidate302 accepts CPU without a quality draw");
        require(count==603&&tiers==(std::array<int,3>{100,100,101}),"exact widening thresholds and fallback draw count");
    }
    {
        auto state=blank();state[t(0)+22]=1;DistributionCursors cursors{};
        require(annual_cyclic_target(state,0,cursors,[](int,int)->int{throw std::runtime_error("unexpected cyclic RNG");})==1&&cursors[0]==3,"duplicate managed slots advance shared weighted cursor");
        cursors={16,0,0};require(annual_cyclic_target(state,0,cursors,[](int,int){return 0;})==11&&cursors[0]==0,"17th cursor slot wraps exactly to0");
    }
    { // Signed16 SI wraps even when every previous candidate was managed.
        auto state=blank();state[t(0)+22]=1;int targets=0,quality=0;
        const int club=annual_cpu_target(state,1,[&](int low,int high){
            if(low==0){require(high==42,"wrapped target range");return ++targets<=32768?0:1;}
            require(low==2&&high==30,"signed16 wrap restores quality check at retained tier3");++quality;return 2;
        });
        require(club==1&&targets==32769&&quality==1,"native signed16 retry-counter wrap");
    }
    {
        auto state=blank();for(int id=1;id<=3;++id)state[p(id)+29]=0;state[t(12)+22]=1;
        auto expected=state;expected[p(1)+29]=expected[p(2)+29]=13;
        Script script{{{0,2,0},{0,2,0},{0,2,1}}};DistributionCursors cursors{};
        annual_rebalance_clubs(state,{0,2,0,0},cursors,[&](int l,int h){return script.draw(l,h);});script.done();
        require(state==expected&&cursors==(DistributionCursors{0,4,0}),"donor holes persist; original pool bound and shared target cursor");
    }
    {
        auto state=blank();state[p(1)+29]=state[p(2)+29]=0;auto expected=state;expected[p(1)+29]=12;
        int count=0;DistributionCursors cursors{};
        annual_rebalance_clubs(state,{0,2,0,0},cursors,[&](int l,int h){require(l==0&&h==1,"sentinel retries use original donor length");++count;return 0;});
        require(count==1002&&state==expected&&cursors[1]==1,"1001 failed sentinel retries consume demand without a second player write");
    }
    {
        auto state=blank();const auto before=state;DistributionCursors cursors{};
        annual_rebalance_clubs(state,{0,1,0,0},cursors,[](int,int)->int{throw std::runtime_error("empty donor RNG");});
        require(state==before&&cursors==DistributionCursors{},"no donors ends with unmet need, no writes or draws");
    }
    { // Original-controlled foreign excess case, extended with donors to prove
        // that quotas are recomputed AFTER foreign5->4, not before the moves.
        auto state=blank();for(int id=1;id<=5;++id)state[p(id)+29]=64;
        state[p(1)+26]=0;state[p(6)+29]=0;
        for(int id=7;id<=150;++id)state[p(id)+29]=12;
        state[0xdc6]=1;for(size_t slot=0;slot<25;++slot)state[0x696d+slot*52+15]=199;
        require(annual_distribution_quotas(state)==(std::array<int,4>{123,0,0,0}),"foreign5 pre-change quota");
        Script script{{{5,15,5},{1,179,1},{0,42,0},{2,10,2}}};
        for(int i=0;i<5;++i)script.calls.insert(script.calls.end(),{{1,179,6},{0,42,0},{2,10,2}});
        for(int index=0;index<127;++index)script.calls.push_back({0,143,index});
        auto expected=state;expected[p(1)+29]=0;
        constexpr int fixedCycle[]={0,0,1,1,2,2,3,3,4,4,5,6,7,8,9,10,11};
        for(int i=0;i<127;++i)expected[p(7+i)+29]=uint8_t(fixedCycle[i%17]);
        DistributionCursors cursors{};annual_distribute_players(state,cursors,[&](int l,int h){return script.draw(l,h);});script.done();
        require(state==expected&&cursors==(DistributionCursors{8,0,0}),"post-change quota129: two incumbents plus127 moves; only global club bytes change");
        require(annual_distribution_quotas(state)==(std::array<int,4>{129,0,0,0}),"actual foreign4 is recounted after all random switches");
    }
    { // 1238:13A1 as a whole: 398 skill draws (1238:1359), then rand(5,15) and
        // per change rand(1,179) plus the 1238:1838 target draws, nothing between.
        auto state=blank();Script script;
        for(int id=1;id<200;++id){script.calls.push_back({1,7,id==5?7:4});script.calls.push_back({1,7,id==5?1:4});}
        script.calls.push_back({5,15,5});
        for(int id:{5,9,179,9,1})script.calls.insert(script.calls.end(),{{1,179,id},{0,42,0},{2,10,2}});
        auto expected=state;expected[p(5)+21]=63;expected[p(5)+22]=57;
        for(int id:{5,9,179,1})expected[p(id)+29]=0;
        const auto run=[&](int l,int h){return script.draw(l,h);};
        annual_global_skills(state,run);DistributionCursors cursors{};annual_distribute_players(state,cursors,run);script.done();
        require(state==expected&&cursors==DistributionCursors{},"season-change order: skill pass, then player shift, no quota draws");
    }
    const std::filesystem::path refs=argv[1];
    for(const std::string name:{"START","SAISEND","NEUSAIS"}){
        const auto h=decode_hok(read_file(refs/(name+".HOK")));const auto quotas=annual_distribution_quotas(h.state);
        require(quotas==(name=="NEUSAIS"?std::array<int,4>{0,154,0,0}:std::array<int,4>{154,0,0,0}),"independent original saved manager/abroad quota fixtures");
    }
    std::puts("Annual CPU allocation: strict gates, retries, weighted cursors, foreign recount and full-byte goldens passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Annual distribution: %s\n",e.what());return 1;}}
