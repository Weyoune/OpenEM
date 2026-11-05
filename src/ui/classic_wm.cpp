// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_wm.hpp"
#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace em::ui {
namespace {
constexpr auto bold=ClassicFontId::NewBold,flow=ClassicFontId::Flow,mini=ClassicFontId::Mini;
constexpr std::array<int,7> heading_at{1,5,7,9,10,11,12}; //EXE DS4E06
constexpr std::array<std::string_view,7> heading_text{
    "VIERTELFINALE","HALBFINALE","SPIELE 5.-8.","ENDSPIEL",
    "SPIEL UM 3.PL.","SPIEL UM 5.PL.","SPIEL UM 7.PL."};
std::string number(int value,int width=2) {
    auto s=std::to_string(value);
    if(value>=0 && int(s.size())<width) s.insert(0,size_t(width-int(s.size())),'%');
    return s;
}
size_t team_at(int id) {
    if(id<0 || id>=150) throw FormatError("classic WM: invalid team ID");
    return 0x17e7+size_t(id)*61;
}
std::string name(const HokFile& h,int id) {
    const auto at=team_at(id); std::string out;
    for(size_t i=0;i<22 && h.state.at(at+i);++i) out+=char(h.state.at(at+i));
    return out;
}
int badge(const HokFile& h,int id) {
    if(id<52 || id>63) throw FormatError("classic WM: team outside native flag atlas");
    return 80+(h.state.at(team_at(id)+0x34)&63);
}
ClassicWmHit hit(ClassicWmAction action,int l,int t,int r,int b,
                 int team=-1,int page=-1,int round=-1) {
    return {action,team,team<0?-1:2,page,round,l,t,r,b};
}
bool contains(const ClassicWmHit& h,int x,int y) {
    return x>=h.left && x<=h.right && y>=h.top && y<=h.bottom;
}
void preview_date(ClassicWmView& out,const HokFile& h,const SeasonCalendar& calendar) {
    const int cursor=le16(h.state,0xb953);
    if(cursor>209) return; //DS4E01, checked even for a pre-WM placeholder
    size_t day=size_t(cursor);
    while(day<calendar.events.size() && !((calendar.events[day]>>1)&8)) ++day;
    if(day>=calendar.events.size()) throw FormatError("classic WM: missing future WM calendar event");
    if(day>size_t(std::numeric_limits<int>::max()-252))
        throw FormatError("classic WM: calendar date outside host range");
    //1271(0) means the current day, and sentinel2000 means day0.
    uint32_t index=day?uint32_t(day):le32(h.state,0x8e2d);
    if(index==2000) index=0;
    if(index>uint32_t(std::numeric_limits<int>::max()-252))
        throw FormatError("classic WM: current day outside host range");
    out.preview_calendar_index=int(day);
    out.preview_date=date_of_season_day(int(index),le16(h.state,0x8e31));
    out.native_caption="   "+number(out.preview_date->day,1)+"."+
                       number(out.preview_date->month,1)+".";
}
void table_view(ClassicWmView& out,const HokFile& h) {
    const size_t group=size_t(out.group),stat=group*6;
    out.native_title="Gruppe "+number(out.group+1,out.incoming_numeric_width);
    out.buttons.push_back({hit(ClassicWmAction::Page,237,211,293,222,-1,12-out.group),
                           "Gruppe "+number(2-out.group,out.incoming_numeric_width)});
    for(size_t rank=0;rank<out.table.size();++rank) {
        auto& row=out.table[rank]; row.index=h.state.at(0x05f8+stat+rank);
        if(row.index>=6) throw FormatError("classic WM: invalid saved table index");
        row.team=h.state.at(0x03b8+group*0x24+size_t(row.index));
        row.badge=badge(h,row.team); row.baseline=57+int(rank)*28;
        row.badge_x=rank%2?102:46; row.color=uint8_t(rank%2?30:7);
        row.native_rank=number(int(rank)+1,rank==0?out.incoming_numeric_width:1)+".";
        const size_t i=stat+size_t(row.index);
        row.points=h.state.at(0x0628+i);
        row.minus_points=2*int(static_cast<int8_t>(out.phase))-row.points;
        row.goals_for=h.state.at(0x02bb+i); row.goals_against=h.state.at(0x02eb+i);
        row.native_stats=number(row.points)+":"+number(row.minus_points)+" "+
                         number(row.goals_for)+":"+number(row.goals_against);
        row.hit=hit(ClassicWmAction::ClubInfo,0,rank?42+int(rank)*28:43,
                    319,69+int(rank)*28,row.team);
    }
}
void fixture_view(ClassicWmView& out,const HokFile& h) {
    out.frame_rows=9;
    const int width=out.incoming_numeric_width;
    out.native_title="Gruppe "+number(out.group+1,width)+"   "+number(out.round+1,width)+".Spieltag";
    out.buttons={
        {hit(ClassicWmAction::Round,39,180,85,218,-1,-1,(out.round+4)%5),""},
        {hit(ClassicWmAction::Page,104,180,152,218,-1,10),"Endrunde"},
        {hit(ClassicWmAction::Page,169,180,217,218,-1,9-out.group),"Gruppe "+number(2-out.group,width)},
        {hit(ClassicWmAction::Round,234,180,280,218,-1,-1,(out.round+1)%5),""}};
    const size_t off=size_t(out.group)*0x24+size_t(out.round)*6;
    for(size_t j=0;j<out.fixtures.size();++j) {
        auto& row=out.fixtures[j]; row.top=39+int(j)*43;
        row.home=h.state.at(0x03b8+off+j*2); row.away=h.state.at(0x03b8+off+j*2+1);
        row.home_badge=badge(h,row.home); row.away_badge=badge(h,row.away);
        row.home_border=h.state.at(team_at(row.home)+0x16)?18:7;
        row.away_border=h.state.at(team_at(row.away)+0x16)?18:7;
        row.scores={h.state.at(0x04d8+off+j*2),h.state.at(0x04d8+off+j*2+1)};
        row.native_scores={number(row.scores[0],width),number(row.scores[1],width)};
        const int first=j?39+int(j)*43:40,last=81+int(j)*43;
        row.hits={hit(ClassicWmAction::ClubInfo,0,first,125,last,row.home),
                  hit(ClassicWmAction::ClubInfo,126,first,319,last,row.away)};
    }
}
void knockout_view(ClassicWmView& out,const HokFile& h,const SeasonCalendar& calendar) {
    out.native_title="WM-Endrundenspiele"; out.available=out.phase>=5;
    int scan=int(static_cast<int8_t>(h.state.at(0x03b7)));
    if(scan<0) throw FormatError("classic WM: negative knockout scan offset");
    while(scan<32 && h.state.at(0x8eb1+size_t(scan))!=255) scan+=2;
    out.pair_count=out.available?std::min(16,scan/2):0;
    out.pair_count=std::max(1,out.pair_count);
    constexpr std::array<int,3> extra{1,3,7}; //DS4DFD[5..7]
    const int metric=std::max(0,out.pair_count+1-6+(out.available?extra[size_t(out.phase-5)]:0));
    int units=std::min(11,((metric+4)*7+60)/19);
    out.frame_top=(240-(units*19+23))/2; units=std::max(2,units);
    if(units>8) out.frame_top=2;
    out.frame_rows=units+1;
    out.buttons.push_back({hit(ClassicWmAction::PreviousFixtures,136,201,184,239),"Gruppe"});
    preview_date(out,h,calendar); //native construction precedes placeholder branch
    if(!out.available) return;
    int y=out.frame_top+46;
    for(int pair=0;pair<out.pair_count;++pair) {
        for(size_t i=0;i<heading_at.size();++i) if(pair+1==heading_at[i]) {
            out.headings.push_back({y,std::string(heading_text[i])}); y+=7;
        }
        const int home=h.state.at(0x8eb1+size_t(pair)*2),away=h.state.at(0x8eb1+size_t(pair)*2+1);
        if(home>=64) continue;
        //Incomplete pairs leave native stack-derived click bytes. Reject them
        //rather than invent deterministic behavior for malformed state.
        if(away>=64) throw FormatError("classic WM: incomplete knockout pairing");
        ClassicWmKnockoutRow row; row.pair=pair; row.home=home; row.away=away; row.baseline=y;
        row.native_home=name(h,home); row.native_away=name(h,away);
        int left=h.state.at(0x8f60+size_t(pair)*2),right=h.state.at(0x8f60+size_t(pair)*2+1);
        if(left<100 && right<100) {
            if(left>29) {left-=30; row.markers=1; if(left>29) {left-=30; row.markers=2;}}
            //43xx: right flags overwrite the left marker count.
            if(right>29) {right-=30; row.markers=1; if(right>29) {right-=30; row.markers=2;}}
            row.native_score=number(left)+":"+number(right);
        }
        row.hits={hit(ClassicWmAction::ClubInfo,0,y-5,99,y,home),
                  hit(ClassicWmAction::ClubInfo,100,y-5,319,y,away)};
        out.knockout.push_back(std::move(row)); y+=7;
    }
}
void national_box(ClassicCanvas& c,int x,int y,int id) {
    c.rect(x,y,x+41,y+41,1);
    c.line(x,y,x+41,y,23); c.line(x,y,x,y+41,23);
    c.line(x+41,y+1,x+41,y+41,29); c.line(x+1,y+41,x+40,y+41,29);
    c.blit(id,x+2,y+2,0,0,38,38); //2115:0A79 is opaque
}
void standard_button(ClassicCanvas& c,int x,int y,std::string_view text) {
    const std::array<uint8_t,4> corners{c.pixels().at(x,y),c.pixels().at(x+56,y),
                                      c.pixels().at(x,y+11),c.pixels().at(x+56,y+11)};
    c.blit(4,x,y,17,179,57,12,true,192); //645B:10A6's separate button helper
    c.pixels().at(x,y)=corners[0]; c.pixels().at(x+56,y)=corners[1];
    c.pixels().at(x,y+11)=corners[2]; c.pixels().at(x+56,y+11)=corners[3];
    c.text_native(mini,x,y+7,text,29,1,x+59);
}
} // namespace

