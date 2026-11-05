// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_league_fixtures.hpp"
#include "ui/classic_team_schedule.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
using namespace em;using namespace em::ui;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("classic_league_fixtures_tests DATA REFERENCE");
    const std::filesystem::path data=argv[1],refs=argv[2];
    const auto exe=read_file(data/"EM.EXE");const auto tables=load_schedule_tables(exe);const auto calendar=load_season_calendar(exe);
    const auto start=decode_hok(read_file(refs/"START.HOK"));const auto month=decode_hok(read_file(refs/"MONAT1.HOK"));
    const auto before=month.state;const auto v=classic_league_fixtures(month,tables,calendar,0);
    require(month.state==before,"pure view must preserve every source byte");
    require(v.round==10&&v.next_round==10&&v.native_title=="Bundesliga   10.Spieltag  11.10.","literal native MONAT1 title");
    constexpr std::array<std::array<int,6>,6> expected{{{0,6,3,7,72,66},{7,1,4,6,71,68},{10,3,1,2,78,76},{8,2,10,9,67,65},{9,5,12,5,63,68},{4,11,8,11,74,61}}};
    constexpr std::array<const char*,6> home_stats{"%3. Platz, St{rke 72","%4. Platz, St{rke 71","%1. Platz, St{rke 78","10. Platz, St{rke 67","12. Platz, St{rke 63","%8. Platz, St{rke 74"};
    constexpr std::array<const char*,6> away_stats{"%7. Platz, St{rke 66","%6. Platz, St{rke 68","%2. Platz, St{rke 76","%9. Platz, St{rke 65","%5. Platz, St{rke 68","11. Platz, St{rke 61"};
    for(size_t i=0;i<6;++i){const auto& r=v.rows[i];const auto& e=expected[i];
        require(r.home==e[0]&&r.away==e[1]&&r.baseline==48+20*int(i),"independent MONAT1 pairing/baseline vector");
        require(r.native_score=="--:--","current round hides initialized scores");
        require(r.native_home_stats==home_stats[i]&&r.native_away_stats==away_stats[i],"all twelve independent cached rank/strength labels");
        require(r.home_color==(r.home==7?24:30)&&r.away_color==1,"native raw owner name colors");
    }
    require(v.rows[0].native_home_stats=="%3. Platz, St{rke 72"&&v.rows[0].native_away_stats=="%7. Platz, St{rke 66","native long rank/strength labels");
    for(int league=0;league<3;++league){const auto s=classic_league_fixtures(start,tables,calendar,league);
        require(s.round==1&&s.rows[0].home==league*12&&s.rows[0].away==league*12+1,"independent START league offsets");
        require(s.preview_date.day==10&&s.preview_date.month==9&&s.preview_date.year==1993,"START calendar bit lookup");
    }
    const auto past=classic_league_fixtures(month,tables,calendar,0,9);
    require(past.native_title=="Bundesliga   9.Spieltag  8.10."&&past.rows[0].home==5&&past.rows[0].away==0&&past.rows[0].native_score=="%3:%3","literal native ninth-round result");
    require(past.outgoing_numeric_width==2&&v.outgoing_numeric_width==1,"historical score formatter lifetime");
    require(classic_league_fixtures(month,tables,calendar,0,8,2).native_title.rfind("Bundesliga   %8.Spieltag  ",0)==0,"previous score width affects the next caption");
    auto signed_scores=clone_hok(month);signed_scores.state[0x0796+8*12]=255;signed_scores.state[0x0796+8*12+1]=130;
    require(classic_league_fixtures(signed_scores,tables,calendar,0,9).rows[0].native_score=="-1:-126","historical scores are signed bytes without marker decoding");
    auto ended=clone_hok(month);ended.state[0x8f80]=45;require(classic_league_fixtures(ended,tables,calendar,0).round==1,"ended season wraps initial selected round");
    require(classic_league_fixtures_hit(v,140,35).team==0&&classic_league_fixtures_hit(v,140,35).mode==0,"signed first-row division includes35");
    require(classic_league_fixtures_hit(v,141,59).team==0&&classic_league_fixtures_hit(v,141,59).mode==1,"independent140 mode boundary");
    require(classic_league_fixtures_hit(v,160,59).team==0&&classic_league_fixtures_hit(v,161,59).team==6,"independent160 team boundary");
    require(classic_league_fixtures_hit(v,4,60).team==7&&classic_league_fixtures_hit(v,3,60).action==ClassicLeagueAction::None,"main frame left boundary");
    require(classic_league_fixtures_hit(v,100,160).action==ClassicLeagueAction::None,"no seventh row");
    require(classic_league_fixtures_hit(v,30,180).action==ClassicLeagueAction::Previous&&classic_league_fixtures_hit(v,76,218).action==ClassicLeagueAction::Previous&&classic_league_fixtures_hit(v,77,218).action==ClassicLeagueAction::None,"arrow hit region is narrower than the frame");
    require(classic_league_fixtures_hit(v,188,218).action==ClassicLeagueAction::Competition,"competition inclusive frame");
    auto work=clone_hok(month);prepare_classic_league_fixtures_working(work,tables,calendar,0);
    require(work.state==month.state,"native MONAT1 entry changes no state");
    prepare_classic_league_fixtures_working(work,tables,calendar,0,11);
    const auto native=refs.parent_path()/"exp"/"day_20261103"/"league-monat1-next.hok";
    if(std::filesystem::exists(native))require(work.state==decode_hok(read_file(native)).state,"all47761 native future-round working bytes");
    require(work.date.day==15&&work.date.month==10&&work.date.year==1993,"explicit future preview date");
    finish_classic_league_fixtures_working(work,tables,0);
    require(std::equal(work.state.begin()+0x8dfd,work.state.begin()+0x8e21,month.state.begin()+0x8dfd)&&work.date.day==15,"exit restores pairings but retains preview date");
    normalize_classic_team_schedule_main_return(work);require(work.state==month.state,"main return restores every original state byte");
    auto bad=tables;bad.teams12[0][0]='z';const auto unchanged=work.state;bool rejected=false;
    try{prepare_classic_league_fixtures_working(work,bad,calendar,0,11);}catch(const FormatError&){rejected=true;}
    require(rejected&&work.state==unchanged,"malformed schedule rejection is atomic");
    rejected=false;try{prepare_classic_league_fixtures_working(work,tables,SeasonCalendar{},0,11);}catch(const FormatError&){rejected=true;}
    require(rejected&&work.state==unchanged,"missing calendar rejection is atomic");
    std::puts("Native league fixtures tests passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Classic fixtures: %s\n",e.what());return 1;}}
