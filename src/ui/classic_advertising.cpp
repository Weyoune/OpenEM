// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_advertising.hpp"
#include <algorithm>
#include <string_view>
#include <utility>

// The classic advertising screen: view model, hit test and drawing (see classic_advertising.hpp).

namespace em::ui {
namespace {
constexpr auto mini=ClassicFontId::Mini;
// Sponsor names as stored in the game: 0..9 for the boards and surfaces, 10..14 the TV
// stations of surface 8. Game text in the game's encoding ('{' = ä).
constexpr std::array<std::string_view,15> names{"JWE Service GmbH","CCH","Korona Soft","PC Games",
    "Amiga Games","Rushware","Herrenh{user","Play Time","Software 2000","KRON Soft",
    "ARB","4 SAT","SET 1","LTR minus","premium"};
// The five "Firmen-Info" lines per sponsor 0..9 (game text, reproduced verbatim including its
// typos; '{' = ä, '}' = ü).
constexpr std::array<std::array<std::string_view,5>,10> information{{
    {"J.W.E. Service GmbH","Telekommunikation vom Fachbetrieb.","Telefone, Anrufbeantworter, Telefax","Beratung - Verkauf - Installation","Telefon/Fax: 0511/602094"},
    {"Computer Center Holstein","Ihr Lieferant fuer PCs und PC Zubehoer","Max-Planck-Str. 9","2420 Eutin","Tel.: 04521-74300"},
    {"","Der Computerspiele Blitzversand","Tel.: 05241-1828","",""},
    {"","Jeden Monat neu! Spieletests, Insiderberichte und alles","rund um MS-DOS kompatible Rachnersysteme!","Jeden Monat neu mit randvoller Coverdisk an Ihrem Kiosk!",""},
    {"","Jeden Monat neu! Spieletests, Insiderberichte und alles","rund um den Amiga! Jeden Monat neu mit randvoller","Coverdisk an Ihrem Kiosk!",""},
    {"","Deutschlands Top Computer- und Videospiel Vertrieb","Rushware - Online with the trend","",""},
    {"Herrenh{user Brauerei Hannover","Gegr. 1868. Bedeutendste Privatbrauerei Norddeutschlands.","Seit vielen Generationen in Privatbesitz.","Produkte: Pils und Diplomat.","Das Pils, das noch 90 Tage reift."},
    {"","Monatlich erscheinende Fachzeitschrift f}r alle","Computer- und Videospielsysteme","",""},
    {"","Publisher f}r anspruchsvolle Software","Hotline : 04521-800444","Mailbox : 04521-800447",""},
    {"KrOn Software","Gegr. 1988. Bescheidendstes Programmierteam Deutschlands.","Besteht aus: Werner Krahe und Jens Onnen.","W}rden uns freuen, wenn Sie dieses","Spielchen ORIGINAL (legal) erworben haben..."}
}};
// Surface kind of a contract slot: 0 for the boards 0..6, 1..4 for slots 7..10.
int kind(int slot) {return slot<7?0:slot-6;}
// State offset of the running contract of `slot` for `manager`.
size_t contract_at(int manager,int slot) {return 0xacfc+size_t(manager)*0x6e+size_t(slot)*10;}
// Decodes the 10-byte advertising record at state offset `at`.
ClassicAdvertisingRecord record(const HokFile& h,size_t at) {
    return {int32_t(le32(h.state,at)),h.state.at(at+4),h.state.at(at+5),h.state.at(at+6),
            h.state.at(at+7),le16(h.state,at+8)};
}
// A surface is free for a new deal when empty or when flag bit 7 is set.
bool free(const ClassicAdvertisingRecord& r) {return r.amount==0 || (r.flags&128)!=0;}
// Formats an integer like the game: optional thousands grouping with `separator`; positive
// numbers are padded on the left to `width` with '%' (a blank-width glyph in the game font).
std::string number(int64_t value,int width=1,bool grouped=false,char separator='.') {
    const bool negative=value<0; auto s=std::to_string(negative?-value:value);
    if(grouped) for(size_t i=s.size();i>3;i-=3) s.insert(i-3,1,separator);
    if(negative) return "-"+s;
    if(int(s.size())<width) s.insert(0,size_t(width-int(s.size())),'%');
    return s;
}
// Internal currency units -> grouped display amount (',' as separator when money_num is 4).
std::string money(int64_t amount,const FinanceTables& t,int width=1) {
    if(t.money_num<=0 || t.money_den<=0) throw FormatError("classic advertising: invalid currency");
    return number(amount*t.money_den/t.money_num,width,true,t.money_num==4?',':'.');
}
ClassicAdvertisingHit hit(ClassicAdvertisingAction action,int slot,int l,int t,int r,int b,bool enabled=true) {
    return {action,slot,l,t,r,b,enabled};
}
// Adds a detail-panel line (colour 0, no shadow).
void text(ClassicAdvertisingView& v,int x,int y,std::string s) {v.detail.push_back({x,y,-1,0,-1,std::move(s)});}
// Draws a 57x12 button from KB image 4 at (x, y), keeping the four corner pixels of the
// background, with the centred label.
void button(ClassicCanvas& c,int x,int y,std::string_view label) {
    const std::array<uint8_t,4> corners{c.pixels().at(x,y),c.pixels().at(x+56,y),c.pixels().at(x,y+11),c.pixels().at(x+56,y+11)};
    c.blit(4,x,y,17,179,57,12);
    c.pixels().at(x,y)=corners[0]; c.pixels().at(x+56,y)=corners[1];
    c.pixels().at(x,y+11)=corners[2]; c.pixels().at(x+56,y+11)=corners[3];
    c.text_native(mini,x,y+7,label,29,1,x+59);
}
// Draws the sponsor logo (from the logo atlas, KB image 11) on every surface with a running
// contract: the seven boards along the top, surfaces 7 and 8 on the left, 9 and 10 on the right.
void contracts(ClassicCanvas& c,const HokFile& h,int manager) {
    for(int slot=0;slot<11;++slot) {
        const auto r=record(h,contract_at(manager,slot)); if(!r.amount) continue;
        if(r.sponsor>9 || (slot==8 && r.sponsor>4)) throw FormatError("classic advertising: invalid sponsor atlas index");
        if(slot<7) c.blit(11,40+35*slot,35,1,1+25*r.sponsor,33,23);
        else if(slot==7) {
            if(r.sponsor<=7) c.blit(11,30,120,70,26*r.sponsor,31,26);
            else c.blit(11,30,120,39,130+26*(r.sponsor-8),31,26);
        } else if(slot==8) c.blit(11,11,173,36,1+25*r.sponsor,33,23);
        else c.blit(11,268,92+36*(slot-9),1,1+25*r.sponsor,33,23);
    }
}
// Outlines the selected surface in colour 18. Surfaces 9 and 10 outline the whole facility
// area and, with `leaf`, also the surface itself.
void outline_selection(ClassicCanvas& c,int slot,bool leaf=true) {
    if(slot<7) c.outline(39+35*slot,34,73+35*slot,58,18);
    else {
        constexpr std::array<std::array<int,4>,3> region{{{3,86,78,157},{3,164,78,235},{87,86,315,158}}};
        const auto& r=region[size_t(slot<9?slot-7:2)]; c.outline(r[0],r[1],r[2],r[3],18);
        if(slot>=9 && leaf) c.outline(267,91+36*(slot-9),301,115+36*(slot-9),18);
    }
}
} // namespace

ClassicAdvertisingView classic_advertising_view(const HokFile& h,const FinanceTables& tables,
                                               ClassicAdvertisingSelection selection,int manager) {
    if(manager<0 || manager>=4 || selection.contractSlot < -1 || selection.contractSlot>10)
        throw FormatError("classic advertising: invalid manager/surface");
    if(h.state.at(0xaeb4)>5) throw FormatError("classic advertising: invalid saved difficulty");
    FinanceTables t=tables; t.level=h.state.at(0xaeb4); t.cost_factor=int32_t(le32(h.state,0x06b0));
    auto b=budget_from_save(h,manager);
    // The team strength is the sum of the four cached team values stored in the team record
    // (0x17E7 + team * 61, +0x18..+0x1B), read from the raw state so the result does not depend
    // on whether the caller has refreshed the decoded team model.
    if(b.team<0 || b.team>=150) throw FormatError("classic advertising: invalid manager team");
    b.team_strength=0;
    for(size_t i=0;i<4;++i) b.team_strength+=h.state.at(0x17e7+size_t(b.team)*61+0x18+i);
    ClassicAdvertisingView v; v.selection=selection;
    v.income=monthly_income(b,true); v.expenses=monthly_expenses(t,b,true,true);
    // Loans between human managers (manager block +0x19B, one i32 per other manager): 2.5% of
    // a negative entry the other manager holds for this one counts as income (if this manager
    // is active), 2.5% of a negative entry this manager holds for the other one as expenses
    // (if that one is active). Active = index below state 0x0DC6, the number of human managers.
    const size_t block=0x0dcd+size_t(manager)*0x1af;
    for(int party=0;party<4;++party) if(party!=manager) {
        const int64_t principal=int32_t(le32(h.state,block+0x19b+size_t(party)*4));
        const int64_t reciprocal=int32_t(le32(h.state,0x0dcd+size_t(party)*0x1af+0x19b+size_t(manager)*4));
        if(manager<h.state.at(0x0dc6) && reciprocal<0) v.income-=reciprocal*25/1000;
        if(party<h.state.at(0x0dc6) && principal<0) v.expenses-=principal*25/1000;
    }
    v.saldo=int32_t(uint32_t(v.income-v.expenses));  // wraps to 32 bits like the game
    v.nativeIncome=money(v.income,t,7)+" DM"; v.nativeExpenses=money(v.expenses,t,7)+" DM";
    v.nativeSaldo=money(v.saldo,t,7)+" DM";
    // Hit areas; 9 and 10 are usable only once stadium facility 0 / 2 is built (state 2).
    for(int i=0;i<7;++i) {
        v.surfaces[size_t(i)]=hit(ClassicAdvertisingAction::Surface,i,37+35*i,29,71+35*i,53);
        if(free(record(h,contract_at(manager,i)))) ++v.freeBoards;
    }
    v.surfaces[7]=hit(ClassicAdvertisingAction::Surface,7,4,86,79,161);
    v.surfaces[8]=hit(ClassicAdvertisingAction::Surface,8,4,162,79,239);
    v.surfaces[9]=hit(ClassicAdvertisingAction::Surface,9,269,91,303,125,b.stadium[0]==2);
    v.surfaces[10]=hit(ClassicAdvertisingAction::Surface,10,269,126,303,160,b.stadium[2]==2);
    const int slot=selection.contractSlot;
    if(slot<0) return v;
    v.available=v.surfaces[size_t(slot)].enabled; if(!v.available) return v;
    v.kind=kind(slot); v.maxOffer=slot==8?4:9;
    const auto current=record(h,contract_at(manager,slot));
    v.renewalPending=(current.flags&127)!=0 && current.months==0;
    const size_t offers=0xa52c+size_t(manager)*0x1f4+size_t(v.kind)*100;
    // Offers of this surface kind (state 0xA52C + manager * 0x1F4 + kind * 100).
    for(int i=v.maxOffer;i>=0;--i) if(record(h,offers+size_t(i)*10).amount!=0) {v.highestOffer=i;break;}
    if(selection.offerIndex==-1) v.selection.offerIndex=v.highestOffer;
    if(v.selection.offerIndex<0 || v.selection.offerIndex>v.maxOffer)
        throw FormatError("classic advertising: invalid offer index");
    v.record=selection.browsingOffers?record(h,offers+size_t(v.selection.offerIndex)*10):current;
    const auto& r=v.record;
    if(r.sponsor>9 || (slot==8 && r.sponsor>4)) throw FormatError("classic advertising: invalid detail sponsor");
    v.nativeSponsor=names[size_t(r.sponsor+(slot==8?10:0))];
    if(r.amount || selection.browsingOffers) text(v,156,179,v.nativeSponsor);
    if(r.amount) {
        text(v,156,189,"Dauer");
        const int option=r.flags&127;
        text(v,191,189,": "+number(int(static_cast<int8_t>(r.months)))+
             (option?" - "+number(int(static_cast<int8_t>(r.months))+option):"")+" Monat"+
             ((int(static_cast<int8_t>(r.months))>1 || option)?"e":""));
        if(!selection.browsingOffers) {
            if(option && r.months==0) text(v,275,189,"! Option !");
            else if(r.flags&128) text(v,275,189,"K}ndbar.");
        }
        if(slot<7) {
            // An offer states the boards wanted; a running deal counts the boards sharing its id.
            int boards=r.boards;
            if(!selection.browsingOffers) {
                boards=0;for(int i=0;i<7;++i) if(record(h,contract_at(manager,i)).boards==r.boards) ++boards;
            }
            text(v,156,197,"Banden"); text(v,191,197,": "+number(boards));
        }
        text(v,156,205,selection.browsingOffers?"Angebot":"Pr{mie");
        text(v,191,205,": "+money(r.amount,t)+" DM  pro Monat");
    } else text(v,156,189,selection.browsingOffers?"Es liegt momentan kein Angebot vor...":"Diese Werbefl{che ist noch unbefleckt.");
    // Browsing: info for all but the TV surface; accepting needs enough free boards, or a
    // free single surface.
    if(selection.browsingOffers) {
        v.canInfo=slot!=8;
        v.canAccept=r.amount!=0 && (slot<7?r.boards<=v.freeBoards:free(current));
        v.controls.push_back(hit(ClassicAdvertisingAction::PreviousOffer,slot,138,183,145,191,v.selection.offerIndex>0));
        v.controls.push_back(hit(ClassicAdvertisingAction::NextOffer,slot,138,192,145,200,v.selection.offerIndex<v.maxOffer));
        if(v.canInfo) v.controls.push_back(hit(ClassicAdvertisingAction::Info,slot,258,170,313,180));
        if(v.canAccept) v.controls.push_back(hit(ClassicAdvertisingAction::Accept,slot,258,186,313,196));
    }
    constexpr std::array<int,5> ys{100,113,121,129,137};
    for(size_t i=0;i<5;++i) v.info[i]={30,ys[i],290,uint8_t(i?1:7),5,
        (i?"":"Firmen-Info: ")+std::string(information[size_t(v.selection.offerIndex)][i])};
    return v;
}

ClassicAdvertisingHit classic_advertising_hit(const ClassicAdvertisingView& v,int x,int y) {
    for(const auto& h:v.controls) if(h.enabled && x>=h.left && x<=h.right && y>=h.top && y<=h.bottom) return h;
    // An unavailable facility is still a surface: selecting it leaves the offer browser and
    // paints the inactive group, without entering a deal.
    for(const auto& h:v.surfaces) if(x>=h.left && x<=h.right && y>=h.top && y<=h.bottom) return h;
    return {};
}

void draw_classic_advertising(ClassicCanvas& c,const HokFile& h,const FinanceTables& t,
                             ClassicAdvertisingSelection selection,int manager) {
    const auto v=classic_advertising_view(h,t,selection,manager);
    const auto b=budget_from_save(h,manager);
    c.background(151);
    if(b.stadium[0]==2) c.blit(24,152,87,0,0,24,30);
    if(b.stadium[2]==2) c.blit(24,136,145,24,0,56,10);
    c.outline(99,178,135,204,24); c.line(90,213,310,213,28);
    contracts(c,h,manager);
    c.text_native(mini,95,223,"Einnahmen",0); c.text_native(mini,137,223,":",0);
    c.text_native(mini,198-c.font().measure_native(mini,v.nativeIncome),223,v.nativeIncome,0);
    c.text_native(mini,95,231,"Ausgaben",0); c.text_native(mini,137,231,":",0);
    c.text_native(mini,198-c.font().measure_native(mini,v.nativeExpenses),231,v.nativeExpenses,0);
    c.text_native(mini,219,227,"Saldo  :",0); c.text_native(mini,254,227,v.nativeSaldo,0);
    c.blit(4,138,183,20,0,10,17); c.outline(137,182,148,200,24);
    if(selection.contractSlot<0) {
        // The original restores the detail regions from its saved copy of the background
        // picture; the complete repaint above already contains that picture, so nothing is
        // cleared here.
        return;
    }
    if(!v.available) {
        outline_selection(c,selection.contractSlot,false);
        return;
    }
    outline_selection(c,selection.contractSlot);
    if(v.record.amount || selection.browsingOffers) c.blit(11,100,179,selection.contractSlot==8?35:0,25*v.record.sponsor,35,25);
    else c.rect(100,179,135,204,0);
    for(const auto& row:v.detail) c.text_native(mini,row.x,row.baseline,row.native,row.color,row.shadow,row.right);
    c.outline(99,178,135,204,selection.browsingOffers?7:24);
    if(v.canInfo) button(c,257,169,"Info");
    if(v.canAccept) button(c,257,185,"Annehmen");
}

void draw_classic_advertising_info(ClassicCanvas& c,const ClassicAdvertisingView& v) {
    if(!v.canInfo) return;
    // Like the original: sample the pixel at (35,95); unless it is 255, draw a plain box with a
    // shadow (32,93..290,150) whose corners take the sampled colour. Then the bevel and text.
    const auto edge=c.pixels().at(35,95);
    if(edge!=255) {
        c.rect(32,93,290,150,6); c.rect(30,91,288,148,0);
        for(const auto p:std::array<std::pair<int,int>,6>{{{30,91},{290,93},{288,91},{290,150},{32,150},{30,148}}})
            c.rect(p.first,p.second,p.first,p.second,edge);
        c.rect(288,148,288,148,0);
    }
    c.line(31,91,287,91,1); c.line(30,92,30,147,1);
    c.line(288,92,288,147,29); c.line(31,148,287,148,29);
    for(const auto& row:v.info) c.text_native(mini,row.x,row.baseline,row.native,row.color,row.shadow,row.right);
}
} // namespace em::ui