ClassicWmView classic_wm_view(const HokFile& h,const SeasonCalendar& calendar,int page,
                             int round,int width,int manager) {
    if(page!=8 && page!=9 && page!=10 && page!=11 && page!=12)
        throw FormatError("classic WM: page must be8,9,10,11 or12");
    if(width<0 || width>255) throw FormatError("classic WM: invalid incoming numeric width");
    if(manager<0 || manager>5) throw FormatError("classic WM: invalid active manager");
    ClassicWmView out; out.page=page; out.incoming_numeric_width=width; out.active_manager=manager;
    out.phase=h.state.at(0x8f85);
    if(out.phase>7) throw FormatError("classic WM: unsupported native competition phase");
    out.group=page==12 || page==9?1:0;
    out.round=round==-1?std::min(out.phase,4):round;
    if(out.round<0 || out.round>4) throw FormatError("classic WM: round must be0..4 or default-1");
    if(page==10) knockout_view(out,h,calendar);
    else if(page>=11) table_view(out,h);
    else fixture_view(out,h);
    return out;
}

ClassicWmHit classic_wm_hit(const ClassicWmView& view,int x,int y) {
    if(x<0 || x>319 || y<0 || y>239) return {};
    for(const auto& button:view.buttons) if(contains(button.hit,x,y)) return button.hit;
    if(view.page>=11) {
        for(const auto& row:view.table) if(contains(row.hit,x,y)) return row.hit;
    } else if(view.page<=9) {
        for(const auto& row:view.fixtures) for(const auto& h:row.hits) if(contains(h,x,y)) return h;
    } else if(y>view.frame_top+26) {
        for(const auto& row:view.knockout) for(const auto& h:row.hits) if(contains(h,x,y)) return h;
    }
    return {};
}

