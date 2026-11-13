// SPDX-License-Identifier: GPL-3.0-only
// Tests for the UI session layer (src/ui/session.cpp) and the HOK export of Game. Without
// arguments only money() formatting is checked. With DATA REFERENCES WORK: the club identity
// comes from the state, not the header preview; loading a manual-lineup save; the exact HOK
// export of a fresh game; transactional actions that roll back on exceptions; refused loads;
// 35 days through the session compared with a direct Game; save protection (no overwriting,
// race-free creation, Unicode names); a refused purchase that still counts as an action; and a
// season review whose contract questions can be cancelled and asked again identically.
// Every run writes into its own fresh subfolder of WORK.
#include "ui/session.hpp"
#include "game/checkpoint.hpp"

#include <cstdio>
#include <chrono>
#include <atomic>
#include <thread>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;
static int failures = 0;  // number of failed CHECKs; nonzero makes main() fail
// Records a failure (file, line, expression) and continues.
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

int main(int argc, char** argv) {
    try {
        CHECK(money(1500000) == "500.000 DM");
        CHECK(money(-3000001) == "-1.000.000 DM");
        CHECK(money(0) == "0 DM");
        if (argc < 4) return failures ? 1 : 0;
        const fs::path data = argv[1], refs = argv[2], work = argv[3];
        fs::create_directories(work);
        // Every run has its own directory; tests never overwrite a reference or prior result.
        const auto unique = work / ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(unique);
        const Bytes exe = read_file(data / "EM.EXE");
        const Bytes start = read_file(refs / "START.HOK");
        {
            HokFile duplicate=decode_hok(start);
            const int own=duplicate.state.at(0x0DCD+0x14);
            // Header names are previews, not identity: team records of the original can share
            // names, and the club preview (header 0x25) is bounded at exactly 19 bytes.
            const size_t own_name=0x17E7+size_t(own)*61,other_name=0x17E7+149*61;
            std::copy_n(duplicate.state.begin()+own_name,22,duplicate.state.begin()+other_name);
            std::fill_n(duplicate.header.begin()+0x25,19,uint8_t('X'));
            duplicate.header[0x38]='Q';
            const Bytes misleading=encode_hok(duplicate);
            CHECK(decode_hok(misleading).club==std::string(19,'X'));
            Game identity(exe,misleading);
            CHECK(identity.human_team()==own);
        }
        if(fs::exists(refs/"M0.HOK")){
            const Bytes manual_source=read_file(refs/"M0.HOK");
            const auto raw=decode_hok(manual_source);
            CHECK(raw.state.at(0x078A)==0&&raw.state.at(0x078B)==1);
            Game loaded_manual(exe,manual_source);
            // When the original loads M0.HOK, the active lineup mode (0x078A) is restored from the
            // saved one (0x078B), so both read 1 afterwards.
            CHECK(loaded_manual.save().state.at(0x078A)==1&&loaded_manual.save().state.at(0x078B)==1);
        }
        // HOK export of a fresh game with seed 1, against independently computed constants:
        // save nonce (header 0xB4), filler 0xB8 and key0 0xB9, the random state after the
        // export's draws, year 1993 at 0x0A, manager count 1 at 0x0E, matchday 1 at 0x0F,
        // zeroed preview records 0x38..0xAF and the "EH0" marker at 0xB0. Repeated exports and a
        // reloaded checkpoint give the same bytes.
        {
            Game native(exe,start,1);
            const Bytes checkpoint=native.checkpoint_bytes();
            CHECK(native.save_bytes()==native.save_bytes()&&native.checkpoint_bytes()==checkpoint);
            const auto plain=decode_hok(native.save_bytes()).state;
            const Bytes exported=native.native_save_bytes();
            CHECK(int32_t(le32(exported,0xB4))==-28542&&exported[0xB9]==84&&exported[0xB8]==66);
            CHECK(decode_game_checkpoint(native.checkpoint_bytes()).borland_random==71484141);
            CHECK(decode_hok(exported).state==plain);
            CHECK(le32(exported,0x0A)==1993&&exported[0x0E]==1&&exported[0x0F]==1);
            CHECK(std::string(exported.begin()+0xB0,exported.begin()+0xB4)==std::string("EH0\0",4));
            for(size_t at=0x38;at<0xB0;at++)CHECK(exported[at]==0);
            Game resumed(exe,native.checkpoint_bytes());
            CHECK(resumed.native_save_bytes()==native.native_save_bytes());
            CHECK(resumed.checkpoint_bytes()==native.checkpoint_bytes());
        }
        Session session(data);
        session.load(refs / "START.HOK", 6);
        CHECK(!session.dirty() && session.date().day == 10 && session.date().month == 9);
        const Bytes initial = session.game().checkpoint_bytes();
        CHECK(session.next_day_has_match());
        CHECK(session.game().checkpoint_bytes() == initial && !session.dirty());
        bool rejected = false;
        try { session.apply([](Game& g) -> ActionResult { g.advance_day(); throw std::runtime_error("rollback"); }); }
        catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected && session.game().checkpoint_bytes() == initial && !session.dirty());
        {
            std::ofstream broken(unique / "broken.hok"); broken << "invalid";
        }
        rejected = false;
        try { session.load(unique / "broken.hok"); } catch (const std::exception&) { rejected = true; }
        CHECK(rejected && session.game().checkpoint_bytes() == initial);

        Game direct(exe, start, 6);
        NativeSaveHistory initial_history;initial_history.remember(le32(start,0xb4));
        direct.set_native_save_history(initial_history); // explicit user-load phase of Session
        direct.set_database(decode_db(read_file(data / "KB" / "23.vga")));
        for (int i = 0; i < 35; i++) { direct.advance_day(); direct.clear_log(); CHECK(session.advance_day()); }
        CHECK(session.dirty() && session.game().checkpoint_bytes() == direct.checkpoint_bytes());
        session.save(unique / "native.hok");
        CHECK(session.dirty() && read_file(unique / "native.hok") == direct.native_save_bytes());
        const auto after_export = session.game().checkpoint_bytes();
        rejected = false;
        try { session.save(unique / "native.hok"); } catch (const std::exception&) { rejected = true; }
        CHECK(rejected && session.game().checkpoint_bytes() == after_export);
        session.save(unique / "career.oem");
        CHECK(!session.dirty() && read_file(unique / "career.oem") == direct.checkpoint_bytes());
        const Bytes protected_file = read_file(unique / "career.oem");
        CHECK(session.advance_day());
        rejected = false;
        try { session.save(unique / "career.oem"); } catch (const std::exception&) { rejected = true; }
        CHECK(rejected && session.dirty() && read_file(unique / "career.oem") == protected_file);
        const auto unicode=unique/path_from_text("Saison_ÄÖÜ_ß.oem");
        session.save(unicode);
        CHECK(read_file(unicode)==session.game().checkpoint_bytes());
        CHECK(path_text(unicode.filename())=="Saison_ÄÖÜ_ß.oem");
        std::atomic<int> published{0},lost_race{0};
        const auto race=unique/"race.bin";
        auto publish=[&](uint8_t value){try{write_new_save(race,Bytes(1000,value));published++;}catch(const std::exception&){lost_race++;}};
        std::thread first(publish,uint8_t(17)),second(publish,uint8_t(23));first.join();second.join();
        const Bytes winner=read_file(race);
        CHECK(published==1&&lost_race==1);
        CHECK(winner==Bytes(1000,17)||winner==Bytes(1000,23));
        Session restored(data); restored.load(unique / "career.oem", 999);
        direct.advance_day(); direct.clear_log();
        CHECK(restored.advance_day());
        CHECK(restored.game().checkpoint_bytes() == direct.checkpoint_bytes());
        CHECK(session.game().checkpoint_bytes() == direct.checkpoint_bytes());

        // A refusal is a valid game action: retain its cooldown and random-number consumption, as
        // in the original.
        Session refusal(data); refusal.load(refs / "KAUFPR01.HOK", 6);
        const Bytes before_refusal = refusal.game().checkpoint_bytes();
        Game expected(exe, before_refusal);
        const auto expected_result = expected.buy_transfer_player(6, 3, 1, 15000);
        refusal.apply([](Game& g) { const auto r = g.buy_transfer_player(6, 3, 1, 15000); return ActionResult{"Verhandlung", r == PurchaseResult::Purchased}; });
        CHECK(expected_result != PurchaseResult::Purchased && refusal.dirty());
        CHECK(refusal.game().checkpoint_bytes() == expected.checkpoint_bytes());

        // Season review can ask several questions without committing partial finance/RNG/roster changes.
        HokFile review_fixture = decode_hok(read_file(refs / "LICRICH1.HOK"));
        int forced_expiry = 0;
        for (size_t slot = 0; slot < review_fixture.squad.size() && forced_expiry < 2; slot++) {
            auto& s = review_fixture.squad[slot];
            const size_t at = 0x696D + slot * 52;
            if (s.player < 0 || review_fixture.state[at + 0x0D] == 255) continue;
            s.contract_years = 1; review_fixture.state[at + 0x0B] = 1; forced_expiry++;
        }
        const Bytes review_source = encode_hok(review_fixture);
        write_new_save(unique / "review.hok", review_source);
        Session review(data); review.load(unique / "review.hok", 6);
        Game reference(exe, review_source, 6);
        NativeSaveHistory review_history;review_history.remember(le32(review_source,0xb4));
        reference.set_native_save_history(review_history);
        reference.set_database(decode_db(read_file(data / "KB" / "23.vga")));
        reference.set_contract_decider([](const ContractPlayer& p, long) { CHECK(p.player >= 0); return 1; });
        int decisions = 0;
        while (!review.game().seasons_completed()) {
            const Bytes before_day = review.game().checkpoint_bytes();
            bool done = review.advance_day();
            if (!done) {
                CHECK(review.contract_question().has_value());
                CHECK(review.game().checkpoint_bytes() == before_day);
                // Cancelling then restarting must ask the same player at the same price.
                const auto question = *review.contract_question();
                review.cancel_advance();
                CHECK(review.game().checkpoint_bytes() == before_day && !review.contract_question());
                CHECK(!review.advance_day());
                CHECK(review.contract_question()->player == question.player && review.contract_question()->monthly_salary == question.monthly_salary);
            }
            while (!done) {
                CHECK(review.contract_question()->player >= 0 && !review.contract_question()->name.empty());
                CHECK(review.contract_question()->monthly_salary > 0);
                CHECK(review.game().checkpoint_bytes() == before_day);
                done = review.answer_contract(true);
                CHECK(++decisions <= 29);
            }
            reference.advance_day(); reference.clear_log();
            CHECK(review.game().checkpoint_bytes() == reference.checkpoint_bytes());
        }
        CHECK(forced_expiry == 2 && decisions >= 2);
        std::printf("UI session: atomic saves, rollback, valid refusal, 35 daily steps, season review with %d decisions\n", decisions);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        return 1;
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
