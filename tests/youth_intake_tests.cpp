// SPDX-License-Identifier: GPL-3.0-only
// Tests for the youth intake at the season change (src/game/youth_intake.cpp). Five fixed cases
// are read from a vectors file worked out independently of this implementation; each starts from
// START.HOK and gives: refused flag, expected player ID, insertion slot, reserved-place count,
// number of random draws, the player record before/after and the new squad row (hex), then every
// draw as "low high value". youth_intake must request exactly these draws in this order and
// produce exactly the expected state. A second part checks youth_review_attempts (two draws
// always; juniors only from 1995 on and when not suppressed).
// Arguments: the game data folder, the reference folder with START.HOK and the vectors file.
#include "game/youth_intake.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
using namespace em;
namespace {
// Throws `text` as a test failure unless `value`.
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
// Decodes a string of hex digit pairs into bytes.
Bytes hex(const std::string& text){
    Bytes out;for(size_t i=0;i<text.size();i+=2)out.push_back(uint8_t(std::stoul(text.substr(i,2),nullptr,16)));return out;
}
}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("youth_intake_tests DATA REFERENCES VECTORS");
    const auto exe=read_file(std::filesystem::path(argv[1])/"EM.EXE");
    auto tables=load_contract_tables(exe);tables.level=2;
    const auto start=read_file(std::filesystem::path(argv[2])/"START.HOK");
    std::ifstream input(argv[3]);int count=0;input>>count;require(count==5,"five independent reference cases");
    struct Draw{int low,high,value;};
    for(int test=0;test<count;++test){
        int refused=0,id=0,slot=0,reserved=0;size_t calls=0;
        std::string before,after,row;input>>refused>>id>>slot>>reserved>>calls>>before>>after>>row;
        std::vector<Draw> draws(calls);for(auto& d:draws)input>>d.low>>d.high>>d.value;
        require(bool(input),"complete external transcript");
        auto h=decode_hok(start);
        // A refused case fills squad rows 25..27 so that the reserved-place limit is reached.
        if(refused)for(size_t s=25;s<28;++s)h.state.at(0x696d+s*0x34+0x0f)=1;
        require(native_reserved_players(h,0)==reserved,"reserved count including players held in other blocks");
        auto expected=h.state;
        // Expected state: the reused player record replaced, and the new row inserted at `slot`
        // (rows from there on move down one place if the slot is occupied).
        if(!refused){
            const size_t global=0x4fa5+size_t(id)*0x21;
            const auto original=hex(before);require(std::equal(original.begin(),original.end(),h.state.begin()+std::ptrdiff_t(global)),"unchanged original START global fixture");
            const auto globalAfter=hex(after),rowAfter=hex(row);
            require(globalAfter.size()==33&&rowAfter.size()==52,"complete fixed global and squad records");
            std::copy(globalAfter.begin(),globalAfter.end(),expected.begin()+std::ptrdiff_t(global));
            const size_t dest=0x696d+size_t(slot)*0x34;
            if(expected.at(dest+0x0f))std::memmove(expected.data()+dest+0x34,expected.data()+dest,size_t(29-slot)*0x34);
            std::copy(rowAfter.begin(),rowAfter.end(),expected.begin()+std::ptrdiff_t(dest));
        }
        // Intake for manager 0 with youth quality 80; the draw callback replays the transcript.
        size_t cursor=0;const auto result=youth_intake(h,tables,0,80,[&](int low,int high){
            require(cursor<draws.size(),"no additional RNG draw");const auto d=draws[cursor++];
            require(low==d.low&&high==d.high,"exact original RNG bounds and order");return d.value;
        });
        require(bool(result)==!refused,"capacity refusal or successful insertion");
        if(result)require(result->player==id&&result->slot==slot,"original player ID and insertion slot");
        require(cursor==draws.size()&&h.state==expected,"all47761 bytes and complete transcript match the independent golden");
        std::printf("intake ID%d: %zu draws, reserved%d\n",id,cursor,reserved);
    }
    // youth_review_attempts for every year/attempt/suppression combination.
    for(int year:{1993,1994,1995})for(int attempts=0;attempts<=2;++attempts)for(int suppressed=0;suppressed<=1;++suppressed){
        int n=0;const int got=youth_review_attempts(year,[&](int low,int high){
            require(low==0&&high==(n==0?2:1),"annual attempt and suppression bounds");return n++==0?attempts:suppressed;
        });require(n==2&&got==(year>1994&&!suppressed?attempts:0),"year gate follows both RNG draws");
    }
    std::puts("Youth intake transcript tests passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Youth intake: %s\n",e.what());return 1;}}
