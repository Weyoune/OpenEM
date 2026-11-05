// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_euro.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace em;
using namespace em::ui;
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void put32(HokFile& h,size_t at,uint32_t value) {
    for (size_t i=0;i<4;++i) h.state.at(at+i)=uint8_t(value>>(8*i));
}
HokFile fixture() {
    HokFile h; h.state.assign(47761,0);
    h.state.at(0x8e31)=0xc9; h.state.at(0x8e32)=7;
    put32(h,0x8e21,10); put32(h,0x8e25,8); put32(h,0x8e29,1993); put32(h,0x8e2d,1);
    h.date={10,9,1993,1};
    constexpr int pairs[6][4]={{0,1,2,3},{2,0,3,1},{0,3,1,2},
                              {1,0,3,2},{0,2,1,3},{3,0,2,1}};
    for (int kind=2;kind<=3;++kind) for (int group=0;group<(kind==3?1:2);++group) {
        const int first=60+(kind-2)*8+group*4;
        const size_t stat=size_t(kind)*12+size_t(group)*6;
        const size_t schedule=0x03b8+size_t(kind)*0x48+size_t(group)*0x24;
        for (int i=0;i<4;++i) {
            h.state.at(0x0394+size_t(kind)*8+size_t(group)*4+size_t(i))=uint8_t(first+i);
            h.state.at(0x05f8+stat+size_t(i))=uint8_t(i);
        }
        for (int round=0;round<6;++round) for (int i=0;i<4;++i)
            h.state.at(schedule+size_t(round)*6+size_t(i))=uint8_t(first+pairs[round][i]);
    }
    for (int team=0;team<150;++team) {
        const auto text="TEAM"+std::to_string(team);
        std::copy(text.begin(),text.end(),h.state.begin()+0x17e7+size_t(team)*61);
    }
    return h;
}
SeasonCalendar calendar() {
    SeasonCalendar c; c.events.assign(16,0);
    c.events[0]=0x20; //native group scan starts at1
    for (size_t i=1;i<=12;++i) c.events[i]=0x20;
    c.events[1]=0x21; c.events[2]=0xff; //bit tests,not event equality
    return c;
}
void raw_dates_and_pages() {
    auto h=fixture(); const auto c=calendar(); const auto before=h.state;
    const auto group=classic_euro_view(h,c);
    require(group.page==3 && group.kind==2 && group.group==0 && group.completed==0,
            "initial explicit page3 is first group with zero completed rounds");
    require(group.native_title=="Europacup  Gruppe%1" &&
            group.native_buttons==std::array<std::string,2>{"Gruppe%2","Endrunde"} &&
            group.button_pages==std::array<int,2>{4,5},"native title padding and footer destinations");
    require(group.fixtures[0].home==60 && group.fixtures[0].away==61 &&
            group.fixtures[0].baseline==54 && group.fixtures[5].baseline==112,
            "first3 schedule rounds define the6 fixed name rows");
    require(group.fixtures[0].cells[0].calendar_index==1 &&
            group.fixtures[0].cells[1].calendar_index==4 &&
            group.fixtures[0].cells[0].native=="10.9." &&
            group.fixtures[1].cells[0].native.empty(),
            "calendar lookup ignores index0,accepts combined/FF masks,and dates only first match row");
    require(group.last_preview_date && group.last_preview_date->day==15 &&
            group.last_preview_date->month==9,"last group preview is6th calendar event");
    for (size_t i=0;i<4;++i) require(group.rows[i].index==int(i),
            "view does not re-sort equal initial table values");
    const auto second=classic_euro_view(h,c,4);
    require(second.group==1 && second.fixtures[0].home==64 &&
            second.button_pages==std::array<int,2>{3,5},"second group uses its own raw36byte block");
    const auto final=classic_euro_view(h,SeasonCalendar{},5);
    require(!final.available && !final.last_preview_date &&
            final.native_title=="Europacup Endrunde" && final.button_pages==std::array<int,2>{4,3},
            "unavailable final needs neither tables nor calendar and retains navigation");
    require(h.state==before,"raw view is entirely read-only");
    h.state.at(0x8f86)=6;
    const auto ready=classic_euro_view(h,c,5);
    require(ready.available && ready.completed==0 && ready.fixtures[0].home==68 &&
            ready.fixtures[0].cells[0].calendar_index==7 && ready.last_preview_date->day==21,
            "phase6 makes final available and offsets future calendar ordinals by6");
}
void unsigned_scores_order_and_colors() {
    auto h=fixture(); h.state.at(0x8f86)=7;
    constexpr size_t results=0x04d8+2*0x48,stat=2*12;
    h.state.at(results)=255; h.state.at(results+1)=128;
    h.state.at(results+3*6)=7; h.state.at(results+3*6+1)=9;
    constexpr std::array<uint8_t,4> order{2,0,3,1};
    std::copy(order.begin(),order.end(),h.state.begin()+0x05f8+stat);
    h.state.at(0x0628+stat+2)=255; h.state.at(0x02bb+stat+2)=200;
    h.state.at(0x02eb+stat+2)=150;
    h.state.at(0x17e7+61*61+0x16)=6; //any human owner,not active manager
    h.state.at(0x17e7+62*61+0x16)=2;
    const auto before=h.state;
    const auto view=classic_euro_view(h,SeasonCalendar{},3);
    require(view.completed==6 && !view.last_preview_date,"completed groups do not scan future calendar");
    require(view.fixtures[0].cells[0].scores==std::array<int,2>{255,128} &&
            view.fixtures[0].cells[0].native=="255:128",
            "27A5 displays unsigned scores directly without signed/sentinel/overtime decoding");
    require(view.fixtures[0].cells[1].scores==std::array<int,2>{9,7} &&
            view.fixtures[0].cells[1].native=="%9:%7","return leg reverses the stored away/home bytes");
    require(view.fixtures[0].home_color==30 && view.fixtures[0].away_color==24 &&
            view.fixtures[1].home_color==24 && view.fixtures[1].away_color==1,
            "fixture own colors are30/1 with all nonzero owners highlighted24");
    require(view.rows[0].team==62 && view.rows[0].color==24 &&
            view.rows[0].points==255 && view.rows[0].minus_points==-243 &&
            view.rows[0].native_stats=="255:-243   200:150",
            "saved permutation and raw unsigned stat bytes retained;minus points use completed games");
    for (size_t rank=0;rank<4;++rank) require(view.rows[rank].index==order[rank],
            "cached ordering is never recalculated when rendering");
    require(h.state==before,"raw view leaves score,order and owner bytes unchanged");
    h.state.at(0x8f86)=7;
    const auto final=classic_euro_view(h,calendar(),5);
    require(final.completed==1 && final.fixtures[0].cells[0].played &&
            !final.fixtures[2].cells[0].played && !final.fixtures[0].cells[1].played,
            "phase7 has exactly one final round played");
}
void working_date_only() {
    auto h=fixture(); const auto c=calendar(); const auto before=h.state;
    require(prepare_classic_euro_working(h,c,3),"future group commits its last preview date explicitly");
    require(le32(h.state,0x8e21)==15 && le32(h.state,0x8e25)==8 && le32(h.state,0x8e29)==1993 &&
            h.date.day==15 && h.date.month==9 && h.date.year==1993 && h.date.day_index==1,
            "native working date and HokDate model agree");
    for (size_t i=0;i<before.size();++i) if (before[i]!=h.state[i])
        require(i>=0x8e21 && i<0x8e2d,"only derived day/month/year may change");
    const auto preview=h.state;
    require(!prepare_classic_euro_working(h,SeasonCalendar{},5) && h.state==preview,
            "empty final neither resets nor replaces existing working preview date");
    h.state.at(0x8f86)=12; const auto complete=h.state;
    require(!prepare_classic_euro_working(h,SeasonCalendar{},5) && h.state==complete,
            "completed final has no native date lookup/write");
    bool rejected=false;
    try {(void)classic_euro_view(h,c,2);} catch(const FormatError&) {rejected=true;}
    require(rejected,"reject unsupported competition dispatcher pages");
    h.state.at(0x8f86)=0; const auto no_calendar=h.state; rejected=false;
    try {(void)prepare_classic_euro_working(h,SeasonCalendar{},3);} catch(const FormatError&) {rejected=true;}
    require(rejected && h.state==no_calendar,"failed date lookup is atomic before working-date mutation");
}
using TableVector=std::array<std::array<int,5>,4>;
void table_vector(const ClassicEuroView& view,const TableVector& expected) {
    for (size_t i=0;i<4;++i) {
        const auto& row=view.rows[i];
        require(std::array<int,5>{row.team,row.points,row.minus_points,row.goals_for,row.goals_against}==expected[i],
                "table differs from independently extracted original HOK/data.json vector");
    }
}
void original_references(const std::filesystem::path& data,const std::filesystem::path& refs) {
    const auto c=load_season_calendar(read_file(data/"EM.EXE"));
    auto h=decode_hok(read_file(refs/"MONAT1.HOK"));
    const auto before=h.state; const auto first=classic_euro_view(h,c,3);
    table_vector(first,{{{82,0,0,0,0},{129,0,0,0,0},{126,0,0,0,0},{96,0,0,0,0}}});
    table_vector(classic_euro_view(h,c,4),{{{81,0,0,0,0},{83,0,0,0,0},{134,0,0,0,0},{3,0,0,0,0}}});
    constexpr std::array<int,6> days{56,59,61,63,66,68};
    constexpr std::array<const char*,6> dates{"%4.11.","%7.11.","%9.11.","11.11.","14.11.","16.11."};
    for (size_t block=0;block<3;++block) for (size_t column=0;column<2;++column) {
        const auto& cell=first.fixtures[block*2].cells[column]; const size_t i=block+column*3;
        require(cell.calendar_index==days[i] && cell.native==dates[i],"original group dates match native4/7/9 and11/14/16November");
    }
    require(!classic_euro_view(h,c,5).available,"MONAT1 phase0 final is unavailable");
    ClassicCanvas canvas(data);
    for (int page=3;page<=5;++page) draw_classic_euro(canvas,h,c,page);
    require(h.state==before,"painting all3 original pages consumes no HOK writes");
    h=decode_hok(read_file(refs/"MONAT2.HOK"));
    const auto seventh=classic_euro_view(h,c,5);
    table_vector(seventh,{{{126,2,0,4,2},{83,2,0,3,2},{81,0,2,2,3},{129,0,2,2,4}}});
    require(seventh.fixtures[0].cells[0].native=="%4:%2" && seventh.last_preview_date->day==30 &&
            seventh.last_preview_date->month==11,"phase7 final score and last future date independently extracted");
    h=decode_hok(read_file(refs/"SAISEND.HOK"));
    const auto final=classic_euro_view(h,c,5);
    table_vector(final,{{{83,10,2,25,16},{81,8,4,24,17},{126,4,8,17,24},{129,2,10,13,22}}});
    require(!final.last_preview_date && final.fixtures[0].cells[1].native=="%2:%5",
            "phase12 final shows reverse-leg score and never writes a future date");
}
} // namespace
int main(int argc,char** argv) {
    try {
        raw_dates_and_pages(); unsigned_scores_order_and_colors(); working_date_only();
        if (argc>1) original_references(argv[1],argc>2?std::filesystem::path(argv[2]):std::filesystem::path(argv[1]));
        std::puts("Native classic EuroCup raw-view tests passed"); return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"Classic EuroCup: %s\n",e.what()); return 1;
    }
}
