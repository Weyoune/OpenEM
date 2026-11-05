// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_team_schedule.hpp"

#include <algorithm>
#include <limits>
#include <string_view>

namespace em::ui {
namespace {
constexpr auto mini = ClassicFontId::Mini;
constexpr size_t kPairings=0x8dfd,kDate=0x8e21,kDay=0x8e2d,kYear=0x8e31,kNext=0x8f80;

const uint8_t* bytes(const HokFile& h,size_t at,size_t count) {
    if (at>h.state.size() || count>h.state.size()-at)
        throw FormatError("classic team schedule: truncated native state");
    return h.state.data()+at;
}
int league_of(int team) {
    if (team<0 || team>=52) throw FormatError("classic team schedule: invalid domestic team");
    return team<12 ? 0 : team<24 ? 1 : 2; //4996:5AA5 /2
}
std::string number(int value,int width=2) {
    auto result=std::to_string(value);
    if (value>=0 && int(result.size())<width) result.insert(0,size_t(width-int(result.size())),'%');
    return result;
}
std::array<HokPairing,6> pairings(const ScheduleTables& tables,int league,int round) {
    const auto& source=tables.for_size(12);
    if (source.size()!=11 || round<1 || round>255)
        throw FormatError("classic team schedule: invalid native round table");
    //The original EXE table is trusted by DOS. Reject malformed host inputs
    //before the shared zero-based helper could index beyond a source row.
    for (const auto& row:source) {
        if (row.size()!=12) throw FormatError("classic team schedule: truncated round row");
        std::array<bool,12> seen{};
        for (unsigned char c:row) {
            if (c<'a' || c>'l' || seen[size_t(c-'a')])
                throw FormatError("classic team schedule: invalid pairing letter");
            seen[size_t(c-'a')]=true;
        }
    }
    const auto generated=round_robin_pairings(source,round-1);
    std::array<HokPairing,6> out{};
    for (size_t i=0;i<out.size();++i)
        out[i]={generated[i].home+league*12,generated[i].away+league*12};
    return out;
}
int calendar_day(const SeasonCalendar& calendar,int mask,int round) {
    //1238:2349 tests a BIT, includingFF. Calendar::nth_day/league_day have
    //different semantics and must not substitute for this exact lookup.
    for (size_t day=0;day<calendar.events.size();++day)
        if ((calendar.events[day]&mask) && --round==0)
            return int(static_cast<uint8_t>(day)); //1F35 deliberately stores AL
    throw FormatError("classic team schedule: calendar has no requested league round");
}
GameDate native_date(const HokFile& h,uint32_t argument) {
    uint32_t day=argument ? argument : le32(h.state,kDay);
    if (day==2000) day=0; //4F6E:1295..12AA special sentinel
    if (day>uint32_t(std::numeric_limits<int>::max()-252))
        throw FormatError("classic team schedule: season day outside host date range");
    return date_of_season_day(int(day),le16(h.state,kYear));
}
std::string raw_name(const uint8_t* team) {
    size_t count=0; while (count<22 && team[count]) ++count;
    return {reinterpret_cast<const char*>(team),count};
}
void put32(Bytes& state,size_t at,uint32_t value) {
    for (size_t i=0;i<4;++i) state[at+i]=uint8_t(value>>(i*8));
}
void set_date(HokFile& h,const GameDate& date) {
    (void)bytes(h,kDate,16);
    put32(h.state,kDate,uint32_t(date.day));
    put32(h.state,kDate+4,uint32_t(date.month-1));
    put32(h.state,kDate+8,uint32_t(date.year));
    h.date.day=date.day; h.date.month=date.month; h.date.year=date.year;
    h.date.day_index=int(le32(h.state,kDay)); //counter itself is never written
}
void set_pairings(HokFile& h,int league,const std::array<HokPairing,6>& pairs) {
    const size_t at=kPairings+size_t(league)*12;
    (void)bytes(h,at,12);
    for (size_t i=0;i<pairs.size();++i) {
        h.state[at+i*2]=uint8_t(pairs[i].home);
        h.state[at+i*2+1]=uint8_t(pairs[i].away);
    }
    h.pairings[size_t(league)]=pairs;
}
void button(ClassicCanvas& c,int x,std::string_view text) {
    constexpr int y=215;
    const std::array<uint8_t,4> corner{c.pixels().at(x,y),c.pixels().at(x+56,y),
                                     c.pixels().at(x,y+11),c.pixels().at(x+56,y+11)};
    c.blit(4,x,y,17,179,57,12,true,192);
    c.pixels().at(x,y)=corner[0]; c.pixels().at(x+56,y)=corner[1];
    c.pixels().at(x,y+11)=corner[2]; c.pixels().at(x+56,y+11)=corner[3];
    c.text_native(mini,x,y+7,text,29,1,x+59);
}
} // namespace

ClassicTeamSchedule classic_team_schedule(const HokFile& h,const ScheduleTables& tables,
    const SeasonCalendar& calendar,int team,int half) {
    if (half < -1 || half>1) throw FormatError("classic team schedule: invalid half");
    ClassicTeamSchedule out;
    out.league=league_of(team);
    const int next=*bytes(h,kNext+size_t(out.league),1);
    if (!next) return out; //1B65..1B70: no paint, calendar or pairing access
    out.available=true;
    out.half=half<0 ? int(next>22) : half;
    out.native_round_button=out.half ? "1.Runde " : "2.Runde ";
    const int first=1+out.half*22;
    int y=113;
    for (size_t row=0;row<out.rows.size();++row) {
        auto& target=out.rows[row]; target.baseline=y;
        int matched=0;
        for (size_t column=0;column<2;++column) {
            const int round=first+int(row)+int(column)*11;
            const auto pairs=pairings(tables,out.league,round);
            out.last_pairings=pairs;
            for (size_t i=0;i<pairs.size();++i) {
                const auto& pair=pairs[i];
                if (pair.home!=team && pair.away!=team) continue;
                ++matched;
                auto& cell=target.cells[column];
                cell.round=round; cell.home=pair.home==team;
                cell.color=cell.home ? 30 : 1;
                if (!column) {
                    target.opponent=cell.home ? pair.away : pair.home;
                    target.color=cell.color;
                    const auto* opponent=bytes(h,0x17e7+size_t(target.opponent)*61,61);
                    target.native_name=raw_name(opponent);
                    const int position=*bytes(h,0x3bd6+size_t(target.opponent)*80,1)+1;
                    int strength=0; for (size_t k=0;k<6;++k) strength+=opponent[0x18+k];
                    target.native_place_strength=number(position)+". ("+number(strength/6)+")";
                }
                cell.played=round<next;
                if (cell.played) {
                    const auto* score=bytes(h,0x0796+size_t(out.league)*0x210+size_t(round-1)*12+i*2,2);
                    const int own=int(static_cast<int8_t>(score[cell.home ? 0 : 1]));
                    const int other=int(static_cast<int8_t>(score[cell.home ? 1 : 0]));
                    cell.native=number(own)+":"+number(other);
                } else {
                    cell.calendar_index=calendar_day(calendar,1<<out.league,round);
                    const auto date=native_date(h,uint32_t(cell.calendar_index));
                    out.last_preview_date=date;
                    cell.native="?:"+number(date.day)+"."+number(date.month)+".";
                }
                //The original increments SI only after the second found cell.
                //Teams36..51 are outside the generated36 clubs and yield no rows.
                if (matched>1) y+=8;
            }
        }
    }
    return out;
}

void draw_classic_team_schedule(ClassicCanvas& c,const HokFile& h,const ScheduleTables& tables,
    const SeasonCalendar& calendar,int team,int half) {
    const auto view=classic_team_schedule(h,tables,calendar,team,half);
    if (!view.available) return;
    c.rect(96,96,301,229,4);
    button(c,103,"Reicht mir"); button(c,173,"Info");
    //Exact first-pass/page-change clear extends3px beyond the body rectangle.
    c.rect(230,105,304,229,4);
    button(c,243,view.native_round_button);
    c.text_native(mini,108,104,"GEGNER",1);
    c.text_native(mini,194,104,"PL.(ST)",1);
    c.text_native(mini,268,104,"DATUM/ERGEBNIS",1,-1,268);
    for (const auto& row:view.rows) {
        if (row.opponent<0) continue;
        c.text_native(mini,98,row.baseline,row.native_name,row.color);
        c.text_native(mini,194,row.baseline,row.native_place_strength,row.color);
        for (size_t column=0;column<2;++column) {
            const auto& cell=row.cells[column];
            const int center=250+int(column)*37;
            c.text_native(mini,center,row.baseline,cell.native,cell.color,-1,center);
        }
    }
}

bool prepare_classic_team_schedule_working(HokFile& working,const ScheduleTables& tables,
    const SeasonCalendar& calendar,int team,int half) {
    const auto view=classic_team_schedule(working,tables,calendar,team,half);
    if (!view.available) return false;
    //Validate all destinations before committing either native working field.
    (void)bytes(working,kPairings+size_t(view.league)*12,12);
    if (view.last_preview_date) (void)bytes(working,kDate,16);
    set_pairings(working,view.league,view.last_pairings);
    if (view.last_preview_date) set_date(working,*view.last_preview_date);
    return true;
}

bool finish_classic_team_schedule_working(HokFile& working,const ScheduleTables& tables,int team) {
    const int league=league_of(team);
    const int next=*bytes(working,kNext+size_t(league),1);
    if (!next) return false;
    const auto current=pairings(tables,league,next);
    set_pairings(working,league,current);
    return true;
}

void normalize_classic_team_schedule_main_return(HokFile& working) {
    (void)bytes(working,kDay,6);
    const auto date=native_date(working,0);
    set_date(working,date);
}

} // namespace em::ui
