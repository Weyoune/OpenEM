// SPDX-License-Identifier: GPL-3.0-only
// Tests for the development of the youth teams (src/game/youth_progress.cpp). Seven fixed cases
// for youth_fixture_day are read from a vectors file worked out independently of this
// implementation: a quoted name, the three monthly grants, quality, table rank and league of
// the three age groups, the first matchday, the nine expected bytes afterwards (quality, rank,
// league per group) and the expected random draws as "low high value". The rest of the state is
// filled with 0xA5 to show nothing else is written. A second part checks youth_new_season
// (cohort mixing, promotion/relegation, default rank 5).
// Youth block of manager 0 at state 0x06B8: +0..+2 quality, +3..+5 table rank, +6..+8 league,
// +9.. three i32 grants.
// Arguments: the game data folder (EM.EXE) and the vectors file.
#include "game/youth_progress.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <cstdio>
#include <stdexcept>
using namespace em;
// Throws `s` as a test failure unless `v`.
void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("youth_progress_tests DATA VECTORS");
    const std::filesystem::path data=argv[1];
    const auto finance=load_finance_tables(read_file(data/"EM.EXE"));
    std::ifstream input(argv[2]);int cases=0;input>>cases;
    require(cases==7,"seven independently worked-out state/RNG cases");
    struct Draw{int low,high,value;};
    for(int test=0;test<cases;++test){
        std::string name;input>>std::quoted(name);
        std::array<int32_t,3> funding{};std::array<int,3> quality{},rank{},league{};int matchday=0;
        for(auto& n:funding)input>>n;for(auto& n:quality)input>>n;for(auto& n:rank)input>>n;for(auto& n:league)input>>n;input>>matchday;
        std::array<int,9> expected{};for(auto& n:expected)input>>n;
        size_t count=0;input>>count;std::vector<Draw> draws(count);for(auto& d:draws)input>>d.low>>d.high>>d.value;
        require(bool(input),"complete fixed transcript");
        Bytes state(47761,0xa5);const size_t at=0x6b8;
        for(size_t i=0;i<3;++i){state[at+i]=uint8_t(quality[i]);state[at+3+i]=uint8_t(rank[i]);state[at+6+i]=uint8_t(league[i]);
            for(int b=0;b<4;++b)state[at+9+i*4+size_t(b)]=uint8_t(uint32_t(funding[i])>>(8*b));}
        auto expected_state=state;
        for(size_t i=0;i<3;++i){expected_state[at+i]=uint8_t(expected[i*3]);expected_state[at+3+i]=uint8_t(expected[i*3+1]);expected_state[at+6+i]=uint8_t(expected[i*3+2]);}
        size_t cursor=0;
        youth_fixture_day(state,finance,matchday,0,[&](int low,int high){
            require(cursor<draws.size(),"no extra RNG draw");const auto& d=draws[cursor++];
            require(std::min(low,high)==d.low&&std::max(low,high)==d.high,"exact original RNG draw order and inclusive bounds");return d.value;
        });
        require(cursor==draws.size()&&state==expected_state,"all state bytes and complete draw transcript match the reference");
        std::printf("%s: %zu native draw bounds\n",name.c_str(),cursor);
    }
    // Season end: qualities 30/60/90, ranks 0/11/5 in leagues 1/1/2.
    Bytes state(47761,0xa5);size_t at=0x6b8;
    state[at]=30;state[at+1]=60;state[at+2]=90;
    state[at+3]=0;state[at+4]=11;state[at+5]=5;
    state[at+6]=1;state[at+7]=1;state[at+8]=2;
    auto expected=state;
    const std::array<uint8_t,9> rolled{30,50,80,10,1,5,0,2,2};
    std::copy(rolled.begin(),rolled.end(),expected.begin()+std::ptrdiff_t(at));
    youth_new_season(state,0);require(state==expected,"reverse cohort mixing preserves old previous-group quality; promotion/relegation/default place");
    state[at+3]=0;state[at+4]=11;state[at+5]=1;state[at+6]=0;state[at+7]=2;state[at+8]=1;
    youth_new_season(state,0);
    require(state[at+3]==5&&state[at+4]==5&&state[at+5]==11&&state[at+6]==0&&state[at+7]==2&&state[at+8]==0,"top/bottom league boundaries and second promotion rank");
    std::puts("Youth progress transcript tests passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Youth progress: %s\n",e.what());return 1;}}
