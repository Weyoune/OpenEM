// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
#include "ui/classic_keyboard.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace em;
using namespace em::ui;
static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void require_native_rgb(const ClassicCanvas& canvas,const fs::path& file){
    if(!fs::exists(file))return;
    const auto rgb=read_file(file);const auto pixels=canvas.argb();
    require(rgb.size()==pixels.size()*3,"native RGB size");
    for(size_t n=0;n<pixels.size();n++)
        require((pixels[n]&0xFFFFFF)==(uint32_t(rgb[n*3])<<16|uint32_t(rgb[n*3+1])<<8|rgb[n*3+2]),"native modal including backing and cursor is pixel exact");
}

int main(int argc,char** argv){
    try{
        if(argc!=4)throw std::runtime_error("classic_app_tests DATA REFERENCE OUTPUT");
        require(classic_virtual_key(0x100,13,1)==13,"scanless Windows Enter reaches SDL controller");
        require(classic_virtual_key(0x100,8,1)==8,"scanless Windows Backspace reaches SDL controller");
        require(classic_virtual_key(0x100,27,1)==27,"scanless Windows Escape reaches SDL controller");
        require(!classic_virtual_key(0x100,13,0x001c0001),"physical Enter must not be duplicated");
        require(!classic_virtual_key(0x101,13,1),"key release must not resubmit");
        const fs::path data=path_from_text(argv[1]),references=path_from_text(argv[2]);
        const auto serial=std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path output=path_from_text(argv[3])/std::to_string(serial);
        ClassicApplication app(data,output,references/"START.HOK");
        const auto before=app.session().game().checkpoint_bytes();
        app.click(120,110,false);app.click(240,220,false);
        require(app.screen()==ClassicScreen::Training,"native training route");
        app.click(14,103,false); // Erholung, Monday first unit
        require(app.session().game().checkpoint_bytes()==before,"editing must not advance the game");
        app.key(SDLK_ESCAPE);
        require(app.screen()==ClassicScreen::Training,"native training has no escape rollback");
        app.click(0,0,true);
        require(app.screen()==ClassicScreen::Main,"right click exits training");
        require(app.session().game().training_plan()[0][0]==kRecovery,"edited plan committed on return");
        require(app.session().game().budget().open_97ee==33000,"native recovery appointment cost");
        require(app.session().dirty(),"training change marks save dirty");
        app.click(200,160,false);app.draw();const auto save_footer=app.canvas().argb();app.click(80,220,false);
        require(app.screen()==ClassicScreen::Files,"native save route");
        app.click(100,200,false);app.text_input("INPUT");app.key(SDLK_RETURN);
        require(app.screen()==ClassicScreen::Main,"Enter accepts filename");
        const fs::path saved=output/"INPUT.OEM";
        require(fs::exists(saved),"new checkpoint created");
        require(!app.session().dirty(),"checkpoint marks clean");
        app.draw();require(app.canvas().argb()==save_footer,"native save returns with the selected Gemischtes category/footer");
        ClassicApplication loaded(data,output,saved);
        require(loaded.session().game().checkpoint_bytes()==app.session().game().checkpoint_bytes(),"GUI save/load exact continuation");
        app.click(20,220,false);app.click(240,212,false);app.click(100,200,false);app.text_input("INPUT.OEM");app.key(SDLK_RETURN);app.draw();
        require(app.screen()==ClassicScreen::Main&&app.canvas().argb()==save_footer,"native live load retains the selected category/footer without a new menu invocation");
        const Bytes bytes=read_file(saved);
        app.click(200,160,false);app.click(80,220,false);app.click(100,200,false);app.text_input("INPUT");app.key(SDLK_RETURN);
        require(app.screen()==ClassicScreen::Files&&read_file(saved)==bytes,"existing save remains intact");
        app.draw(); // failed-save notice must also render safely
        ClassicApplication contract(data,output,references/"START.HOK");
        const auto original=contract.session().game().checkpoint_bytes();
        contract.click(120,110,false);contract.click(130,220,false);contract.click(40,31,false);
        contract.click(100,155,false); // stage insurance in first player's panel
        contract.click(100,165,false); // explicit Abbruch
        require(contract.session().game().checkpoint_bytes()==original,"cancel restores staged insurance and contract");
        contract.click(40,31,false);contract.click(100,148,false);
        contract.text_input("20000");contract.key(SDLK_RETURN);contract.click(20,165,false);
        require(contract.session().game().save().squad[0].salary==20000,"native salary editor commits a same-length raise");
        require(contract.session().game().save().squad[0].contract_years==2,"salary edit preserves duration");
        ClassicApplication bank(data,output,references/"START.HOK");
        bank.click(40,110,false);bank.click(20,220,false);bank.click(50,20,false);
        bank.click(310,80,false);bank.click(200,80,false);
        require(bank.session().game().budget().bank==3000,"native bank fine-adjust and deposit click");
        require(bank.session().game().budget().balance==1497000,"bank deposit preserves total money");
        ClassicApplication fixtures(data,output,references/"MONAT1.HOK");
        const auto fixtures_before=fixtures.session().game().checkpoint_bytes();
        fixtures.click(40,170,false);fixtures.click(25,220,false);fixtures.motion(0,0);fixtures.draw();
        require(fixtures.screen()==ClassicScreen::LeagueFixtures,"native Pairings-to-Bundesliga route");
        const auto fixture_current=fixtures.canvas().argb();fixtures.key(SDLK_ESCAPE);fixtures.draw();
        require(fixtures.canvas().argb()==fixture_current,"native league fixtures ignore Escape");
        fixtures.click(270,200,false);fixtures.draw();const auto fixture_future=fixtures.canvas().argb();
        require(fixture_future!=fixture_current,"native next-round arrow redraws fixtures");
        const auto fixture_work=decode_hok(fixtures.native_ui_bytes());
        require(fixture_work.date.day==15,"future fixture working date");
        const auto fixture_native=references.parent_path()/"exp"/"day_20261103"/"league-monat1-next.hok";
        if(fs::exists(fixture_native))require(fixture_work.state==decode_hok(read_file(fixture_native)).state,"all47761 native GUI fixture preview bytes");
        fixtures.click(40,48,false);fixtures.click(200,220,false);fixtures.click(200,220,false);fixtures.click(120,220,false);fixtures.draw();
        require(fixtures.canvas().argb()==fixture_future,"nested club/schedule return retains the previously painted fixture rows");
        const auto fixture_after_child=decode_hok(fixtures.native_ui_bytes());
        require(std::equal(fixture_work.state.begin()+0x8dfd,fixture_work.state.begin()+0x8e21,fixture_after_child.state.begin()+0x8dfd),"nested club schedule restores all36 fixture pairing bytes");
        fixtures.click(0,0,true);
        require(fixtures.screen()==ClassicScreen::Main&&fixtures.session().game().checkpoint_bytes()==fixtures_before,"league previews preserve canonical Game/RNG on exit");
        require(decode_hok(fixtures.native_ui_bytes()).state==decode_hok(fixtures.session().game().save_bytes()).state,"league main return discards temporary pairing and date fields");
        fixtures.click(40,170,false);fixtures.click(25,220,false);fixtures.draw();
        require(fixtures.canvas().argb()==fixture_current,"display round resets on native page reopening");
        fixtures.click(160,200,false);
        require(fixtures.screen()==ClassicScreen::LeagueCompetition,"native Play-Off middle frame");
        const auto playoff_work=decode_hok(fixtures.native_ui_bytes());
        require(playoff_work.date.day==6&&playoff_work.date.month==2&&playoff_work.date.year==1994,"invisible native Play-Off placeholder date is explicit");
        const auto playoff_native=references.parent_path()/"exp"/"day_20261103"/"playoff-monat1-empty.hok";
        if(fs::exists(playoff_native))require(playoff_work.state==decode_hok(read_file(playoff_native)).state,"all47761 native Play-Off placeholder working bytes");
        fixtures.key(SDLK_ESCAPE);require(fixtures.screen()==ClassicScreen::LeagueCompetition,"native Play-Off ignores Escape");
        fixtures.click(0,0,true);fixtures.click(40,170,false);fixtures.click(80,220,false);
        require(fixtures.screen()==ClassicScreen::LeagueCompetition,"single competition page selector persists across leagues");
        require(decode_hok(fixtures.native_ui_bytes()).date.day==11,"unavailable Aufstieg has no hidden date lookup");
        fixtures.click(100,210,false);require(fixtures.screen()==ClassicScreen::LeagueFixtures,"left Aufstieg Liga button returns7");
        fixtures.click(160,200,false);fixtures.click(220,210,false);
        require(fixtures.screen()==ClassicScreen::LeagueFixtures,"right Aufstieg Liga button returns7");
        fixtures.click(0,0,true);
        require(fixtures.session().game().checkpoint_bytes()==fixtures_before,"Play-Off and Aufstieg navigation never books games or RNG");
        ClassicApplication advertising(data,output,references/"MONAT1.HOK");
        advertising.click(40,110,false);advertising.click(80,220,false);
        require(advertising.screen()==ClassicScreen::Advertising,"native Manager-to-Werbung route");
        const auto advertising_refs=references.parent_path()/"exp"/"day_20261103";
        if(fs::exists(advertising_refs/"advertising-monat1-entry.hok"))
            require(decode_hok(advertising.session().game().save_bytes()).state==decode_hok(read_file(advertising_refs/"advertising-monat1-entry.hok")).state,"all47761 advertising entry state bytes via GUI");
        const auto ad_entered=advertising.session().game().checkpoint_bytes();
        advertising.key(SDLK_ESCAPE);require(advertising.screen()==ClassicScreen::Advertising,"native advertising base ignores Escape");
        advertising.motion(149,46);advertising.click(149,46,false);advertising.draw();const auto ad_offer=advertising.canvas().argb();
        advertising.click(141,187,false);advertising.draw();const auto previous_ad_offer=advertising.canvas().argb();
        require(previous_ad_offer!=ad_offer,"upper native spinner selects the previous offer");
        advertising.click(149,46,false);advertising.draw();require(advertising.canvas().argb()==previous_ad_offer,"same surface preserves the selected offer");
        advertising.click(141,196,false);advertising.draw();require(advertising.canvas().argb()==ad_offer,"lower spinner restores the next offer");
        advertising.click(285,174,false);advertising.draw();const auto ad_info=advertising.canvas().argb();
        require(ad_info!=ad_offer,"native Firmen-Info overlay");
        advertising.key(SDLK_ESCAPE);advertising.click(0,0,true);advertising.draw();require(advertising.canvas().argb()==ad_info,"Info waits for left acknowledgement");
        advertising.click(160,120,false);advertising.draw();require(advertising.canvas().argb()==ad_offer,"Info restores the existing offer browser");
        require(advertising.session().game().checkpoint_bytes()==ad_entered,"advertising selection/Info preserve HOK and RNG");
        advertising.click(285,191,false);
        if(fs::exists(advertising_refs/"advertising-monat1-accepted.hok"))
            require(decode_hok(advertising.session().game().save_bytes()).state==decode_hok(read_file(advertising_refs/"advertising-monat1-accepted.hok")).state,"all47761 native accepted offer state bytes via GUI");
        advertising.click(0,0,true);require(advertising.screen()==ClassicScreen::Advertising,"first browser right click remains in Werbung");
        advertising.click(0,0,true);require(advertising.screen()==ClassicScreen::Main,"second right click exits Werbung");
        ClassicApplication inactive_facility(data,output,references/"MONAT1.HOK");inactive_facility.render_screen("advertising-offer");
        const auto before_inactive=inactive_facility.session().game().checkpoint_bytes();
        require(inactive_facility.session().game().budget().stadium[2]==1,"native MONAT1 lower advertising facility is below level2");
        inactive_facility.click(280,140,false);inactive_facility.draw();inactive_facility.click(0,0,true);
        require(inactive_facility.screen()==ClassicScreen::Main&&inactive_facility.session().game().checkpoint_bytes()==before_inactive,"unbuilt facility leaves offer browser without booking or RNG");
        const auto make_ad_fixture=[&](const char* name,bool boards){
            auto h=decode_hok(read_file(references/"MONAT1.HOK"));
            const auto put=[&](size_t at,uint32_t n){for(size_t i=0;i<4;++i)h.state.at(at+i)=uint8_t(n>>(i*8));};
            const size_t offers=0xa52c+(boards?0:100);
            for(size_t i=0;i<10;++i)put(offers+i*10,0);
            put(offers,90000);h.state[offers+4]=1;h.state[offers+5]=12;h.state[offers+6]=0;h.state[offers+7]=0;
            if(boards){
                for(size_t i=0;i<6;++i){const size_t at=0xacfc+i*10;put(at,30000);h.state[at+4]=uint8_t(i);h.state[at+5]=12;h.state[at+6]=0;h.state[at+7]=0;}
                put(0xacfc+60,0);h.state[0xacfc+66]=0;h.state[0x01f2]=6;
            }else{put(0xacfc+70,0);h.state[0xacfc+76]=0;}
            const auto file=output/name;write_new_save(file,encode_hok(h));return file;
        };
        for(bool boards:{false,true}){
            ClassicApplication last_offer(data,output,make_ad_fixture(boards?"LASTBOARD.HOK":"SHIRT.HOK",boards));
            last_offer.render_screen("advertising");last_offer.click(boards?260:40,boards?40:120,false);
            const auto before_accept=last_offer.session().game().checkpoint_bytes();last_offer.motion(285,191);last_offer.click(285,191,false);
            require(last_offer.session().game().checkpoint_bytes()!=before_accept,"valid controlled final surface offer is booked");
            last_offer.click(0,0,true);require(last_offer.screen()==ClassicScreen::Main,"non-board and last-free-board acceptance return to outer advertising mode");
        }
        ClassicApplication renewal(data,output,references/"ADRENEW.HOK");renewal.render_screen("advertising");
        const auto before_renewal=renewal.session().game().checkpoint_bytes();renewal.click(214,40,false);renewal.draw();
        const auto pending_renewal=renewal.session().game().checkpoint_bytes();const auto renewal_pixels=renewal.canvas().argb();
        require(pending_renewal!=before_renewal,"native renewal sponsor decides before question");
        renewal.key(SDLK_ESCAPE);renewal.click(0,0,true);renewal.draw();
        require(renewal.session().game().checkpoint_bytes()==pending_renewal&&renewal.canvas().argb()==renewal_pixels,"renewal ignores Escape/right click while pending");
        renewal.click(190,172,false);require(renewal.session().game().checkpoint_bytes()!=pending_renewal,"explicit Nein resolves the prepared contract");
        ClassicApplication wm(data,output,references/"MONAT1.HOK");
        const auto wm_before=wm.session().game().checkpoint_bytes();
        wm.click(275,110,false);wm.click(293,220,false);wm.motion(0,0);wm.draw();
        require(wm.screen()==ClassicScreen::WorldChampionship,"native tables-to-WM route");
        const auto wm_group1=wm.canvas().argb();
        wm.key(SDLK_ESCAPE);wm.draw();
        require(wm.canvas().argb()==wm_group1,"WM ignores Escape");
        wm.click(260,217,false);wm.draw();const auto wm_group2=wm.canvas().argb();
        require(wm_group2!=wm_group1,"WM standings group selector");
        wm.click(0,0,true);wm.click(275,110,false);wm.click(293,220,false);wm.draw();
        require(wm.canvas().argb()==wm_group2,"WM standings selector persists on reopening");
        wm.click(0,0,true);wm.click(40,170,false);wm.click(293,220,false);wm.draw();
        require(wm.screen()==ClassicScreen::WorldChampionship,"native pairings-to-WM route");
        const auto wm_fixtures=wm.canvas().argb();
        wm.click(255,200,false);wm.draw();
        require(wm.canvas().argb()!=wm_fixtures,"WM right arrow advances the displayed round");
        wm.click(60,200,false);wm.draw();
        require(wm.canvas().argb()==wm_fixtures,"WM left arrow restores the displayed round");
        wm.click(190,200,false);wm.draw();const auto wm_fixtures2=wm.canvas().argb();
        wm.click(120,200,false);wm.draw();
        require(wm.canvas().argb()!=wm_fixtures2,"WM Endrunde opens the native placeholder");
        require(wm.session().game().checkpoint_bytes()==wm_before,"WM preview date never leaks into canonical game or RNG");
        require(decode_hok(wm.native_ui_bytes()).state!=decode_hok(wm.session().game().save_bytes()).state,"WM end-round preview has an explicit working date");
        const auto wm_native=references.parent_path()/"exp"/"day_20261103"/"wm-monat1-final.hok";
        if(fs::exists(wm_native))require(decode_hok(wm.native_ui_bytes()).state==decode_hok(read_file(wm_native)).state,"WM preview matches all47761 bytes of native DOSBox RAM");
        wm.click(160,220,false);wm.draw();
        require(wm.canvas().argb()==wm_fixtures2,"WM Gruppe returns to the previous fixtures group");
        wm.click(0,0,true);
        require(decode_hok(wm.native_ui_bytes()).state==decode_hok(wm.session().game().save_bytes()).state,"WM exit normalizes and discards the working date");
        require(wm.session().game().checkpoint_bytes()==wm_before,"WM navigation is state and RNG neutral");
        ClassicApplication strengths(data,output,references/"START.HOK");
        strengths.click(120,170,false);strengths.click(20,220,false);
        require(strengths.screen()==ClassicScreen::Strengths,"native first-league strengths route");
        const auto strengths_state=strengths.session().game().checkpoint_bytes();
        strengths.click(150,218,false);strengths.draw();strengths.click(230,218,false);strengths.draw();
        require(strengths.session().game().checkpoint_bytes()==strengths_state,"strengths mode changes and repaint have no state/RNG effects");
        strengths.key(SDLK_ESCAPE);require(strengths.screen()==ClassicScreen::Strengths,"native strengths page ignores Escape");
        strengths.click(319,35,false);strengths.draw();const auto club_pixels=strengths.canvas().argb();
        strengths.key(SDLK_ESCAPE);strengths.draw();
        require(strengths.canvas().argb()==club_pixels,"native club-info ignores Escape and accepts Y-only list hit");
        strengths.click(270,221,false);
        require(decode_hok(strengths.session().game().save_bytes()).state.at(0x190F)==71,"club-info reporting button persists ESV headphones mode");
        strengths.click(270,221,false);strengths.motion(272,220);strengths.draw();
        require_native_rgb(strengths.canvas(),references.parent_path()/"exp/day_20261103/raster_goldens/clubinfo-camera-start.rgb");
        const auto schedule_refs=references.parent_path()/"exp/day_20261103";
        strengths.motion(200,220);strengths.click(200,221,false);strengths.draw();
        if(fs::exists(schedule_refs/"schedule-esv-first.hok"))
            require(decode_hok(strengths.native_ui_bytes()).state==decode_hok(read_file(schedule_refs/"schedule-esv-first.hok")).state,"all47761 native schedule working bytes, including temporary date/pairings");
        strengths.key(SDLK_ESCAPE);strengths.click(200,221,false);
        if(fs::exists(schedule_refs/"schedule-esv-info-return.hok"))
            require(decode_hok(strengths.native_ui_bytes()).state==decode_hok(read_file(schedule_refs/"schedule-esv-info-return.hok")).state,"Info restores pairings and retains the native temporary date");
        strengths.click(270,221,false);strengths.click(270,221,false);strengths.click(270,221,false);
        if(fs::exists(schedule_refs/"schedule-esv-info-return.hok"))
            require(decode_hok(strengths.native_ui_bytes()).state==decode_hok(read_file(schedule_refs/"schedule-esv-info-return.hok")).state,"native temporary date survives staged reporting actions inside Info");
        strengths.click(0,0,true);strengths.draw();
        require(strengths.screen()==ClassicScreen::Strengths&&strengths.canvas().argb()!=club_pixels,"right click restores strengths under club modal");
        strengths.click(0,0,true);require(strengths.screen()==ClassicScreen::Main,"right click exits native strengths");
        if(fs::exists(schedule_refs/"schedule-esv-main-return.hok"))
            require(decode_hok(strengths.native_ui_bytes()).state==decode_hok(read_file(schedule_refs/"schedule-esv-main-return.hok")).state,"main return normalizes date exactly and retains reporting choice");
        ClassicApplication cup(data,output,references/"MONAT1.HOK");
        cup.click(280,110,false);cup.click(180,220,false);
        require(cup.screen()==ClassicScreen::Cup,"native Tabellen/Pokal route");
        cup.draw();const auto cup_pixels=cup.canvas().argb();cup.click(250,92,false);cup.draw();
        require(cup.canvas().argb()!=cup_pixels,"Cup away row opens native club-info mode1");
        cup.key(SDLK_ESCAPE);cup.click(0,0,true);cup.draw();
        require(cup.screen()==ClassicScreen::Cup&&cup.canvas().argb()==cup_pixels,"club-info right close restores Cup page");
        cup.click(0,0,true);require(cup.screen()==ClassicScreen::Main,"Cup right click returns to main");
        require(decode_hok(cup.native_ui_bytes()).date.day==11,"Cup preview date is normalized at main return");
        ClassicApplication euro(data,output,references/"MONAT1.HOK");
        const auto euro_before=euro.session().game().checkpoint_bytes();euro.render_screen("euro-group1");
        euro.click(50,54,false);euro.click(0,0,true); //fixture info then restore group
        euro.click(103,215,false);euro.draw();const auto group2=euro.canvas().argb();
        euro.click(217,215,false);euro.draw();const auto endround=euro.canvas().argb();
        require(endround!=group2,"Euro footer selects group2 then unassigned final page");
        euro.click(50,54,false);euro.key(SDLK_ESCAPE);euro.draw();
        require(euro.canvas().argb()==endround,"undrawn Euro final disables team clicks and ignores Escape");
        euro.click(0,0,true);euro.click(280,110,false);euro.click(235,220,false);euro.draw();
        require(euro.screen()==ClassicScreen::EuroCup&&euro.canvas().argb()==endround,"Euro main-menu reopen retains original runtime page selector");
        require(euro.session().game().checkpoint_bytes()==euro_before,"competition viewing leaves canonical Game state and RNG unchanged");
        bank.click(20,150,false);bank.click(158,94,false);
        bank.click(10,80,false);bank.click(10,80,false);bank.click(10,80,false);bank.click(100,80,false);
        auto bought_gui=decode_hok(bank.session().game().save_bytes());
        require(le32(bought_gui.state,0x3C)==3&&le32(bought_gui.state,0x38)==730,"native PlayTime selection/fine-adjust purchase");
        bank.click(20,150,false);bank.click(161,94,false);bank.click(210,80,false);
        auto sold_gui=decode_hok(bank.session().game().save_bytes());
        require(le32(sold_gui.state,0x3C)==2&&le32(sold_gui.state,0x38)==481,"native price-bar partial sale");
        ClassicApplication lineup(data,output,references/"START.HOK");
        lineup.click(120,110,false);lineup.click(20,220,false);
        require(lineup.screen()==ClassicScreen::Lineup,"native lineup route");
        lineup.click(20,39,false); // remove the selected second goalkeeper and disable auto
        require(lineup.session().game().save().squad[1].role==0,"lineup click removes selected goalie");
        const auto manual_checkpoint=lineup.session().game().checkpoint_bytes();
        lineup.click(0,0,false); // native auto-disabled notice
        lineup.click(280,195,false);lineup.click(20,39,false);
        require(lineup.session().game().save().squad[1].role==1,"selected TW panel assigns goalie again");
        require(lineup.session().game().checkpoint_bytes()!=manual_checkpoint,"subsequent staged action preserves manual edits");
        lineup.click(280,230,false);
        require(lineup.screen()==ClassicScreen::Tactics,"native lineup-to-tactics shortcut");
        lineup.click(117,55,false); // AUTOM. -> MANUELL (already manual remains bounded)
        lineup.click(118,129,false); // lineMode0 ->1
        lineup.click(0,0,true);
        require(lineup.screen()==ClassicScreen::Main,"right click commits tactics and exits");
        const auto tactics_save=decode_hok(lineup.session().game().save_bytes());
        require(tactics_save.state.at(0x01F9)==1,"native tactics mode arrow committed");
        require(tactics_save.state.at(0x025B)==2&&tactics_save.state.at(0x025C)==3&&tactics_save.state.at(0x025D)==3&&tactics_save.state.at(0x025E)==0,"native tactics exit normalizes zero quotas");
        require(tactics_save.state.at(0x078A)==0&&tactics_save.state.at(0x078B)==1,"menu edit preserves native pre-match backup phase");
        const auto exe=read_file(data/"EM.EXE");
        Game next_match(exe,lineup.session().game().checkpoint_bytes());
        next_match.advance_day();
        require(next_match.save().state.at(0x078A)==0&&next_match.save().state.at(0x078B)==0,"day loop keeps the manager's new manual mode");
        Game resumed(exe,next_match.checkpoint_bytes());
        next_match.advance_day();resumed.advance_day();
        require(next_match.checkpoint_bytes()==resumed.checkpoint_bytes(),"manual tactics continue identically after checkpoint reload");
        Game market_mode(exe,lineup.session().game().checkpoint_bytes());
        require(market_mode.list_transfer_player(0)==TransferMoveResult::Moved,"own reserve can be listed after tactics edit");
        require(market_mode.save().state.at(0x078A)==0,"roster refresh must not restore stale automatic backup");
        ClassicApplication detail(data,output,references/"START.HOK");
        detail.render_screen("lineup");
        const auto before_detail=detail.session().game().checkpoint_bytes();
        detail.click(50,31,true);detail.draw();
        const auto modal_pixels=detail.canvas().argb();
        detail.key(SDLK_ESCAPE);detail.click(0,0,true);detail.draw();
        require(detail.canvas().argb()==modal_pixels,"native detail ignores Escape and right click");
        require(detail.session().game().checkpoint_bytes()==before_detail,"unflagged detail quote/repaint is state and RNG neutral");
        detail.click(200,214,false);detail.draw();
        require(detail.canvas().argb()!=modal_pixels&&detail.screen()==ClassicScreen::Lineup,"Aha returns to preserved lineup page");
        ClassicApplication transfer(data,output,references/"START.HOK");
        transfer.render_screen("transfer");
        transfer.key(SDLK_ESCAPE);
        require(transfer.screen()==ClassicScreen::Transfer,"native transfer base page ignores Escape");
        const int first_id=transfer.session().game().save().state.at(0x696D+15);
        transfer.click(25,31,false);
        auto transfer_hok=decode_hok(transfer.session().game().save_bytes());
        bool listed=false;size_t listed_slot=0;
        for(size_t slot=0;slot<12;slot++)if(transfer_hok.state.at(0x696D+4*0x618+slot*52+15)==first_id){listed=true;listed_slot=slot;}
        require(listed,"native own-roster transfer click lists the full player record");
        transfer.click(200,40+int(listed_slot)*16,false);
        transfer_hok=decode_hok(transfer.session().game().save_bytes());
        bool recalled=false;for(const auto& player:transfer_hok.squad)if(player.player>=0&&transfer_hok.players[size_t(player.player)].raw==transfer_hok.state.data()+0x4FC6+size_t(first_id-1)*33)recalled=true;
        require(recalled,"native own-pool click recalls the player");
        const auto preview_dir=output/"preview";fs::create_directories(preview_dir);
        // Use a new native identity: a second copy of START is correctly
        // rejected by the original history guard despite a different filename.
        Game preview_source(exe,read_file(references/"START.HOK"),2026);
        write_new_save(preview_dir/"REF.HOK",preview_source.native_save_bytes());
        ClassicApplication files(data,preview_dir,references/"START.HOK");
        files.render_screen("load");files.click(240,212,false);files.draw();
        const auto file_pixels=files.canvas().argb();
        files.click(50,68,true);files.draw();
        require(files.canvas().argb()!=file_pixels,"right-click save header preview overlays the chooser");
        const auto held_preview=files.canvas().argb();files.key(SDLK_ESCAPE);files.click(0,0,false);files.draw();
        require(files.canvas().argb()==held_preview,"native preview remains while held, ignoring Escape and another button-down");
        files.release();files.draw();
        require(files.canvas().argb()==file_pixels,"preview restores the chooser on right mouse release");
        files.click(50,68,false);require(files.screen()==ClassicScreen::Files,"first file click selects only");
        files.click(50,68,false);require(files.screen()==ClassicScreen::Main,"second file click loads");
        const auto native_folder=references.parent_path()/"exp"/"day_20261103";
        if(fs::exists(native_folder/"nativepurchase-before.hok")){
            Game reference_purchase(exe,read_file(native_folder/"nativepurchase-before.hok"),1617108858u);
            const auto purchase_file=output/"PURCHASE.OEM";
            write_new_save(purchase_file,reference_purchase.checkpoint_bytes());
            ClassicApplication purchase(data,output,purchase_file);
            purchase.render_screen("transfer");purchase.motion(204,91);
            const auto before_purchase=purchase.session().game().checkpoint_bytes();
            purchase.click(205,92,false);purchase.text_input("400000");purchase.key(SDLK_ESCAPE);
            require(purchase.session().game().checkpoint_bytes()==before_purchase,"fee Escape consumes no RNG and changes no state");
            purchase.click(205,92,false);purchase.text_input("400000");purchase.key(SDLK_RETURN);
            require(decode_hok(purchase.session().game().save_bytes()).state==decode_hok(read_file(native_folder/"nativepurchase-pending.hok")).state,"GUI fee stage matches all native pending state bytes");
            purchase.motion(205,91);purchase.draw();write_new_save(output/"purchase-pending.bmp",purchase.canvas().bmp_bytes());
            const auto purchase_golden=native_folder/"raster_goldens"/"purchase-contract-m0.rgb";
            require_native_rgb(purchase.canvas(),purchase_golden);
            const auto pending=purchase.session().game().checkpoint_bytes();purchase.key(SDLK_ESCAPE);
            require(purchase.session().game().checkpoint_bytes()==pending,"Escape does not abandon native contract modal");
            purchase.click(110,85,false);
            require(decode_hok(purchase.session().game().save_bytes()).state==decode_hok(read_file(native_folder/"nativepurchase-cancel.hok")).state,"GUI Abbruch matches all native cancellation state bytes");
        }
        std::printf("Classic input/save integration passed\n");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"Classic input: %s\n",e.what());return 1;}
}
