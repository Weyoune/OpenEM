// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_team_schedule.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace em;
using namespace em::ui;
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void put32(HokFile& h,size_t p,uint32_t value) {
    for (size_t i=0;i<4;++i) h.state.at(p+i)=uint8_t(value>>(8*i));
}
ScheduleTables tables() {
    //Independent original EXE03C0..0442 extraction, not generated pairings.
    ScheduleTables t;
    t.teams12={"abcdefghijkl","dafchejglibk","ahgbcjidkefl","eabilcdfkghj",
        "aljbcegdfhik","aclbdjeifgkh","jabekcglhdif","akfbcgldejhi",
        "fabcdelhgijk","aghbkdicjfel","iabdchfkeglj"};
    return t;
}
SeasonCalendar calendar() {
    SeasonCalendar c; c.events.assign(100,0);
    for (size_t i=1;i<=44;++i) c.events[i]=7;
    return c;
}
HokFile fixture() {
    HokFile h; h.state.assign(47761,0);
    h.state.at(0x8e31)=0xc9; h.state.at(0x8e32)=7;
    put32(h,0x8e21,10); put32(h,0x8e25,8); put32(h,0x8e29,1993); put32(h,0x8e2d,1);
    h.season_year=1993; h.date={10,9,1993,1};
    for (int league=0;league<3;++league) {
        h.state.at(0x8f80+size_t(league))=1; h.matchday[league]=1;
        for (size_t pair=0;pair<6;++pair) {
            const int home=league*12+int(pair)*2,away=home+1;
            h.pairings[size_t(league)][pair]={home,away};
            h.state.at(0x8dfd+size_t(league)*12+pair*2)=uint8_t(home);
            h.state.at(0x8dfd+size_t(league)*12+pair*2+1)=uint8_t(away);
        }
    }
    for (int id=0;id<150;++id) {
        const size_t at=0x17e7+size_t(id)*61;
        const auto name="T"+std::to_string(id);
        std::copy(name.begin(),name.end(),h.state.begin()+at);
        std::fill_n(h.state.begin()+at+0x18,6,uint8_t(80));
    }
    return h;
}
void raw_view_and_dates() {
    auto h=fixture(); const auto t=tables(); const auto c=calendar();
    const auto before=h.state;
    auto view=classic_team_schedule(h,t,c,4);
    require(view.available && view.league==0 && view.half==0 && view.native_round_button=="2.Runde ",
            "1B84..1CDB initial first half and exact trailing-space toggle label");
    require(view.rows[0].opponent==5 && view.rows[0].native_name=="T5" &&
            view.rows[0].native_place_strength=="%1. (80)" && view.rows[0].color==30,
            "original first pairing e/f and width2 place/strength marker");
    require(view.rows[0].cells[0].round==1 && view.rows[0].cells[1].round==12 &&
            view.rows[0].cells[0].native=="?:10.%9." && view.rows[0].cells[1].native=="?:21.%9." &&
            view.rows[0].cells[0].home && !view.rows[0].cells[1].home && view.rows[0].cells[1].color==1,
            "1F29..20A8 dates use native prefix,padding and independent home/away colors per column");
    require(view.rows[10].baseline==193 && view.rows[10].opponent==6 &&
            view.rows[10].cells[0].round==11 && view.rows[10].cells[1].round==22,
            "two cells share baseline; eleven rows113..193 cover1..11 and12..22");
    require(view.last_pairings[0].home==0 && view.last_pairings[0].away==8 &&
            view.last_pairings[5].home==9 && view.last_pairings[5].away==11,
            "settled page0 retains exact native round22 pairing work array");
    require(view.last_preview_date && view.last_preview_date->day==1 && view.last_preview_date->month==10,
            "synthetic ordinal22 =>252+22=274 =>October1");
    require(h.state==before,"raw view does not change input date,pairings or unrelated bytes");
    h.state.at(0x8f80)=23;
    view=classic_team_schedule(h,t,c,4);
    require(view.half==1 && view.native_round_button=="1.Runde " &&
            view.rows[0].cells[0].round==23 && view.rows[10].cells[1].round==44,
            "next23 starts second half,displaying23..33 and34..44");
    require(view.rows[0].opponent==5 && view.rows[0].color==30,"another22 rounds preserve static left column");
    require(classic_team_schedule(h,t,c,4,0).half==0,"explicit transient page selection overrides initial choice");
    h.state.at(0x8f80)=22;
    require(classic_team_schedule(h,t,c,4).half==0,"next22 remains first half");
    //1238:2349 includesFF in its bit test. Day0 is then passed to1271,
    //whose0 argument means current season day17 rather than calendar zero.
    auto with_end=c; with_end.events[0]=255; h.state.at(0x8f80)=1; put32(h,0x8e2d,17);
    view=classic_team_schedule(h,t,with_end,4);
    require(view.rows[0].cells[0].calendar_index==0 && view.rows[0].cells[0].native=="?:26.%9.",
            "bit-calendar includesFF and native0 date argument uses current season-day");
    //The original caller truncates the calendar search result to AL.
    SeasonCalendar late; late.events.assign(310,0);
    for (size_t i=0;i<44;++i) late.events[260+i]=7;
    view=classic_team_schedule(h,t,late,4);
    require(view.rows[0].cells[0].calendar_index==4 && view.rows[0].cells[0].native=="?:13.%9.",
            "1F35 AL truncation is preserved for a host calendar extending beyond255");
}
void scores_and_other_divisions() {
    auto h=fixture(); const auto t=tables(); const auto c=calendar();
    h.state.at(0x8f80)=3;
    h.state.at(0x0796+4)=2; h.state.at(0x0796+5)=5;
    h.state.at(0x0796+12+4)=6; h.state.at(0x0796+12+5)=1;
    auto view=classic_team_schedule(h,t,c,4);
    require(view.rows[0].cells[0].played && view.rows[0].cells[0].native=="%2:%5" &&
            view.rows[1].cells[0].played && view.rows[1].cells[0].native=="%1:%6" &&
            !view.rows[2].cells[0].played,
            "strict round<next score branch uses selected-team orientation and raw12-byte result stride");
    h.state.at(0x0796+4)=255; h.state.at(0x0796+5)=128;
    require(classic_team_schedule(h,t,c,4).rows[0].cells[0].native=="-1:-128",
            "1FEC/2051 sign-extend raw goals instead of substituting an unknown-score marker");
    h.state.at(0x8f81)=2;
    h.state.at(0x0796+0x210+4)=9; h.state.at(0x0796+0x210+5)=3;
    view=classic_team_schedule(h,t,c,16);
    require(view.league==1 && view.rows[0].opponent==17 && view.rows[0].cells[0].native=="%9:%3",
            "second division uses absolute team offset12 and result stride528");
    view=classic_team_schedule(h,t,c,28);
    require(view.league==2 && view.rows[0].opponent==29,"Oberliga uses absolute team offset24");
    view=classic_team_schedule(h,t,c,36);
    require(view.available && view.rows[0].opponent==-1 && !view.last_preview_date,
            "native team36..51 classification makes no invented league fixtures");
}
void explicit_working_lifetime() {
    auto h=fixture(); const auto t=tables(); const auto c=calendar();
    const auto canonical=h.state;
    require(prepare_classic_team_schedule_working(h,t,c,4),"working prepare available");
    require(h.date.day==1 && h.date.month==10 && h.date.year==1993 && h.date.day_index==1 &&
            le32(h.state,0x8e21)==1 && le32(h.state,0x8e25)==9 && le32(h.state,0x8e2d)==1,
            "prepare writes preview date while retaining season-day; struct/raw stay synchronized");
    require(h.pairings[0][0].away==8 && h.state.at(0x8dfe)==8,"pairing struct/raw sync to round22");
    require(finish_classic_team_schedule_working(h,t,4),"explicit finish available");
    require(h.pairings[0][0].home==0 && h.pairings[0][0].away==1 && h.date.month==10,
            "2198 exit restores next-round pairing while retaining preview date");
    normalize_classic_team_schedule_main_return(h);
    require(h.state==canonical && h.date.day==10 && h.date.month==9,
            "27F5 normalizes derived date and restores full canonical bytes after schedule");
    //Page changes have their own working-image prepare. After all44 rounds
    //are past, neither half performs a date calculation or date restoration.
    prepare_classic_team_schedule_working(h,t,c,4,1);
    require(h.date.day==23 && h.date.month==10,"synthetic page1 ends atordinal44=October23");
    h.state.at(0x8f80)=45; h.matchday[0]=45;
    const auto date_before=h.date;
    require(!classic_team_schedule(h,t,c,4).last_preview_date,"all past rounds yield no preview date");
    prepare_classic_team_schedule_working(h,t,c,4);
    finish_classic_team_schedule_working(h,t,4);
    require(h.date.day==date_before.day && h.date.month==date_before.month,
            "all-past draw/exit retain previous working date without synthetic normalization");
    h.state.at(0x8f80)=0;
    const auto unavailable=h.state;
    require(!classic_team_schedule(h,t,SeasonCalendar{},4).available &&
            !prepare_classic_team_schedule_working(h,t,SeasonCalendar{},4) &&
            !finish_classic_team_schedule_working(h,t,4) && h.state==unavailable,
            "next0 returns before calendar access and all mutations");
    put32(h,0x8e2d,2000);
    normalize_classic_team_schedule_main_return(h);
    require(h.date.day==9 && h.date.month==9 && h.date.year==1993 && h.date.day_index==2000,
            "4F6E:1295 special season-day2000 maps tooffset252 without changing counter");
}
void invalid_inputs() {
    auto h=fixture(); const auto t=tables(); const auto c=calendar();
    for (int team:{-1,52,150}) {
        bool rejected=false;
        try {(void)classic_team_schedule(h,t,c,team);} catch (const FormatError&) {rejected=true;}
        require(rejected,"reject team outside domestic schedule route");
    }
    for (int half:{-2,2}) {
        bool rejected=false;
        try {(void)classic_team_schedule(h,t,c,4,half);} catch (const FormatError&) {rejected=true;}
        require(rejected,"reject invented half values");
    }
    const auto before=h.state;
    bool rejected=false;
    try {prepare_classic_team_schedule_working(h,t,SeasonCalendar{},4);} catch (const FormatError&) {rejected=true;}
    require(rejected && h.state==before,"missing calendar event errors before working-image writes");
    auto broken=t; broken.teams12[3]="abc";
    rejected=false;
    try {prepare_classic_team_schedule_working(h,broken,c,4);} catch (const FormatError&) {rejected=true;}
    require(rejected && h.state==before,"invalid source table errors before working-image writes");
    h.state.resize(3); rejected=false;
    try {(void)classic_team_schedule(h,t,c,4);} catch (const FormatError&) {rejected=true;}
    require(rejected,"truncated native state is safely rejected");
}
void original_runtime(const std::filesystem::path& data,const std::filesystem::path& refs,
                      const std::filesystem::path& native) {
    const auto exe=read_file(data/"EM.EXE");
    const auto t=load_schedule_tables(exe); const auto c=load_season_calendar(exe);
    const auto source=std::filesystem::exists(native/"gui-h0-export.hok") ? native/"gui-h0-export.hok" : refs/"GH0.HOK";
    auto h=decode_hok(read_file(source));
    require(h.state.size()==47761,"native complete working image size");
    //Independent parent captured reporting7->135 before opening schedule.
    //This is the only unrelated change in all three capture companions.
    require(h.state.at(0x190f)==7,"original GH0 ESV crest/reporting byte changed");
    h.state.at(0x190f)=135;
    auto view=classic_team_schedule(h,t,c,4);
    require(view.rows[0].native_name=="KREFELDER EV" &&
            view.rows[0].native_place_strength=="%1. (70)" &&
            view.rows[0].cells[0].native=="?:10.%9." && view.rows[0].cells[1].native=="?:18.10." &&
            view.rows[10].cells[0].native=="?:15.10." && view.rows[10].cells[1].native=="?:%3.12.",
            "fresh native em021 screenshot and independent original calendar/string goldens");
    const auto canonical=h.state;
    ClassicCanvas canvas(data); canvas.rect(0,0,319,239,83);
    draw_classic_team_schedule(canvas,h,t,c,4);
    require(h.state==canonical,"drawing must not apply native temporary working-field writes");
    for (int y=0;y<240;++y) for (int x=0;x<320;++x)
        if (x<96 || x>304 || y<96 || y>229)
            require(canvas.pixels().at(x,y)==83,"body-only renderer preserves existing popup/title/crest/graph");
    require(prepare_classic_team_schedule_working(h,t,c,4),"original schedule prepare");
    require(h.state==decode_hok(read_file(native/"schedule-esv-first.hok")).state,
            "all47761 bytes must match independent native schedule-first RAM capture");
    require(h.date.day==3 && h.date.month==12 && h.date.year==1993 && h.date.day_index==1,
            "native temporary date3December and unchanged season-day1");
    if(std::filesystem::exists(native/"schedule-esv-second.hok")){
        prepare_classic_team_schedule_working(h,t,c,4,1);
        require(h.state==decode_hok(read_file(native/"schedule-esv-second.hok")).state,"all47761 bytes of independent native second-half capture");
        prepare_classic_team_schedule_working(h,t,c,4,0);
    }
    finish_classic_team_schedule_working(h,t,4);
    require(h.state==decode_hok(read_file(native/"schedule-esv-info-return.hok")).state,
            "all47761 bytes must match independent native Info-return RAM capture");
    normalize_classic_team_schedule_main_return(h);
    require(h.state==decode_hok(read_file(native/"schedule-esv-main-return.hok")).state && h.state==canonical,
            "all47761 bytes must match independent native main-return capture and source+reporting only");
    //Native no-op path also leaves pixels untouched.
    h.state.at(0x8f80)=0;
    const auto pixels=canvas.pixels().pixels;
    draw_classic_team_schedule(canvas,h,t,SeasonCalendar{},4);
    require(canvas.pixels().pixels==pixels,"next0 has no draw side effects");
}
} // namespace

int main(int argc,char** argv) {
    try {
        raw_view_and_dates(); scores_and_other_divisions(); explicit_working_lifetime(); invalid_inputs();
        if (argc>1) {
            const std::filesystem::path data=argv[1],refs=argc>2 ? argv[2] : argv[1];
            const auto native=argc>3 ? std::filesystem::path(argv[3]) : data.parent_path()/"exp/day_20261103";
            original_runtime(data,refs,native);
        }
        std::printf("Native classic team schedule tests passed\n"); return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr,"Classic team schedule: %s\n",e.what()); return 1;
    }
}