void draw_classic_wm(ClassicCanvas& c,const HokFile& h,const SeasonCalendar& calendar,
                     int page,int round,int width,int manager) {
    const auto view=classic_wm_view(h,calendar,page,round,width,manager);
    c.background(22);
    if(page>=11) {
        c.frame(16,2,12,12);
        standard_button(c,237,211,view.buttons[0].native);
        c.text_native(bold,0,21,view.native_title,7,1,319);
        c.line(27,24,292,24,7);
        for(size_t i=0;i<view.table.size();++i) {
            const auto& r=view.table[i];
            c.text_native(bold,25,r.baseline,r.native_rank,r.color,0);
            c.text_native(flow,25,r.baseline+7,std::string(44,'-'),r.color,0);
            if(i==3) {c.line(27,r.baseline+8,289,r.baseline+8,18); c.line(27,r.baseline+9,289,r.baseline+9,18);}
            national_box(c,r.badge_x,r.baseline-20,r.badge);
            c.text_native(bold,170,r.baseline,r.native_stats,r.color,0);
        }
        return;
    }
    if(page<=9) {
        c.frame(16,2,12,9);
        for(int i=0;i<4;++i) c.frame(39+65*i,180,2,2);
        c.text_native(mini,108,201,"Endrunde",30,-1,151);
        c.text_native(mini,173,201,view.buttons[2].native,30,-1,215);
        c.blit(4,49,195,19,104,29,9); c.blit(4,244,195,19,125,29,9);
        //The native offscreen image begins as a copy of this exact canvas.
        if(manager!=5) {c.blit(36,29,10,0,66,22,22); c.blit(36,269,10,0,66,22,22);}
        c.rect(51,12,268,32,4); c.rect(26,33,292,165,4);
        c.text_native(bold,0,30,view.native_title,7,1,319);
        //4996:0C81 copies only292 columns (x0..291) from its offscreen image.
        c.line(27,33,291,33,7);
        for(const auto& r:view.fixtures) {
            c.text_native(bold,125,r.top+27,"-",7,0,125);
            for(int side=0;side<2;++side) {
                const int x=73+68*side;
                c.outline(x,r.top,x+41,r.top+41,side?r.away_border:r.home_border);
                c.outline(x+1,r.top+1,x+40,r.top+40,4);
                c.blit(side?r.away_badge:r.home_badge,x+2,r.top+2,0,0,38,38);
                const int sx=218+35*side;
                c.text_native(bold,sx,r.top+27,r.native_scores[size_t(side)],7,0,sx);
                if(side==0) c.text_native(bold,234,r.top+27,":",7,0,237);
            }
        }
        return;
    }
    c.frame(136,201,2,2);
    c.text_native(mini,160,221,"Gruppe",7,-1,160);
    c.frame(5,view.frame_top,13,view.frame_rows);
    c.blit(36,13,view.frame_top+4,0,66,22,22); c.blit(36,286,view.frame_top+4,0,66,22,22);
    c.text_native(bold,0,view.frame_top+19,view.native_title,7,1,319);
    c.text_native(flow,0,view.frame_top+34,view.available?view.native_caption:
                  "Die Paarungen stehen noch nicht fest...",30,0,319);
    for(const auto& heading:view.headings) c.text_native(mini,15,heading.baseline,heading.native,7,0,183);
    for(const auto& r:view.knockout) {
        c.text_native(mini,15,r.baseline,r.native_home,30,-1,94);
        c.text_native(mini,98,r.baseline,"-",7,-1,100);
        c.text_native(mini,104,r.baseline,r.native_away,1,-1,183);
        c.text_native(mini,233,r.baseline,r.native_score,1);
        for(int d=0;d<r.markers;++d) c.rect(255+2*d,r.baseline,255+2*d,r.baseline,1);
    }
}

bool prepare_classic_wm_working(HokFile& h,const SeasonCalendar& calendar,int page) {
    if(page!=10) { //validate page; tables/fixtures have no date lookup
        if(page!=8 && page!=9 && page!=11 && page!=12) throw FormatError("classic WM: invalid page");
        return false;
    }
    const auto view=classic_wm_view(h,calendar,page);
    if(!view.preview_date) return false;
    (void)h.state.at(0x8e2c);
    const auto date=*view.preview_date; const int index=int(le32(h.state,0x8e2d));
    const auto put=[&](size_t at,uint32_t value) {
        for(size_t i=0;i<4;++i) h.state.at(at+i)=uint8_t(value>>(8*i));
    };
    put(0x8e21,uint32_t(date.day)); put(0x8e25,uint32_t(date.month-1)); put(0x8e29,uint32_t(date.year));
    h.date={date.day,date.month,date.year,index};
    return true;
}
} // namespace em::ui
