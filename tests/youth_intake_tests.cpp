// SPDX-License-Identifier: GPL-3.0-only
#include "game/youth_intake.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
using namespace em;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
Bytes hex(const std::string& text){
    Bytes out;for(size_t i=0;i<text.size();i+=2)out.push_back(uint8_t(std::stoul(text.substr(i,2),nullptr,16)));return out;
}
}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("youth_intake_tests DATA REFERENCES VECTORS");
    const auto exe=read_file(std::filesystem::path(argv[1])/"EM.EXE");
    auto tables=load_contract_tables(exe);tables.level=2;
    const auto start=read_file(std::filesystem::path(argv[2])/"START.HOK");
    std::ifstream input(argv[3]);int count=0;input>>count;require(count==5,"five independent ASM cases");
    struct Draw{int low,high,value;};
    for(int test=0;test<count;++test){
        int refused=0,id=0,slot=0,reserved=0;size_t calls=0;
        std::string before,after,row;input>>refused>>id>>slot>>reserved>>calls>>before>>after>>row;
        std::vector<Draw> draws(calls);for(auto& d:draws)input>>d.low>>d.high>>d.value;
        require(bool(input),"complete external transcript");
        auto h=decode_hok(start);
        if(refused)for(size_t s=25;s<28;++s)h.state.at(0x696d+s*0x34+0x0f)=1;
        require(native_reserved_players(h,0)==reserved,"native reserved count including foreign storage");
        auto expected=h.state;
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
        size_t cursor=0;const auto result=youth_intake(h,tables,0,80,[&](int low,int high){
            require(cursor<draws.size(),"no additional RNG draw");const auto d=draws[cursor++];
            require(low==d.low&&high==d.high,"exact original RNG bounds and order");return d.value;
        });
        require(bool(result)==!refused,"capacity refusal or successful insertion");
        if(result)require(result->player==id&&result->slot==slot,"original player ID and insertion slot");
        require(cursor==draws.size()&&h.state==expected,"all47761 bytes and complete transcript match independent ASM golden");
        std::printf("intake ID%d: %zu draws, reserved%d\n",id,cursor,reserved);
    }
    for(int year:{1993,1994,1995})for(int attempts=0;attempts<=2;++attempts)for(int suppressed=0;suppressed<=1;++suppressed){
        int n=0;const int got=youth_review_attempts(year,[&](int low,int high){
            require(low==0&&high==(n==0?2:1),"annual attempt and suppression bounds");return n++==0?attempts:suppressed;
        });require(n==2&&got==(year>1994&&!suppressed?attempts:0),"year gate follows both RNG draws");
    }
    std::puts("Youth intake ASM transcript tests passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Youth intake: %s\n",e.what());return 1;}}
