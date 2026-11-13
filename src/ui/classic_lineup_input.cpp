// SPDX-License-Identifier: GPL-3.0-only
// Input handling of the classic line-up page: selecting a block, assigning
// players, removing numbers from the diagram, reserve toggling, and keeping
// the frontend's copy of the diagram in step with the game's changes.
#include "ui/classic_app.hpp"
#include "ui/classic_lineup.hpp"
#include <algorithm>

namespace em::ui {
namespace {
// The game's notice text for a line-up refusal (empty for None).
std::string lineup_message(ManualLineupMessage message){
    switch(message){
        case ManualLineupMessage::None:return {};
        case ManualLineupMessage::Injured:return "Spieler ist\nnoch verletzt !";
        case ManualLineupMessage::Suspended:return "Spieler ist\nnoch gesperrt !";
        case ManualLineupMessage::TeamFull:return "Schon 22 Mann\nim Team !";
        case ManualLineupMessage::NoLineSelected:return "Keine Reihe\nausgewählt !";
        case ManualLineupMessage::WrongRole:return "Der darf da\nnicht rein !";
        case ManualLineupMessage::SixFieldPlayers:return "Schon 6 Feldspieler\nauf dem Eis !";
        case ManualLineupMessage::NoSpace:return "Kein\nPlatz mehr !";
    }
    return {};
}
}
// Opens the line-up page with no block selected and builds the diagram; the
// six-field-player mode is on when block 1 holds six players and no
// goalkeeper sits in cell 4 of row 4.
void ClassicApplication::open_lineup(){
    hover_kind_=ClassicRosterKind::Lineup;roster_hover_={};roster_rules_=false;
    screen_=ClassicScreen::Lineup;selected_line_=0;lineup_cells_=classic_lineup_cells(snapshot(),squad_manager());
    const auto& first=(*lineup_cells_)[0];
    lineup_six_=std::count_if(first.begin(),first.end(),[](int n){return n!=0;})==6&&!(*lineup_cells_)[4][4];
}
// Lets the game apply a line-up action to squad slot, then mirrors the
// change in the diagram: the player's old cell is cleared (in six-player
// mode the sixth cell moves into the gap and the mode ends) and the cell the
// game assigned is filled. Shows the game's notice, preceded by
// "Auto-Aufstellung ist deaktiviert !" when the change switched the
// automatic line-up off.
void ClassicApplication::apply_lineup(size_t slot,ManualLineupAction action){
    try{
        ManualLineupResult result;
        session_.apply([&](Game& game){result=game.change_lineup(slot,selected_line_,action,&lineup_six_,lineup_cells_?&*lineup_cells_:nullptr,live_day_&&live_screen_);return ActionResult{{},result.message==ManualLineupMessage::None};});
        invalidate();selected_line_=result.selected_line;
        if(result.changed&&lineup_cells_){
            int old_block=-1,old_cell=-1;
            for(int block=0;block<5&&old_block<0;block++)for(int cell=0;cell<6;cell++)
                if((*lineup_cells_)[size_t(block)][size_t(cell)]==int(slot)+1){old_block=block;old_cell=cell;break;}
            if(old_block>=0){
                auto& cells=(*lineup_cells_)[size_t(old_block)];
                cells[size_t(old_cell)]=0;
                if(old_block==0&&lineup_six_&&(action==ManualLineupAction::RemoveNumber||old_cell>2)){
                    cells[size_t(old_cell)]=cells[5];
                    if(action==ManualLineupAction::RemoveNumber)cells[5]=0;
                    lineup_six_=false;
                }
            }
            if(result.assigned_cell>=0&&selected_line_>=1&&selected_line_<=5){
                auto& cells=(*lineup_cells_)[size_t(selected_line_-1)];
                if(selected_line_==5)cells[4]=int(slot)+1;
                else{
                    cells[size_t(result.assigned_cell)]=int(slot)+1;
                    if(selected_line_==1&&result.assigned_cell==5)lineup_six_=true;
                }
            }
        }
        const auto message=lineup_message(result.message);
        if(result.automatic_disabled){notice("Auto-Aufstellung\nist deaktiviert !");if(!message.empty())pending_notices_.push_back(message);}
        else if(!message.empty())notice(message);
    }catch(const std::exception& e){notice(e.what());}
}
// Clicks on the line-up page. Right button: on the name column opens the
// player page, elsewhere in the list toggles the reserve, outside the list
// returns to the main menu. Left button: the title bar returns, the bottom
// right opens the tactics, a list row assigns the player to the selected
// block, a block box selects the block and a number plate in it removes
// that player. List rows are 6 pixels high from y = 29.
void ClassicApplication::click_lineup(int x,int y,bool right){
    const auto& h=snapshot();
    int count=0;for(int slot=0;slot<29;slot++)if(h.state.at(squad_block(squad_manager())+size_t(slot)*52+15))++count;
    const int slot=(y-29)/6;
    const bool roster=x>=0&&x<=233&&slot>=0&&slot<count;
    if(right){
        if(!roster){screen_=ClassicScreen::Main;return;}
        if(x>34&&x<96){open_player(slot);return;}
        apply_lineup(size_t(slot),ManualLineupAction::ReserveToggle);return;
    }
    if(y<14){screen_=ClassicScreen::Main;return;}
    if(x>245&&y>224){open_tactics();return;}
    if(roster){apply_lineup(size_t(slot),ManualLineupAction::Select);return;}
    for(int line=0;line<5;line++)if(x>=246&&x<=318&&y>=15+42*line&&y<=53+42*line){
        selected_line_=line+1;
        if(!lineup_cells_)lineup_cells_=classic_lineup_cells(h,squad_manager());
        int cell=-1;
        if(line==4){if(x>=276&&x<=292&&y>=203&&y<=213)cell=4;}
        else{
            if(y>=22+42*line&&y<=32+42*line&&x>=258&&x<=310)cell=(x-258)/18;
            else if(y>=35+42*line&&y<=45+42*line){
                if(line==0&&lineup_six_&&x>=258&&x<=310)cell=3+(x-258)/18;
                else if(x>=267&&x<=301)cell=3+int(x>284);
            }
        }
        if(cell>=0){const int number=(*lineup_cells_)[size_t(line)][size_t(cell)];if(number)apply_lineup(size_t(number-1),ManualLineupAction::RemoveNumber);}
        return;
    }
}
} // namespace em::ui
