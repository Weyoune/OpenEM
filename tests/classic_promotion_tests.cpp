// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_euro.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
using namespace em;using namespace em::ui;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("classic_promotion_tests DATA REFERENCE");
    const std::filesystem::path data=argv[1],refs=argv[2];
    const auto calendar=load_season_calendar(read_file(data/"EM.EXE"));
    auto month=decode_hok(read_file(refs/"MONAT1.HOK"));const auto original=month.state;
    for(int group=0;group<2;++group){
        const auto v=classic_promotion_view(month,SeasonCalendar{},group);
        require(!v.available&&v.kind==1&&v.group==group&&v.native_title==std::string("Aufstieg  %")+char('1'+group)+".Liga","literal original empty promotion title");
        require(v.native_buttons==std::array<std::string,2>{"Liga","Liga"}&&v.button_pages==std::array<int,2>{7,7},"both native footer controls return to regular league");
        require(!v.last_preview_date&&!prepare_classic_promotion_working(month,SeasonCalendar{},group)&&month.state==original,"unavailable promotion never looks up or writes a hidden date");
    }
    const auto h=decode_hok(read_file(refs/"POCHECK.HOK"));const auto before=h.state;
    const auto v=classic_promotion_view(h,calendar,0);
    require(h.state==before&&v.available&&v.completed==1,"independent POCHECK group phase and pure view");
    require(v.fixtures[0].home==8&&v.fixtures[0].away==2&&v.fixtures[0].cells[0].native=="%7:%1","original first promotion fixture and result");
    require(v.fixtures[1].home==12&&v.fixtures[1].away==19&&v.fixtures[1].cells[0].native=="11:%4","original second promotion fixture and result");
    constexpr std::array<int,4> teams{12,8,2,19};
    constexpr std::array<const char*,4> stats{"%2:%0   11:%4","%2:%0   %7:%1","%0:%2   %1:%7","%0:%2   %4:11"};
    for(size_t i=0;i<4;++i)require(v.rows[i].team==teams[i]&&v.rows[i].native_stats==stats[i],"saved group order and all four independent table rows");
    require(v.fixtures[0].cells[1].calendar_index==169&&v.fixtures[2].cells[0].calendar_index==154&&v.fixtures[2].cells[1].calendar_index==194&&v.fixtures[4].cells[0].calendar_index==164&&v.fixtures[4].cells[1].calendar_index==201,"native promotion calendar skips Play-off-only legs");
    require(v.last_preview_date&&v.last_preview_date->day==29&&v.last_preview_date->month==3&&v.last_preview_date->year==1994,"last native group preview date");
    const auto second=classic_promotion_view(h,calendar,1);
    require(second.fixtures[0].home==22&&second.fixtures[0].away==15&&second.fixtures[0].cells[0].native=="%6:%3"&&second.rows[0].team==22,"independent second promotion group");
    auto unsigned_scores=clone_hok(h);unsigned_scores.state[0x0520]=255;unsigned_scores.state[0x0521]=130;
    require(classic_promotion_view(unsigned_scores,calendar,0).fixtures[0].cells[0].native=="255:130","group scores remain raw unsigned bytes");
    unsigned_scores.state[0x8f87]=2;unsigned_scores.state[0x0259]=5;unsigned_scores.state[0x0520+18]=61;unsigned_scores.state[0x0521+18]=32;
    const auto complete=classic_promotion_view(unsigned_scores,SeasonCalendar{},0);
    require(complete.completed==6&&complete.fixtures[0].cells[1].native=="32:61"&&!complete.last_preview_date,"return leg reverses raw scores without flags or calendar lookup");
    auto synthetic=clone_hok(h);synthetic.state[0x8f87]=0;synthetic.state[0x0259]=0;
    SeasonCalendar bits;bits.events.assign(14,0x40);bits.events[1]=255;
    const auto selected=classic_promotion_view(synthetic,bits,0);
    require(selected.fixtures[0].cells[0].calendar_index==1&&selected.fixtures[2].cells[0].calendar_index==3&&selected.fixtures[4].cells[0].calendar_index==6&&selected.fixtures[0].cells[1].calendar_index==8&&selected.fixtures[2].cells[1].calendar_index==11&&selected.fixtures[4].cells[1].calendar_index==13,"qualifying event positions0/2, includingFF, starting at calendar1");
    auto work=clone_hok(h);require(prepare_classic_promotion_working(work,calendar,0),"explicit promotion date action");
    for(size_t i=0;i<work.state.size();++i)if(i<0x8e21||i>=0x8e2d)require(work.state[i]==h.state[i],"working action writes only derived twelve-byte date range");
    require(work.date.day==29&&work.date.month==3&&work.date.year==1994,"working date model agrees with raw preview");
    const auto unchanged=work.state;bool rejected=false;
    try{prepare_classic_promotion_working(work,SeasonCalendar{},0);}catch(const FormatError&){rejected=true;}
    require(rejected&&work.state==unchanged,"missing calendar rejection is atomic");
    const auto end=decode_hok(read_file(refs/"SAISEND.HOK"));const auto final=classic_promotion_view(end,SeasonCalendar{},0);
    require(final.completed==6&&final.rows[0].team==12&&final.rows[0].points==11&&final.rows[0].minus_points==1&&final.rows[0].goals_for==20&&final.rows[0].goals_against==13,"independent SAISEND final table vector");
    std::puts("Native promotion groups tests passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Classic promotion: %s\n",e.what());return 1;}}
