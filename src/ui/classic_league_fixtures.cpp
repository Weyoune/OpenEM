// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_league_fixtures.hpp"
#include <algorithm>
#include <limits>

namespace em::ui {
namespace {
std::string number(int value,int width=2){
    auto s=std::to_string(value);if(value>=0&&int(s.size())<width)s.insert(0,size_t(width-int(s.size())),'%');return s;
}
void validate_league(int league){if(league<0||league>2)throw FormatError("classic fixtures: invalid league");}
int next_round(const HokFile& h,int league){
    validate_league(league);const int n=int(static_cast<int8_t>(h.state.at(0x8f80+size_t(league))));
    if(n<1)throw FormatError("classic fixtures: invalid native next round");return n;
}
std::array<HokPairing,6> pairings(const ScheduleTables& tables,int league,int round){
    validate_league(league);const auto& source=tables.for_size(12);
    if(source.size()!=11||round<1||round>127)throw FormatError("classic fixtures: invalid round table");
    for(const auto& row:source){
        if(row.size()!=12)throw FormatError("classic fixtures: truncated round row");
        std::array<bool,12> seen{};
        for(unsigned char c:row){if(c<'a'||c>'l'||seen[size_t(c-'a')])throw FormatError("classic fixtures: invalid pairing letter");seen[size_t(c-'a')]=true;}
    }
    const auto generated=round_robin_pairings(source,round-1);std::array<HokPairing,6> out{};
    for(size_t i=0;i<out.size();++i)out[i]={generated[i].home+league*12,generated[i].away+league*12};return out;
}
GameDate preview_date(const HokFile& h,const SeasonCalendar& calendar,int league,int round){
    size_t day=0;int remaining=round;
    for(;day<calendar.events.size();++day)if((calendar.events[day]&(1<<league))&&!--remaining)break;
    if(day==calendar.events.size())throw FormatError("classic fixtures: missing calendar round");
    uint32_t arg=day?uint32_t(day):2000u; //1238:2349 returns0 for the first calendar slot.
    if(arg==2000)arg=0;
    if(arg>uint32_t(std::numeric_limits<int>::max()-252))throw FormatError("classic fixtures: date outside host range");
    return date_of_season_day(int(arg),le16(h.state,0x8e31));
}
std::string name(const HokFile& h,int team){
    const size_t at=0x17e7+size_t(team)*61;std::string s;
    for(size_t i=0;i<22&&h.state.at(at+i);++i)s+=char(h.state.at(at+i));return s;
}
std::string stats(const HokFile& h,int team){
    const size_t at=0x17e7+size_t(team)*61;int strength=0;
    for(size_t i=0;i<6;++i)strength+=h.state.at(at+0x18+i);
    return number(int(h.state.at(0x3bd6+size_t(team)*80))+1)+". Platz, St{rke "+number(strength/6);
}
void set_pairings(HokFile& h,int league,const std::array<HokPairing,6>& pairs){
    const size_t at=0x8dfd+size_t(league)*12;(void)h.state.at(at+11);
    for(size_t i=0;i<pairs.size();++i){h.state[at+i*2]=uint8_t(pairs[i].home);h.state[at+i*2+1]=uint8_t(pairs[i].away);}
    h.pairings[size_t(league)]=pairs;
}
}
ClassicLeagueFixtures classic_league_fixtures(const HokFile& h,const ScheduleTables& tables,
    const SeasonCalendar& calendar,int league,int round,int width){
    ClassicLeagueFixtures out;out.league=league;out.next_round=next_round(h,league);
    if(width<1||width>255)throw FormatError("classic fixtures: invalid numeric width");
    out.round=round<0?(out.next_round>44?1:out.next_round):round;
    if(out.round<1||out.round>44)throw FormatError("classic fixtures: display round outside1..44");
    out.pairings=pairings(tables,league,out.round);out.preview_date=preview_date(h,calendar,league,out.round);
    constexpr std::array<const char*,3> labels{"Bundesliga","Zweite Liga","Oberliga"};
    out.native_title=std::string(labels[size_t(league)])+"   "+number(out.round,width)+".Spieltag  "+
        number(out.preview_date.day,1)+"."+number(out.preview_date.month,1)+".";
    out.native_competition=league?"Aufstieg":"Play-Off";
    const bool played=out.round<out.next_round;out.outgoing_numeric_width=played?2:1;
    for(size_t i=0;i<out.rows.size();++i){
        auto& row=out.rows[i];row.home=out.pairings[i].home;row.away=out.pairings[i].away;row.baseline=48+int(i)*20;
        row.native_home=name(h,row.home);row.native_away=name(h,row.away);
        row.home_color=h.state.at(0x17e7+size_t(row.home)*61+0x16)?24:30;
        row.away_color=h.state.at(0x17e7+size_t(row.away)*61+0x16)?24:1;
        row.native_home_stats=stats(h,row.home);row.native_away_stats=stats(h,row.away);
        const size_t score=0x0796+size_t(league)*0x210+size_t(out.round-1)*12+i*2;
        row.native_score=played?number(int(static_cast<int8_t>(h.state.at(score))))+":"+
            number(int(static_cast<int8_t>(h.state.at(score+1)))):"--:--";
    }
    return out;
}
namespace {
void draw_view(ClassicCanvas& c,const ClassicLeagueFixtures& view,bool live){
    constexpr auto mini=ClassicFontId::Mini,flow=ClassicFontId::Flow;
    c.background(22);c.frame(4,15,13,8);
    if(!live){  // mode 2 (during the matches) has no buttons
        c.frame(30,180,2,2);c.frame(250,180,2,2);c.frame(140,180,2,2);
        c.text_native(mini,147,200,view.native_competition,1,-1,182);
        c.blit(4,40,195,19,104,29,9);c.blit(4,260,195,19,125,29,9);
    }
    c.line(13,34,303,34,7);c.line(14,35,304,35,1);c.rect(8,23,310,33,4);
    c.text_native(flow,5,30,view.native_title,10,28,313);c.rect(12,35,306,157,4);
    for(const auto& row:view.rows){
        c.text_native(flow,0,row.baseline,row.native_home,row.home_color,-1,160);
        c.text_native(flow,156,row.baseline,"-",7);
        c.text_native(flow,160,row.baseline,row.native_away,row.away_color,-1,319);
        c.text_native(mini,0,row.baseline+7,row.native_home_stats,8,-1,160);
        c.text_native(mini,160,row.baseline+7,row.native_away_stats,8,-1,319);
        c.text_native(flow,0,row.baseline+9,row.native_score,7,-1,319);
    }
}
}
void draw_classic_league_fixtures(ClassicCanvas& c,const HokFile& h,const ScheduleTables& tables,
    const SeasonCalendar& calendar,int league,int round,int width){
    draw_view(c,classic_league_fixtures(h,tables,calendar,league,round,width),false);
}
void draw_classic_league_live(ClassicCanvas& c,const HokFile& h,const ScheduleTables& tables,const SeasonCalendar& calendar,
    int league,int minute,const std::vector<std::array<int,4>>& scores){
    auto view=classic_league_fixtures(h,tables,calendar,league);
    // Amiga FUN_0010e62e mode 2: the title ends with the minute instead of the date, the running scores of the round
    constexpr std::array<const char*,3> labels{"Bundesliga","Zweite Liga","Oberliga"};
    view.native_title=std::string(labels[size_t(league)])+"   "+number(view.round,1)+".Spieltag  "+number(minute,3)+".MINUTE";
    for(auto& row:view.rows)
        for(const auto& s:scores)if(s[0]==row.home&&s[1]==row.away)row.native_score=number(s[2])+":"+number(s[3]);
    draw_view(c,view,true);
}
ClassicLeagueHit classic_league_fixtures_hit(const ClassicLeagueFixtures& v,int x,int y){
    if(y>=180&&y<=218){
        if(x>=30&&x<=76)return {ClassicLeagueAction::Previous};
        if(x>=250&&x<=296)return {ClassicLeagueAction::Next};
        if(x>=140&&x<=188)return {ClassicLeagueAction::Competition};
    }
    if(x>=4&&x<=316&&y>=35&&y<=167){
        const int i=(y-40)/20; //native signed IDIV truncates toward zero.
        if(i>=0&&i<6){const auto& row=v.rows[size_t(i)];return {ClassicLeagueAction::ClubInfo,x>160?row.away:row.home,x>140?1:0};}
    }
    return {};
}
void prepare_classic_league_fixtures_working(HokFile& h,const ScheduleTables& tables,
    const SeasonCalendar& calendar,int league,int round,int width){
    const auto view=classic_league_fixtures(h,tables,calendar,league,round,width);
    (void)h.state.at(0x8e2c);(void)h.state.at(0x8dfd+size_t(league)*12+11);
    set_pairings(h,league,view.pairings);
    const uint32_t fields[]={uint32_t(view.preview_date.day),uint32_t(view.preview_date.month-1),uint32_t(view.preview_date.year)};
    for(size_t i=0;i<3;++i)for(size_t b=0;b<4;++b)h.state[0x8e21+i*4+b]=uint8_t(fields[i]>>(b*8));
    h.date.day=view.preview_date.day;h.date.month=view.preview_date.month;h.date.year=view.preview_date.year;
}
void finish_classic_league_fixtures_working(HokFile& h,const ScheduleTables& tables,int league){
    const auto pairs=pairings(tables,league,next_round(h,league));set_pairings(h,league,pairs);
}
} //namespace em::ui
