// SPDX-License-Identifier: GPL-3.0-only
// Match-day presentation of the classic UI: banner before the own league match (native capture work/exp/ui_20261107).
#include "ui/classic_app.hpp"
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
    return count;
}
int main(int argc,char** argv){
    try{
        if(argc!=5)throw std::runtime_error("classic_match_tests DATA REFERENCE GOLDENS OUTPUT");
        const fs::path data=path_from_text(argv[1]),references=path_from_text(argv[2]),goldens=path_from_text(argv[3]);
        const auto serial=std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path output=path_from_text(argv[4])/std::to_string(serial);
        {
            ClassicApplication app(data,output,references/"START.HOK");
            app.motion(292,49);app.render_screen("match-banner");app.draw();
            require(differences(app.canvas(),goldens/"match-banner.rgb")==0,"Ligaspiel banner matches the native capture in all 76800 pixels");
        }
        {
            // The crest starts the day: the banner comes first, then the day is played.
            ClassicApplication app(data,output,references/"START.HOK");
            const int day=app.session().game().season_day();
            app.motion(292,49);app.click(292,49,false);
            app.tick();app.draw();
            require(app.session().game().season_day()==day,"nothing is played while the banner shows");
            require(differences(app.canvas(),goldens/"match-banner.rgb")==0,"banner appears before the first league match");
            SDL_Delay(1000);app.tick();app.draw();
            require(app.session().game().season_day()>day,"the match day is played after the banner");
            require(differences(app.canvas(),goldens/"match-banner.rgb")!=0,"banner is gone after the day");
        }
        std::printf("classic match: ok\n");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"Classic match: %s\n",e.what());return 1;}
}
