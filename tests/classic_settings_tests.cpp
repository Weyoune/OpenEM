// SPDX-License-Identifier: GPL-3.0-only
// Einstellungen page: pixel comparison with native DOSBox captures (work/exp/ui_20261107) and behaviour.
#include "ui/classic_app.hpp"
#include "ui/classic_settings.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace em;
using namespace em::ui;
static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static size_t differences(const ClassicCanvas& canvas,const fs::path& golden){
    const auto expected=read_file(golden);
    require(expected.size()==320*240*3,"native RGB golden size");
    const auto actual=canvas.argb();size_t count=0;
    for(size_t pixel=0;pixel<actual.size();pixel++){
        const uint32_t rgb=uint32_t(expected[pixel*3])<<16|uint32_t(expected[pixel*3+1])<<8|expected[pixel*3+2];
        if((actual[pixel]&0xffffff)!=rgb)count++;
    }
    if(count&&std::getenv("EM_DUMP_MISMATCH")){
        const fs::path out=path_from_text(std::getenv("EM_DUMP_MISMATCH"))/(golden.stem().string()+".bmp");
        std::error_code ec;fs::remove(out,ec);write_new_save(out,canvas.bmp_bytes());
    }
    return count;
}
int main(int argc,char** argv){
    try{
        if(argc!=5)throw std::runtime_error("classic_settings_tests DATA REFERENCE GOLDENS OUTPUT");
        const fs::path data=path_from_text(argv[1]),references=path_from_text(argv[2]),goldens=path_from_text(argv[3]);
        const auto serial=std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path output=path_from_text(argv[4])/std::to_string(serial);
        // Hit areas are the native DS:4C06/4BE8 tables.
        require(classic_settings_hit(222,130).widget==0&&classic_settings_hit(232,142).widget==0&&classic_settings_hit(233,142).widget<0,"small switch area is 11x13");
        require(classic_settings_hit(200,24).widget==9&&classic_settings_hit(218,46).widget==9&&classic_settings_hit(219,46).widget<0,"large switch area is 19x23");
        require(classic_settings_hit(191,90).speed==0&&classic_settings_hit(285,90).speed==70&&classic_settings_hit(190,90).speed<0&&classic_settings_hit(286,90).speed<0,"speed row 191..285");
        {
            ClassicApplication app(data,output,references/"START.HOK");
            const auto before=app.session().game().checkpoint_bytes();
            app.motion(127,219);app.render_screen("settings");app.draw();
            require(differences(app.canvas(),goldens/"settings-start.rgb")==0,"settings page matches the native capture in all 76800 pixels");
            require(app.session().game().checkpoint_bytes()==before,"opening and drawing leave the game untouched");
            const auto defaults=app.session().game().settings();
            require(defaults.speed==70&&defaults.flags[10]==1&&defaults.scrolling[0]==1&&defaults.end_year>=2000&&defaults.level==2,"native defaults");
            // The same clicks as the native capture session.
            app.click(227,136,false);app.click(287,164,false);app.click(210,35,false);app.click(230,65,false);app.click(250,90,false);
            app.motion(227,69);app.draw();
            require(app.screen()==ClassicScreen::Settings,"settings page stays open");
            require(differences(app.canvas(),goldens/"settings-changed.rgb")==0,"changed settings match the native capture in all 76800 pixels");
            const auto changed=app.session().game().settings();
            require(changed.flags[0]==1&&changed.flags[8]==1&&changed.flags[9]==1&&changed.scrolling[0]==0&&changed.speed==50,"toggles reach the game state");
            require(app.session().dirty(),"a setting change marks the game dirty");
            // Torszenen cycles 0,1,2,0; the rink hint appears once.
            app.motion(209,35);app.click(209,35,false);
            require(app.session().game().settings().flags[9]==2,"Torszenen second step");
            app.draw();
            app.motion(209,34);app.draw();//the native pointer sat one row higher than the click position
            require(differences(app.canvas(),goldens/"settings-hint.rgb")==0,"modal rink hint over the not yet redrawn page matches the native capture");
            app.click(121,171,false);// dismiss the hint: the page is redrawn with the lens
            app.motion(121,170);app.draw();
            require(differences(app.canvas(),goldens/"settings-rink.rgb")==0,"Torszenen mode 2 matches the native capture");
            app.click(210,35,false);app.click(210,35,false);app.click(210,35,false);
            require(app.session().game().settings().flags[9]==2&&app.screen()==ClassicScreen::Settings,"the hint is shown only once");
            app.click(210,35,false);
            require(app.session().game().settings().flags[9]==0,"Torszenen wraps to off");
            app.draw();
            // Save and reload: the bytes are part of the checkpoint.
            app.click(0,0,true);
            require(app.screen()==ClassicScreen::Main,"right click leaves the page");
            const fs::path saved=output/"SETT.OEM";
            app.click(180,185,false);app.click(80,220,false);app.click(100,200,false);app.text_input("SETT");app.key(SDLK_RETURN);
            require(fs::exists(saved),"checkpoint created through the save page");
            ClassicApplication loaded(data,output,saved);
            const auto after=loaded.session().game().settings(),now=app.session().game().settings();
            require(after.flags==now.flags&&after.scrolling==now.scrolling&&after.speed==now.speed,"settings survive save and load");
        }
        {
            ClassicApplication app(data,output,references/"START.HOK");
            app.click(180,185,false);
            app.click(110,222,false);
            require(app.screen()==ClassicScreen::Settings,"Gemischtes -> Einst. opens the page");
            const auto before=app.session().game().checkpoint_bytes();
            app.click(250,90,false);
            require(app.session().game().checkpoint_bytes()!=before,"speed changes the state");
            app.click(190,90,false);
            require(app.session().game().settings().speed==50,"clicks off the penguins do nothing");
        }
        std::printf("classic settings: ok\n");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"Classic settings: %s\n",e.what());return 1;}
}
