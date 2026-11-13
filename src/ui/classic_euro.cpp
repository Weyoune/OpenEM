// SPDX-License-Identifier: GPL-3.0-only
// Four-team group pages of the classic UI: the European cup groups 1 and 2
// and the final round ("Endrunde"), and the two promotion groups
// ("Aufstieg"). All share one layout: six fixtures (three match days, first
// and second leg side by side, future games show their date) and the group
// table with points and goals.
#include "ui/classic_euro.hpp"
#include <algorithm>
#include <limits>
#include <string_view>

namespace em::ui {
namespace {
constexpr auto mini=ClassicFontId::Mini,flow=ClassicFontId::Flow;
// Save-state date fields: current date (three u32: day, month-1, year),
// current season day (u32) and the season's start year (u16).
constexpr size_t kDate=0x8e21,kDay=0x8e2d,kYear=0x8e31;
// Number padded on the left to width with the '%' digit-wide blank.
std::string number(int value,int width=2) {
    auto text=std::to_string(value);
    if (value>=0 && int(text.size())<width) text.insert(0,size_t(width-int(text.size())),'%');
    return text;
}
// Team name (game encoding) from the 61-byte team record at 0x17E7.
std::string name(const HokFile& h,int id) {
    if (id<0 || id>=150) throw FormatError("classic euro: invalid team ID");
    const size_t at=0x17e7+size_t(id)*61;
    std::string text;
    for (size_t i=0;i<22 && h.state.at(at+i);++i) text+=char(h.state.at(at+i));
    return text;
}
// Team record byte +0x16 is non-zero for a team run by a human manager.
bool human(const HokFile& h,int id) { return h.state.at(0x17e7+size_t(id)*61+0x16)!=0; }
// Season-day index of the ordinal-th (1-based) calendar event of the
// competition: European cup events have bit 0x20, promotion events 0x40.
int calendar_day(const SeasonCalendar& calendar,int ordinal,int kind) {
    // Days with combined events count too. The promotion round counts only
    // positions 0 and 2 of each five-event play-off block, whereas the
    // European cup counts every event with its bit.
    int position=0;const int mask=kind==1?0x40:0x20;
    for (size_t day=1;day<calendar.events.size();++day)
        if (calendar.events[day]&mask) {
            const bool eligible=kind!=1||position%5==0||position%5==2;++position;
            if(!eligible||--ordinal)continue;
            if (day>size_t(std::numeric_limits<int>::max()-252))
                throw FormatError("classic euro: calendar index outside date range");
            return int(day);
        }
    throw FormatError("classic euro: missing original calendar round");
}
// Maps a scheduled team to the group's participant list (four bytes at
// base): if it is one of the first three participants it is used as is,
// otherwise the game falls back to the fourth participant.
int member(const HokFile& h,size_t base,int scheduled) {
    for (size_t i=0;i<3;++i) if (h.state.at(base+i)==scheduled) return scheduled;
    return h.state.at(base+3);
}
// Stores a little-endian u32.
void put32(Bytes& s,size_t at,uint32_t value) {
    for (size_t i=0;i<4;++i) s.at(at+i)=uint8_t(value>>(8*i));
}
} // namespace

namespace {
// Fills fixtures and table of one group. Per kind (2 European cup groups,
// 3 final round, 1 promotion) and group the save state holds:
//   0x0394 + kind*8 + group*4   four participants
//   0x03B8 + kind*0x48 + group*0x24   schedule: 3 blocks of 6 bytes
//                                     (two matches of home/away team)
//   0x04D8 + same offset        scores: 6 rounds of 6 bytes (two matches of
//                               two goal bytes; the return leg reversed)
//   0x05F8 + kind*12 + group*6  table order (index 0..3 per rank)
//   0x0628 / 0x02BB / 0x02EB + same + index   points, goals for, against
// Minus points are 2*completed - points. Round r < completed is played.
ClassicEuroView populate_group(const HokFile& h,const SeasonCalendar& calendar,ClassicEuroView out) {
    if (!out.available) return out; // no fixtures, table or date lookup
    const size_t offset=size_t(out.kind)*0x48+size_t(out.group)*0x24;
    const size_t schedule=0x03b8+offset,scores=0x04d8+offset;
    const size_t participants=0x0394+size_t(out.kind)*8+size_t(out.group)*4;
    for (int block=0;block<3;++block) for (int match=0;match<2;++match) {
        auto& row=out.fixtures[size_t(block*2+match)];
        row.baseline=54+26*block+6*match;
        row.home=member(h,participants,h.state.at(schedule+size_t(block)*6+size_t(match)*2));
        row.away=member(h,participants,h.state.at(schedule+size_t(block)*6+size_t(match)*2+1));
        row.native_home=name(h,row.home); row.native_away=name(h,row.away);
        row.home_color=human(h,row.home)?24:30; row.away_color=human(h,row.away)?24:1;
        for (int column=0;column<2;++column) {
            auto& cell=row.cells[size_t(column)]; const int round=block+3*column;
            cell.played=round<out.completed;
            if (cell.played) {
                const size_t at=scores+size_t(round)*6+size_t(match)*2;
                cell.scores={h.state.at(at+size_t(column)),h.state.at(at+size_t(1-column))};
                cell.native=number(cell.scores[0])+":"+number(cell.scores[1]);
            } else if (match==0) {
                cell.calendar_index=calendar_day(calendar,round+1+(out.kind==3?6:0),out.kind);
                const auto date=date_of_season_day(cell.calendar_index,le16(h.state,kYear));
                out.last_preview_date=date;
                cell.native=number(date.day)+"."+number(date.month,1)+".";
            }
        }
    }
    const size_t stat=size_t(out.kind)*12+size_t(out.group)*6;
    for (size_t rank=0;rank<out.rows.size();++rank) {
        auto& row=out.rows[rank]; row.baseline=135+int(rank)*10;
        row.index=h.state.at(0x05f8+stat+rank);
        if (row.index>3) throw FormatError("classic euro: invalid saved table index");
        row.team=h.state.at(schedule+size_t(row.index));
        row.native_name=name(h,row.team); row.color=human(h,row.team)?24:30;
        const size_t index=stat+size_t(row.index);
        row.points=h.state.at(0x0628+index);
        row.minus_points=2*out.completed-row.points;
        row.goals_for=h.state.at(0x02bb+index); row.goals_against=h.state.at(0x02eb+index);
        row.native_stats=number(row.points)+":"+number(row.minus_points)+"   "+
                         number(row.goals_for)+":"+number(row.goals_against);
    }
    return out;
}
} //namespace
// The European cup phase byte at 0x8F86 (0..12) counts played rounds: the
// groups play rounds 1..6, the final round is available from phase 6 on and
// has played phase-6 rounds.
ClassicEuroView classic_euro_view(const HokFile& h,const SeasonCalendar& calendar,int page) {
    if (page<3 || page>5) throw FormatError("classic euro: page must be3,4 or5");
    ClassicEuroView out; out.page=page; out.kind=page==5?3:2; out.group=page==4?1:0;
    const int phase=h.state.at(0x8f86);
    if (phase>12) throw FormatError("classic euro: invalid native competition phase");
    out.completed=out.kind==2?std::min(phase,6):(phase>5?phase-6:phase);
    out.available=out.kind!=3 || phase>=6;
    out.native_title=out.kind==3?"Europacup Endrunde":"Europacup  Gruppe"+number(out.group+1);
    out.native_buttons[0]="Gruppe"+number(2-out.group);
    out.native_buttons[1]=out.kind==2?"Endrunde":"Gruppe"+number(1);
    out.button_pages=out.kind==2?std::array<int,2>{4-out.group,5}:std::array<int,2>{4,3};
    return populate_group(h,calendar,std::move(out));
}
// Promotion groups: completed rounds = 2 * (byte at 0x8F87) plus one for
// each of the thresholds "games > 0" and "games > 2" of the counter at
// 0x0259, at most 7. The group is hidden while the draw byte at 0x03B6 is
// still 255.
ClassicEuroView classic_promotion_view(const HokFile& h,const SeasonCalendar& calendar,int group){
    if(group<0||group>1)throw FormatError("classic promotion: group must be0 or1");
    ClassicEuroView out;out.kind=1;out.group=group;out.page=1;
    const int games=h.state.at(0x0259);
    out.completed=std::min(2*int(h.state.at(0x8f87))+int(games>0)+int(games>2),7);
    out.available=h.state.at(0x03b6)!=255;
    out.native_title="Aufstieg  "+number(group+1)+".Liga";
    out.native_buttons={"Liga","Liga"};out.button_pages={7,7};
    return populate_group(h,calendar,std::move(out));
}

namespace {
// Renders a group page: frame, two trophy icons (promotion or European
// cup), title, the two navigation buttons, fixtures and table.
void draw_group(ClassicCanvas& c,const ClassicEuroView& view) {
    c.background(22); c.frame(4,3,13,10);
    const int trophy=view.kind==1?0:44;
    c.blit(36,13,7,0,trophy,22,22); c.blit(36,286,7,0,trophy,22,22);
    c.frame(79,196,2,2); c.frame(193,196,2,2);
    c.text_native(ClassicFontId::NewBold,0,23,view.native_title,7,1,319);
    c.text_native(mini,79,217,view.native_buttons[0],7,-1,128);
    c.text_native(mini,193,217,view.native_buttons[1],7,-1,242);
    if (!view.available) {
        c.text_native(flow,0,100,"Die Paarungen stehen noch nicht fest...",30,0,320);
        return;
    }
    for (const auto& row:view.fixtures) {
        // The game paints future dates before the names and scores after
        // them; keep that order so long team names overlap the same way.
        for (size_t column=0;column<2;++column) {
            const auto& cell=row.cells[column];
            if (!cell.played) c.text_native(mini,230+int(column)*46,row.baseline,
                                           cell.native,column==0?30:1);
        }
        c.text_native(mini,25,row.baseline,row.native_home,row.home_color);
        c.text_native(mini,120,row.baseline,"-",7);
        c.text_native(mini,137,row.baseline,row.native_away,row.away_color);
        for (size_t column=0;column<2;++column) {
            const auto& cell=row.cells[column];
            if (cell.played) c.text_native(mini,226+int(column)*46,row.baseline,
                                          cell.native,column==0?30:1);
        }
    }
    for (size_t rank=0;rank<view.rows.size();++rank) {
        const auto& row=view.rows[rank];
        c.text_native(flow,39,row.baseline,number(int(rank)+1,1)+".  ",7);
        c.text_native(flow,57,row.baseline,row.native_name,row.color);
        c.text_native(flow,206,row.baseline,row.native_stats,row.color);
    }
}

// Side effect of the game's page: the date routine leaves the current-date
// fields set to the last future date it printed. Writes that date into the
// working copy (day, month-1, year at kDate) and HokFile::date, keeping the
// season-day counter. Returns false if no future date was shown.
bool prepare_group_working(HokFile& working,const ClassicEuroView& view) {
    if (!view.last_preview_date) return false;
    (void)working.state.at(kDate+15); //validate all destinations/counter before committing
    const int day_index=int(le32(working.state,kDay));
    const auto& date=*view.last_preview_date;
    put32(working.state,kDate,uint32_t(date.day));
    put32(working.state,kDate+4,uint32_t(date.month-1));
    put32(working.state,kDate+8,uint32_t(date.year));
    working.date={date.day,date.month,date.year,day_index};
    return true;
}
} //namespace
void draw_classic_euro(ClassicCanvas& c,const HokFile& h,const SeasonCalendar& calendar,int page){draw_group(c,classic_euro_view(h,calendar,page));}
void draw_classic_promotion(ClassicCanvas& c,const HokFile& h,const SeasonCalendar& calendar,int group){draw_group(c,classic_promotion_view(h,calendar,group));}
bool prepare_classic_euro_working(HokFile& h,const SeasonCalendar& calendar,int page){return prepare_group_working(h,classic_euro_view(h,calendar,page));}
bool prepare_classic_promotion_working(HokFile& h,const SeasonCalendar& calendar,int group){return prepare_group_working(h,classic_promotion_view(h,calendar,group));}
} // namespace em::ui
