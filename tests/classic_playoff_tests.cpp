// SPDX-License-Identifier: GPL-3.0-only
// View model of the classic play-off page (ui/classic_playoff.cpp): the bracket of quarterfinals, semifinals and
// final with up to five legs per series, the centred frame, the hidden preview date of the next play-off day and
// the explicit working date the page writes, the click areas of the club names, and the pages of real saves at
// several points of the play-offs.
//
// Usage: classic_playoff_tests [DATA [REFERENCE]] — with DATA (EM.EXE and graphics) and REFERENCE (START, MONAT1,
// POCHECK, POPOST, POSEMI1, POSEMIP and SAISEND .HOK) the real saves are checked as well.
//
// State fields used: 0x8E91 the play-off pairings (home, away; 0xFF = end), 0x03B6 the cursor of the current
// round in that list, 0x8F87 the stage (0 quarter, 1 semi, 2 final), 0x0259 the game of the series, 0x0205 the
// result history (two bytes per pairing, 14 bytes per leg; 100 ends a series; values above 30/60 carry flags).
#include "ui/classic_playoff.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

using namespace em;
using namespace em::ui;
namespace {
// Throws with `message` when `value` is false.
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
// Writes `value` little-endian as 4 bytes at state offset `at`.
void put32(HokFile& h,size_t at,uint32_t value) {
    for(size_t i=0;i<4;++i) h.state.at(at+i)=uint8_t(value>>(8*i));
}
// A state on 10.09.1993 without play-off pairings (cursor 0xFF) and clubs named "CLUB<n>".
HokFile fixture() {
    HokFile h; h.state.assign(47761,0);
    put32(h,0x8e21,10);put32(h,0x8e25,8);put32(h,0x8e29,1993);
    put32(h,0x8e2d,1);put32(h,0x8e31,1993);h.date={10,9,1993,1};
    h.state.at(0x3b6)=255;
    std::fill_n(h.state.begin()+0x8e91,32,uint8_t(255));
    for(int t=0;t<64;++t) {
        const auto text="CLUB"+std::to_string(t);
        std::copy(text.begin(),text.end(),h.state.begin()+0x17e7+size_t(t)*61);
    }
    return h;
}
// A calendar with one play-off day (event 0x40) on day 150 and the end of season (0xFF) on day 232.
SeasonCalendar calendar() {
    SeasonCalendar c;c.events.assign(233,0);c.events[150]=64;c.events[232]=255;return c;
}
// Sets stage, game and cursor and fills `count` pairings 0-1, 2-3, ... into the pairing list.
void bracket(HokFile& h,int stage,int game,int cursor,int count) {
    h.state.at(0x8f87)=uint8_t(stage);h.state.at(0x259)=uint8_t(game);h.state.at(0x3b6)=uint8_t(cursor);
    for(int p=0;p<count;++p) {
        h.state.at(0x8e91+size_t(p)*2)=uint8_t(2*p);
        h.state.at(0x8e92+size_t(p)*2)=uint8_t(2*p+1);
    }
}
// The empty bracket before the play-offs: placeholder frame, the hidden preview date, the Liga button and the
// working date written by prepare_classic_playoff_working.
void placeholder_and_explicit_date() {
    auto h=fixture();const auto c=calendar();const auto before=h.state;
    const auto v=classic_playoff_view(h,c);
    require(!v.available && v.rows.empty() && v.headings.empty() && v.pair_count==1 &&
            v.frame_top==70 && v.frame_rows==5 && v.native_title=="Play-Off Spiele",
            "empty bracket forces minimum count only for the native frame");
    require(v.preview_calendar_index==150 && v.preview_date && v.preview_date->day==6 &&
            v.preview_date->month==2 && v.preview_date->year==1994 && v.native_caption=="   6.2.",
            "date lookup precedes placeholder: even an unavailable bracket constructs hidden February6 preview");
    require(h.state==before && h.date.day==10,"raw view leaves canonical and derived dates untouched");
    require(classic_playoff_hit(v,136,201).action==ClassicPlayoffAction::Liga &&
            classic_playoff_hit(v,184,239).action==ClassicPlayoffAction::Liga &&
            classic_playoff_hit(v,135,201).action==ClassicPlayoffAction::None &&
            classic_playoff_hit(v,185,239).action==ClassicPlayoffAction::None &&
            classic_playoff_hit(v,136,200).action==ClassicPlayoffAction::None,
            "registered Liga frame includes native one-past paint edges");
    require(prepare_classic_playoff_working(h,c) && le32(h.state,0x8e21)==6 &&
            le32(h.state,0x8e25)==1 && le32(h.state,0x8e29)==1994 &&
            h.date.day==6 && h.date.month==2 && h.date.year==1994 && h.date.day_index==1 &&
            le32(h.state,0x8e2d)==1 && le16(h.state,0xb953)==0,
            "explicit action commits only derived working date, preserving season-day and cursor");
    for(size_t i=0;i<before.size();++i) if(h.state[i]!=before[i])
        require(i>=0x8e21 && i<0x8e2d,"no record, RNG or competition byte changes during prepare");
    h.state.at(0xb953)=210;const auto prepared=h.state;
    require(!prepare_classic_playoff_working(h,SeasonCalendar{}) && h.state==prepared,
            "cursor210 skips all date lookup and writes");
    h.state.at(0xb953)=209;auto mixed=c;mixed.events[209]=0x41;
    require(classic_playoff_view(h,mixed).preview_calendar_index==209,"209 limit is inclusive and combined events count");
    mixed.events[209]=255;
    require(classic_playoff_view(h,mixed).preview_calendar_index==209,"FF satisfies shifted calendar mask");
    const auto no_write=h.state;bool rejected=false;
    try {(void)prepare_classic_playoff_working(h,SeasonCalendar{});} catch(const FormatError&) {rejected=true;}
    require(rejected && h.state==no_write,"missing future event fails before date mutation");
}
// Result legs of the quarterfinals, highlight of the managers' clubs and the click areas.
void native_legs_and_hits() {
    auto h=fixture();h.state.at(0xb953)=210;bracket(h,0,3,0,4);
    // Edge cases of the original's decoding: the right flag overwrites the left flag 2 with 1;
    // 100 ends this tie even if later bytes contain a plausible score.
    h.state.at(0x205)=62;h.state.at(0x206)=31;
    h.state.at(0x205+14)=100;h.state.at(0x206+14)=0;
    h.state.at(0x205+28)=9;h.state.at(0x206+28)=8;
    h.state.at(0xdc6)=1;h.state.at(0xdcd+0x14)=1;
    h.state.at(0x17e7+2*61+0x16)=1; //owner alone must not highlight CLUB2
    const auto before=h.state;auto v=classic_playoff_view(h,SeasonCalendar{});
    require(v.frame_top==70 && v.frame_rows==5 && v.rows.size()==4 &&
            v.headings.size()==1 && v.headings[0].native=="VIERTELFINALE" && v.headings[0].baseline==116 &&
            v.rows[0].baseline==123 && v.rows[3].baseline==144,
            "four saved quarterfinal ties retain flattened order and seven-pixel rows");
    require(v.rows[0].native_home=="CLUB0" && v.rows[0].native_away=="CLUB1" &&
            v.rows[0].home_color==30 && v.rows[0].away_color==24 && v.rows[1].home_color==30,
            "type2 highlight follows human-manager club list instead of owner byte");
    require(v.rows[0].scores.size()==1 && v.rows[0].scores[0].native=="%2:%1" &&
            v.rows[0].scores[0].markers==1 && v.rows[0].scores[0].x==187,
            "native overwrite flag and early100 termination preserve opaque raw history orientation");
    require(v.rows[1].scores.size()==3 && v.rows[1].scores[0].x==187 &&
            v.rows[1].scores[1].x==213 && v.rows[1].scores[2].x==239 &&
            v.rows[1].scores[0].color==1 && v.rows[1].scores[1].color==30 && v.rows[1].scores[2].color==1,
            "Play-off columns are fixed26 apart, not Cup game-dependent spacing");
    require(classic_playoff_hit(v,99,118).team==0 && classic_playoff_hit(v,99,118).mode==0 &&
            classic_playoff_hit(v,100,123).team==1 && classic_playoff_hit(v,100,123).mode==1 &&
            classic_playoff_hit(v,319,123).team==1 &&
            classic_playoff_hit(v,99,117).action==ClassicPlayoffAction::None &&
            classic_playoff_hit(v,100,124).action==ClassicPlayoffAction::None,
            "six accepted row pixels, one-pixel gap and99/100 side split apply across score columns");
    require(h.state==before,"all raw fields and RNG bytes remain unchanged by view/hit lookups");
    put32(h,0x8e2d,218);h.state.at(0x259)=0;
    require(classic_playoff_view(h,SeasonCalendar{}).rows[1].scores.empty(),
            "season-day218 Cup inclusion exception never applies to Play-off type2");
}
// Earlier rounds stay above the current one; the complete bracket of seven ties; flag decoding of large values
// and rejection of invalid states.
void old_rounds_and_final() {
    auto h=fixture();h.state.at(0xb953)=210;bracket(h,1,0,8,6);
    auto v=classic_playoff_view(h,SeasonCalendar{});
    require(v.frame_top==61 && v.frame_rows==6 && v.headings.size()==2 &&
            v.headings[0].baseline==107 && v.headings[1].baseline==142 &&
            v.rows[0].baseline==114 && v.rows[3].baseline==135 &&
            v.rows[4].baseline==149 && v.rows[5].baseline==156,
            "POSEMI1 history layout keeps quarters before the current semifinal block");
    require(v.rows[0].scores.size()==5 && v.rows[3].scores.size()==5 &&
            v.rows[4].scores.empty() && v.rows[5].scores.empty(),
            "old pairs inspect all five stored legs while current G0 pairs inspect none");
    bracket(h,2,5,12,7);v=classic_playoff_view(h,SeasonCalendar{});
    require(v.frame_top==51 && v.frame_rows==7 && v.rows.size()==7 && v.headings.size()==3 &&
            v.headings[0].baseline==97 && v.headings[1].baseline==132 &&
            v.headings[2].baseline==153 && v.headings[2].native=="ENDSPIEL" &&
            v.rows[6].baseline==160 && v.rows[6].scores.size()==5 &&
            v.rows[6].scores[4].x==291 && v.rows[6].scores[4].color==1,
            "SAISEND complete7-tie bracket and fifth leg position are literal original vectors");
    h.state.at(0x205)=90;h.state.at(0x206)=0;
    require(classic_playoff_view(h,SeasonCalendar{}).rows[0].scores[0].native=="30:%0" &&
            classic_playoff_view(h,SeasonCalendar{}).rows[0].scores[0].markers==2,
            "flag decoder subtracts at most60 even for raw90");
    auto broken=fixture();broken.state.resize(5);bool rejected=false;
    try {(void)classic_playoff_view(broken,SeasonCalendar{});} catch(const FormatError&) {rejected=true;}
    require(rejected,"truncated HOK is a safe format error");
    broken=fixture();broken.state=h.state;broken.state.at(0x8e92)=255;rejected=false;
    try {(void)classic_playoff_view(broken,SeasonCalendar{});} catch(const FormatError&) {rejected=true;}
    require(rejected,"incomplete native click-array row has no fabricated modal target");
}
// The page for real saves of the original before, during and after the play-offs.
void originals(const std::filesystem::path& data,const std::filesystem::path& refs) {
    const auto c=load_season_calendar(read_file(data/"EM.EXE"));
    for(const char* file:{"START.HOK","MONAT1.HOK"}) {
        auto h=decode_hok(read_file(refs/file));const auto before=h.state;
        const auto v=classic_playoff_view(h,c);
        require(!v.available && v.frame_top==70 && v.frame_rows==5 && v.preview_calendar_index==150 &&
                v.native_caption=="   6.2.","original early-season data matches independent invisible preview/frame vector");
        ClassicCanvas canvas(data);draw_classic_playoff(canvas,h,c);
        require(h.state==before,"original page drawing does not change any HOK/RNG byte");
    }
    const auto poc=classic_playoff_view(decode_hok(read_file(refs/"POCHECK.HOK")),c);
    require(poc.game==2 && poc.rows[2].home==5 && poc.rows[2].away==7 &&
            poc.rows[2].away_color==24 && poc.rows[2].baseline==137 &&
            poc.rows[2].scores.size()==2 && poc.rows[2].scores[1].native=="%2:%4" &&
            poc.rows[2].scores[1].x==213 && poc.rows[2].scores[1].markers==1 &&
            poc.preview_calendar_index==154 && poc.native_caption=="   10.2.",
            "POCHECK independent raw32:4 and current draw5/7 show one marker at235,137");
    const auto pop=classic_playoff_view(decode_hok(read_file(refs/"POPOST.HOK")),c);
    require(pop.rows[1].home==10 && pop.rows[1].away==4 && pop.rows[1].scores.size()==3 &&
            pop.rows[1].scores[2].native=="%3:%2" && pop.rows[1].scores[2].x==239 &&
            pop.rows[1].scores[2].markers==1 && pop.preview_calendar_index==157,
            "POPOST independent33:2 vector retains already swapped orientation");
    const auto semi=classic_playoff_view(decode_hok(read_file(refs/"POSEMI1.HOK")),c);
    require(semi.stage==1 && semi.game==0 && semi.cursor==8 && semi.rows[0].scores.size()==5 &&
            semi.rows[4].home==3 && semi.rows[4].away==7 && semi.rows[4].scores.empty() &&
            semi.rows[5].home==0 && semi.rows[5].away==10 && semi.rows[5].scores.empty(),
            "POSEMI1 independent seven-day history distinguishes old five-leg rows from newG0 semis");
    const auto semip=classic_playoff_view(decode_hok(read_file(refs/"POSEMIP.HOK")),c);
    require(semip.rows[4].home==7 && semip.rows[4].away==3 && semip.rows[4].scores.size()==1 &&
            semip.rows[4].scores[0].native=="%2:%5" && semip.rows[4].baseline==149,
            "POSEMIP native swapped semifinal leg0 remains2:5 without reverse-leg inference");
    auto h=decode_hok(read_file(refs/"SAISEND.HOK"));const auto end=classic_playoff_view(h,c);
    require(end.rows.size()==7 && end.frame_top==51 && end.frame_rows==7 &&
            !end.preview_date && end.native_caption.empty() && end.rows[6].home==0 && end.rows[6].away==10 &&
            end.rows[6].baseline==160 && end.rows[6].scores.size()==5 &&
            end.rows[6].scores[4].native=="%4:%2" && end.rows[6].scores[4].x==291 &&
            end.rows[3].scores[2].native=="%5:%8" && end.rows[3].scores[2].markers==2,
            "SAISEND independent final4:2, quarterraw5:68 and end-of-season missing caption");
    const auto before=h.state;
    require(!prepare_classic_playoff_working(h,c) && h.state==before,"original cursor216 leaves working date untouched");
}
} // namespace
int main(int argc,char** argv) {
    try {
        placeholder_and_explicit_date();native_legs_and_hits();old_rounds_and_final();
        if(argc>1) originals(argv[1],argc>2?std::filesystem::path(argv[2]):std::filesystem::path(argv[1]));
        std::puts("Native classic Play-off raw-view tests passed");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"Classic Play-off: %s\n",e.what());return 1;}
}
