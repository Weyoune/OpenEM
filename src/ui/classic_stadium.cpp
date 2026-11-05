// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_stadium.hpp"
#include <bit>
#include <string_view>

namespace em::ui {
namespace {
constexpr auto mini=ClassicFontId::Mini;
constexpr std::array<std::string_view,11> labels{
    "]berdachung","Anzeigetafel","K}hlanlage","Sitzkomfort","Zustand","Verkehrsanb.",
    "Gastronomie","Kino","Bowlingbahn","Sitzpl{tze","Stehpl{tze"};
constexpr std::array<std::array<std::string_view,5>,9> descriptions{{
    {"OHNE DACH","HALBOFFEN","HALLE","",""},
    {"MANUELL","DIGITAL","HIGH-TECH","",""},
    {"'K]HLI'","'FROSTA'","'MEGAFROST'","",""},
    {"PLASTIKSCHALEN","SITZE MIT KISSEN","FAST SOFAS","",""},
    {"GANZ }BEL","SCHLECHT","MITTEL","GANZ GUT","SEHR GUT"},
    {"FU~WEG","STRA~E","AUTOBAHN","",""},
    {"OHNE","IMBI~BUDE","KNEIPEN","RESTAURANT",""},
    {"OHNE","KLEIN","MITTEL","GRo~",""},
    {"OHNE","VORHANDEN","","",""}
}};
constexpr std::array<int,9> description_count{3,3,3,3,5,3,4,4,2};
//DS4F97 paint order; every row is sourceX/Y,destX/Y,width/height.
struct Sprites { int item; std::array<std::array<int,6>,3> levels; };
constexpr std::array<Sprites,5> sprites{{
    {5,{{{0,0,244,50,66,118},{66,0,244,50,76,118},{0,0,0,0,0,0}}}},
    {6,{{{211,109,25,112,50,30},{211,139,21,105,56,39},{143,115,13,91,68,58}}}},
    {8,{{{142,60,201,94,68,55},{0,0,0,0,0,0},{0,0,0,0,0,0}}}},
    {0,{{{142,0,79,57,144,60},{0,118,80,57,143,57},{0,0,0,0,0,0}}}},
    {7,{{{144,173,29,44,52,21},{210,86,27,42,53,23},{210,60,24,39,58,26}}}}
}};
const uint8_t* bytes(const HokFile& h,size_t at,size_t count) {
    if(at>h.state.size() || count>h.state.size()-at)
        throw FormatError("classic stadium: truncated native state");
    return h.state.data()+at;
}
std::string description(int item,int level) {
    if(item<0 || item>=9 || level<0 || level>=description_count[size_t(item)])
        throw FormatError("classic stadium: unavailable original level description");
    return std::string(descriptions[size_t(item)][size_t(level)]);
}
std::string number(int64_t value,int width=2,char separator='.') {
    const bool negative=value<0;
    auto text=std::to_string(negative?-value:value);
    for(size_t i=text.size();i>3;i-=3) text.insert(i-3,1,separator);
    if(negative) return "-"+text;
    if(int(text.size())<width) text.insert(0,size_t(width-int(text.size())),'%');
    return text;
}
std::string money(int32_t raw,const FinanceTables& t,int width=2) {
    if(t.money_num<=0 || t.money_den<=0) throw FormatError("classic stadium: invalid currency");
    //199F:02D8 multiplies low32 before signed long division047C.
    const auto product=uint32_t(uint64_t(uint32_t(raw))*uint32_t(t.money_den));
    const auto converted=int64_t(std::bit_cast<int32_t>(product))/int64_t(t.money_num);
    return number(converted,width,t.money_num==4?',':'.')+" DM";
}
int hover_item(int x,int y) {
    //0BE7 checks the first footprint even when level0 has no painted overlay.
    for(const auto& group:sprites) {
        const auto& a=group.levels[0];
        if(x>=a[2] && x<=a[2]+a[4] && y>=a[3] && y<=a[3]+a[5]) return group.item;
    }
    if(x<185 && y>=173 && y<=233) return (y-173)/7;
    if(x>212 && y>=172 && y<=185) return 9+int(y>178);
    return -1;
}
bool arrow(int x,int y,int anchor_x,int anchor_y) {
    return x>=anchor_x && x<anchor_x+8 && y>=anchor_y && y<anchor_y+9;
}
} // namespace

ClassicStadiumView classic_stadium_view(const HokFile& h,const FinanceTables& finance,
    const StadiumTables& tables,ClassicStadiumSelection selection,int manager) {
    if(manager<0 || manager>=4 || selection.hover < -1 || selection.hover>10 ||
       selection.quoteItem < -1 || selection.quoteItem>10)
        throw FormatError("classic stadium: invalid manager or selection");
    const size_t block=0x0dcd+size_t(manager)*0x1af;
    (void)bytes(h,block,0x193);
    ClassicStadiumView out;out.selection=selection;
    out.ticket=h.state[block+0xe4];out.cash_bits=le32(h.state,block+0x18f);
    const char separator=finance.money_num==4?',':'.';
    for(int item=0;item<11;++item) {
        auto& row=out.values[size_t(item)]; row.item=item; row.native_label=labels[size_t(item)];
        row.label_x=item<9?1:213;row.value_x=item<9?55:267;row.baseline=177+7*(item<9?item:item-9);
        row.color=uint8_t(selection.planned?1:31);row.native_value=": ";
        if(item<9) {
            const size_t p=block+0x116+size_t(item)*2;
            const int level=h.state[p+size_t(selection.planned)];
            if(selection.planned && !level) row.native_value+="(keine Baut{tigkeiten)";
            else {
                row.native_value+=description(item,level);
                if(selection.planned) {
                    const int weeks=h.state[block+0x128+size_t(item)];
                    row.native_value+=" (Noch "+number(weeks,selection.numberWidth,separator)+(weeks==1?" Woche)":" Wochen)");
                }
            }
        } else {
            const size_t p=block+0x133+size_t(item-9)*4;
            const int capacity=le16(h.state,p+2*size_t(selection.planned));
            if(selection.planned && !capacity) row.native_value+="(kein Bau...)";
            else {
                row.native_value+=number(capacity,selection.numberWidth,separator);
                if(selection.planned) row.native_value+=" ("+number(h.state[block+0x128+size_t(item)],selection.numberWidth,separator)+")";
            }
        }
    }
    for(const auto& group:sprites) {
        const int level=h.state[block+0x116+size_t(group.item)*2];
        if(!level) continue;
        if(level>description_count[size_t(group.item)]-1 || level>3)
            throw FormatError("classic stadium: invalid native overlay level");
        const auto& a=group.levels[size_t(level-1)];
        out.overlays.push_back({group.item,a[2],a[3],a[0],a[1],a[4],a[5]});
    }
    //Ticket is already a DM amount, not raw money converted through02D8.
    out.native_ticket=": "+number(out.ticket,2,separator)+" DM";
    out.native_cash=money(std::bit_cast<int32_t>(out.cash_bits),finance,10);
    const int hover=selection.quoteItem>=0?selection.quoteItem:selection.hover;
    if(hover>=0) out.native_hint=std::string(labels[size_t(hover)])+" ausbauen";
    if(selection.quoteItem>=0) {
        out.quote=stadium_target(h,tables,selection.quoteItem,selection.target,manager);
        out.selection.target=out.quote->target;
        out.native_quote_cost=" Kosten: "+money(out.quote->nominal_cost,finance);
        out.quote_color=uint8_t(out.quote->affordable?8:17);
        out.native_quote_target=selection.quoteItem<9?description(selection.quoteItem,out.quote->target):
            number(out.quote->capacity,2,separator);
    }
    return out;
}

ClassicStadiumHit classic_stadium_hit(const ClassicStadiumView& view,int x,int y,bool right) {
    if(view.quote) {
        if(right) return {ClassicStadiumAction::QuoteCancel,view.quote->item};
        if(arrow(x,y,192,210)) return {ClassicStadiumAction::TargetIncrease,view.quote->item};
        if(arrow(x,y,202,210)) return {ClassicStadiumAction::TargetDecrease,view.quote->item};
        if(x>212 && x<316 && y>207 && y<219)
            return {ClassicStadiumAction::QuoteConfirm,view.quote->item,view.quote->available&&view.quote->affordable};
        return {ClassicStadiumAction::QuoteCancel,view.quote->item};
    }
    if(right) return {y>167?ClassicStadiumAction::TogglePlanned:ClassicStadiumAction::Back};
    if(arrow(x,y,192,189)) return {ClassicStadiumAction::TicketIncrease,-1,view.ticket<30};
    if(arrow(x,y,202,189)) return {ClassicStadiumAction::TicketDecrease,-1,view.ticket>10};
    const int item=hover_item(x,y);
    return item>=0?ClassicStadiumHit{ClassicStadiumAction::Item,item}:ClassicStadiumHit{};
}

int classic_stadium_target_step(const HokFile& h,const StadiumTables& tables,int item,
    int target,int response,int manager) {
    if(response!=-1 && response!=1) throw FormatError("classic stadium: invalid native arrow response");
    const auto old=stadium_target(h,tables,item,target,manager);
    const int candidate=int(static_cast<uint8_t>(old.target-response));
    const auto next=stadium_target(h,tables,item,candidate,manager);
    return next.available && next.affordable?next.target:old.target;
}

void draw_classic_stadium(ClassicCanvas& c,const HokFile& h,const FinanceTables& finance,
    const StadiumTables& tables,ClassicStadiumSelection selection,int manager) {
    const auto view=classic_stadium_view(h,finance,tables,selection,manager);
    c.background(14); //embedded KB14 palette, original upper168 rows
    c.line(0,168,319,168,0);c.rect(0,169,319,239,4);
    for(const auto& a:view.overlays) c.blit(8,a.x,a.y,a.sx,a.sy,a.width,a.height);
    for(const auto& row:view.values) {
        c.text_native(mini,row.label_x,row.baseline,row.native_label,30);
        c.text_native(mini,row.value_x,row.baseline,row.native_value,row.color);
    }
    c.text_native(mini,213,194,"Eintritt",30);
    c.text_native(mini,267,194,view.native_ticket,31);
    c.text_native(mini,213,236,"Konto",11);c.text_native(mini,238,236,":",11);
    c.text_native(mini,312-c.font().measure_native(mini,view.native_cash),236,view.native_cash,11);
    c.blit(4,192,189,20,0,10,8);c.blit(4,202,189,20,9,10,8);
    if(!view.native_hint.empty()) c.text_native(mini,213,205,view.native_hint,31);
    if(view.quote) {
        c.outline(213,208,315,218,2);
        c.blit(4,192,210,20,0,10,8);c.blit(4,202,210,20,9,10,8);
        c.text_native(mini,213,225,view.native_quote_cost,view.quote_color);
        c.text_native(mini,213,215,view.native_quote_target,24,-1,315);
    }
    //0858 patches KB4 source border18,180..71,188 to2 for subsequent
    //standard dialogs. This pure page never mutates the shared asset; its
    //arrow crops use rows0..16 and do not intersect that temporary patch.
}
} // namespace em::ui
