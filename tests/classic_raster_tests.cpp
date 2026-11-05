// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_app.hpp"
#include <SDL.h>
#include <array>
#include <cctype>
#include <cstdio>
#include <stdexcept>

using namespace em;
using namespace em::ui;
int main(int argc,char**argv){
    try{
        if(argc!=4)throw std::runtime_error("classic_raster_tests DATA REFERENCE GOLDENS");
        const auto data=path_from_text(argv[1]),references=path_from_text(argv[2]),goldens=path_from_text(argv[3]);
        struct Scene{const char* name;int mouse_x,mouse_y;const char* save="START";const char* source=nullptr;};
        constexpr std::array<Scene,43> scenes{{{"main",125,200},{"table",25,220},{"training",240,220},
                                            {"contracts",282,239},{"bank",25,220},{"totals",25,220},
                                            {"lineup",25,220,"S1"},{"tactics",280,225,"S1"},
                                            {"transfer",187,219,"M0"},{"player",54,30,"M0"},
                                            {"strengths",27,219},{"strengths-defence",150,218},{"strengths-attack",230,218},{"clubinfo",74,34},
                                            {"team-schedule",201,220},{"team-schedule-second",273,220},
                                            {"cup",187,219,"MONAT1"},{"cup-away",250,91,"MONAT1"},{"cup-home",49,91,"MONAT1"},
                                            {"euro-group1",240,219,"MONAT1"},{"euro-group2",103,215,"MONAT1"},{"euro-final",216,215,"MONAT1"},
                                            {"wm-group1",293,219,"MONAT1"},{"wm-group2",264,217,"MONAT1"},
                                            {"wm-fixtures1",293,219,"MONAT1"},{"wm-fixtures-next",257,199,"MONAT1"},
                                            {"wm-fixtures2",192,199,"MONAT1"},{"wm-final",127,199,"MONAT1"},{"wm-club",99,64,"MONAT1"},
                                            {"league-fixtures1",27,219,"MONAT1"},{"league-fixtures-next",275,199,"MONAT1"},{"league-fixtures-past",55,199,"MONAT1"},
                                            {"league-fixtures2",105,219,"MONAT1"},{"league-fixtures3",132,219,"MONAT1"},{"league-playoff",159,199,"MONAT1"},
                                            {"promotion1",80,219,"MONAT1"},{"promotion2",160,199,"MONAT1"},
                                            {"advertising",80,219,"MONAT1"},{"advertising-offer",150,46,"MONAT1"},
                                            {"advertising-info",285,174,"MONAT1"},{"advertising-accepted",285,191,"MONAT1"},
                                            {"main-files-return",110,200,"H1","../exp/day_20261103/gui-h1-export.hok"},
                                            {"advertising-inactive",280,139,"H2","../exp/day_20261103/native-h2-dialog.hok"}}};
        int failures=0;
        for(const auto& scene:scenes){
            ClassicApplication app(data,goldens/"unused-save-output",references/(scene.source?std::string(scene.source):std::string(scene.save)+".HOK"));
            // Native modal entry preserves the help painted under the pointer.
            // Position it before opening, as the CLI and real mouse path do.
            app.motion(scene.mouse_x,scene.mouse_y);app.render_screen(scene.name);app.draw();
            std::string stem=scene.save;for(char& c:stem)c=char(std::tolower(static_cast<unsigned char>(c)));
            const auto expected=read_file(goldens/(std::string(scene.name)+"-"+stem+".rgb"));
            if(expected.size()!=320*240*3)throw std::runtime_error("Invalid native RGB golden size");
            const auto actual=app.canvas().argb();size_t differences=0,first=0;
            for(size_t pixel=0;pixel<actual.size();pixel++){
                const uint32_t rgb=uint32_t(expected[pixel*3])<<16|uint32_t(expected[pixel*3+1])<<8|expected[pixel*3+2];
                if((actual[pixel]&0xffffff)!=rgb){if(!differences)first=pixel;differences++;}
            }
            if(differences){std::fprintf(stderr,"%s: %zu native pixel differences, first at%zu,%zu\n",scene.name,differences,first%320,first/320);failures++;}
            else std::printf("%s:76800/76800 pixels match native DOSBox capture\n",scene.name);
        }
        return failures?1:0;
    }catch(const std::exception& e){std::fprintf(stderr,"Classic raster: %s\n",e.what());return 2;}
}
