// SPDX-License-Identifier: GPL-3.0-only
// Tests for the stadium actions of Game (ticket price, construction question with Yes/No) on a
// reference position of human manager 2 (stadium-h2-*.hok, recorded with the original game).
// After each step the complete save state must equal the recorded one, the random draws must
// match, and an OpenEM checkpoint taken at any point must continue identically. Finally the
// per-session refusal gates (kept only in memory by the original) must survive UI session
// transactions and loading another save into the running session.
// Arguments: the game data folder (EM.EXE) and the folder holding the reference saves.
#include "game/game.hpp"
#include "game/checkpoint.hpp"
#include "ui/session.hpp"
#include <cstdio>
#include <stdexcept>
using namespace em;
// Throws `message` as a test failure unless `value`.
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("stadium_game_tests DATA NATIVE");
    const std::filesystem::path data=argv[1],native=argv[2];
    const auto exe=read_file(data/"EM.EXE"),source=read_file(native/"stadium-h2-entry.hok");
    Game game(exe,source,2222449849u);
    const auto original=game.checkpoint_bytes();const auto initial=decode_game_checkpoint(original);
    // Ticket price: 9 and 31 are out of range, 13 is the current price (no-op).
    require(!game.set_ticket_price(9)&&!game.set_ticket_price(31)&&!game.set_ticket_price(13)&&game.checkpoint_bytes()==original,"ticket bounds and no-op are fully neutral");
    require(game.set_ticket_price(14),"native ticket increase accepted");
    const auto matches=[&](const char* stem){return decode_hok(game.save_bytes()).state==decode_hok(read_file(native/(std::string("stadium-h2-")+stem+".hok"))).state;};
    require(matches("ticket14"),"all47761 native ticket bytes");
    require(decode_game_checkpoint(game.checkpoint_bytes()).borland_random==initial.borland_random,"ticket editing has no RNG");
    // Construction of facility 8 (bowling) at its initial quote, declined with No.
    StadiumBuildDraft bowling;
    require(game.begin_stadium_build(8,-1,bowling)==StadiumBuildResult::Prepared,"native bowling offer prepared");
    require(bowling.weeks==10&&bowling.discount==0&&bowling.nominal_cost==1350000,"independent native bowling question values");
    require(matches("pending-bowling"),"all47761 native pending bowling bytes before question answer");
    auto pending=decode_game_checkpoint(game.checkpoint_bytes());
    require(pending.borland_random==3025389076u&&pending.game_random==initial.game_random&&pending.stadium_refusals[0][8]==1,"native three-draw RNG and pending refusal gate");
    Game pending_reload(exe,game.checkpoint_bytes());
    require(pending_reload.checkpoint_bytes()==game.checkpoint_bytes(),"OEM preserves pending serialized weeks/RNG/gate");
    require(!game.finish_stadium_build(bowling,false)&&matches("declined-bowling"),"native No clears weeks and no other state bytes");
    require(decode_game_checkpoint(game.checkpoint_bytes()).borland_random==pending.borland_random&&game.stadium_refusals()[0][8]==1,"No consumes no extra RNG and leaves refusal gate");
    const auto declined=game.checkpoint_bytes();
    StadiumBuildDraft ignored;
    require(game.begin_stadium_build(8,-1,ignored)==StadiumBuildResult::Blocked&&game.checkpoint_bytes()==declined,"repeat quote blocked without RNG/state changes");
    require(!game.finish_stadium_build(bowling,false)&&game.checkpoint_bytes()==declined,"question cannot resolve twice");
    // Facility 4 to level 3 with a 6 % discount, accepted with Yes, in parallel on a game resumed
    // from the checkpoint.
    Game resumed(exe,declined);
    StadiumBuildDraft condition,condition_copy;
    require(game.begin_stadium_build(4,3,condition)==StadiumBuildResult::Prepared,"native condition offer prepared");
    require(resumed.begin_stadium_build(4,3,condition_copy)==StadiumBuildResult::Prepared&&resumed.checkpoint_bytes()==game.checkpoint_bytes(),"checkpoint continuation reaches same prepared question");
    require(condition.weeks==7&&condition.discount==6&&condition.nominal_cost==990000&&matches("pending-condition"),"all47761 native pending condition bytes and exact question values");
    require(decode_game_checkpoint(game.checkpoint_bytes()).borland_random==2918066739u,"independent native second prepared RNG");
    require(game.finish_stadium_build(condition,true)&&matches("accepted-condition"),"all47761 native accepted condition bytes");
    require(resumed.finish_stadium_build(condition_copy,true)&&resumed.checkpoint_bytes()==game.checkpoint_bytes(),"same pending construction continues after checkpoint");
    const auto accepted=game.checkpoint_bytes();const auto completed=decode_game_checkpoint(accepted);
    require(completed.borland_random==2618836624u&&completed.game_random==initial.game_random&&game.budget().balance==569400,"native fourth draw and 189800DM balance");
    require(game.stadium_refusals()[0][4]==0&&game.stadium_refusals()[0][8]==1,"Yes clears only selected refusal gate");
    require(!game.finish_stadium_build(condition,true)&&game.checkpoint_bytes()==accepted,"accepted question cannot book twice");
    Game continued(exe,accepted);game.advance_day();continued.advance_day();
    require(game.checkpoint_bytes()==continued.checkpoint_bytes(),"construction and daily refusal lifetime survive exact OEM continuation");
    // The UI session stages every action through an OEM checkpoint; loading a HOK into the running
    // session must keep the in-memory refusal gates even though the HOK cannot contain them.
    em::ui::Session session(data);session.load(native/"stadium-h2-entry.hok",2222449849u);
    StadiumBuildDraft draft;
    session.apply([&](Game& g){g.begin_stadium_build(8,-1,draft);return em::ui::ActionResult{};});
    session.apply([&](Game& g){g.finish_stadium_build(draft,false);return em::ui::ActionResult{};});
    require(session.game().stadium_refusals()[0][8]==1,"Session transaction retains refusal scratch");
    // Loading the same save again is refused (save history), so load a different save to
    // exercise the same live-load preservation path.
    session.load(native.parent_path().parent_path()/"EM"/"START.HOK");
    require(session.game().stadium_refusals()[0][8]==1,"live HOK load retains process-local refusal gate");
    std::puts("Native stadium Game/Session actions passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Stadium game actions: %s\n",e.what());return 1;}}
