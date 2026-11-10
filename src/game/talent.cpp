// SPDX-License-Identifier: GPL-3.0-only
// Talent scout ("Talent" button of the transfer page) of EM.EXE.
//
// Start, ovr035:08CB..0A55 (manager counter +0xEC == 0, click on the button): question
// "M|chten Sie, da~ Ihr 'Spion' / f}r Sie auf Talentsuche geht? / Preis: <price> DM" with
// "Aber immer!" / "Lieber nicht.". On yes the price u32 DS:020D + 4 * manager (state 0x18) is
// paid when the cash is strictly above it, else "Sie haben nicht / genug Geld !". The counter
// becomes rand(3, 10), plus 6 when rand(0, 3) == 0.
//
// Evaluation, seg002:0AC5 (= 146e:0aC5), called by the club day loop ovr038:33FA on the
// twice-weekly days just before 162c:1c5b(1) and 146e:0c89: a running counter drops by one; when it
// reaches zero (DS:4860 is 0) the search ends with
//   rand(0, 5 - level / 2) == 0: seg002:089F(manager, rand(40, 90), 80) creates the player and
//     posts "Ihre Talentsuche war ein Erfolg. / Ein junger Spieler st|~t zu Ihrem Kader.";
//     a full squad (reserved count >= 28) creates nobody and posts nothing
//   otherwise "Ihr Talentsp{her gibt die / Suche ohne Erfolg auf... Pech..."
// Every message (ovr036:01CA) first draws its display date rand(0, 3).
//
// seg002:089F(manager, s, percent): a free player (global +0x1A == 6) among IDs rand(1, 179);
// +0x19 = 0, age rand(18, 21), form rand(45, 55), skills +0x16 and +0x15 = rand(s - 5, s + 1),
// club = the manager's; 59B5:2CD1(id, 0 years, 0, 0, 29) inserts him; owner = manager; squad
// +0x19 = rand(3, 12) << 4 | rand(3, 12); salary = worth(salary) * percent / 100; value =
// worth(value); contract rand(2, 3) years.
#include "game/game.hpp"
#include "game/native_roster.hpp"
#include "game/messages.hpp"

#include <algorithm>

namespace em {
namespace {
constexpr size_t kRecord = 52, kPlayerBase = 0x4FC6;
constexpr size_t kPrice = 0x18, kLevelByte = 0xAEB4;  // the price is one field so far (per manager unverified)
constexpr size_t counter_at(int manager) { return manager_block(manager) + 0xEC; }  // running steps, manager block +0xEC
void store32(Bytes& b, size_t at, uint32_t v) { for (int n = 0; n < 4; n++) b.at(at + size_t(n)) = uint8_t(v >> (8 * n)); }
}  // namespace

long Game::talent_search_price() const { return long(int32_t(le32(save_.state, kPrice))); }
int Game::talent_search_days() const { return save_.state.at(counter_at(active_manager_)); }

TalentResult Game::start_talent_search() {  // for the active manager (docs/MEHRSPIELER_PLAN.md step 5)
    const int a = active_manager_;
    if (national_coach_ >= 0 || mgr_[a].team < 0 || mgr_[a].team >= 52) return TalentResult::Unavailable;
    if (save_.state.at(counter_at(a)) != 0) return TalentResult::Running;
    const long price = talent_search_price();
    if (!(mgr_[a].budget.balance > price)) return TalentResult::InsufficientFunds;
    mgr_[a].budget.balance -= price;
    int days = brng_.range(3, 10);
    if (brng_.range(0, 3) == 0) days += 6;
    save_.state.at(counter_at(a)) = uint8_t(days);
    log_.push_back("talent search started for " + std::to_string(price / 3) + " DM, " + std::to_string(days) + " steps");
    return TalentResult::Started;
}

bool Game::talent_player(int skill, long percent, int m) {
    const size_t kSquadBase = squad_block(m);
    if (native_reserved_players(save_, m) >= 28) return false;
    HokFile prepared = decode_hok(save_bytes());
    Bytes& s = prepared.state;
    int id = 0;
    do id = brng_.range(1, 179); while (s.at(kPlayerBase + size_t(id - 1) * 33 + 0x1A) != 6);
    const size_t global = kPlayerBase + size_t(id - 1) * 33;
    s.at(global + 0x19) = 0;
    s.at(global + 0x14) = uint8_t(brng_.range(18, 21));
    s.at(global + 0x17) = uint8_t(brng_.range(45, 55));
    s.at(global + 0x16) = uint8_t(brng_.range(skill - 5, skill + 1));
    s.at(global + 0x15) = uint8_t(brng_.range(skill - 5, skill + 1));
    s.at(global + 0x1D) = s.at(manager_block(m) + 0x14);
    const ContractRandomDraw draw = [&](int lo, int hi) { return brng_.range(lo, hi); };
    const NativeRosterInsertion inserted = native_insert_player(prepared, contracts_, draw, uint8_t(id), m, 0);
    if (inserted.slot < 0 || inserted.slot >= 30) throw FormatError("talent search: native insertion failed");
    s.at(global + 0x1A) = uint8_t(m);
    const size_t row = kSquadBase + size_t(inserted.slot) * kRecord;
    const int high = brng_.range(3, 12);
    s.at(row + 0x19) = uint8_t(high << 4 | brng_.range(3, 12));
    const ContractPlayer contract = contract_player(prepared, m, inserted.slot);
    store32(s, row + 0x28, uint32_t(player_worth(contracts_, contract, Worth::Salary, draw) * percent / 100));
    store32(s, row + 0x2C, uint32_t(player_worth(contracts_, contract_player(prepared, m, inserted.slot), Worth::Value, draw)));
    s.at(row + 0x0B) = uint8_t(brng_.range(2, 3));
    std::copy_n(s.begin() + long(kSquadBase), 30 * kRecord, save_.state.begin() + long(kSquadBase));
    std::copy_n(s.begin() + long(global), 33, save_.state.begin() + long(global));
    HokPlayer* player = player_by_exe_index(id);
    player->age = s.at(global + 0x14);
    player->skill_a = s.at(global + 0x15);
    player->skill_b = s.at(global + 0x16);
    player->form = s.at(global + 0x17);
    player->team = mgr_[m].team;
    if (inserted.shifted)
        std::move_backward(mgr_[m].injured_role.begin() + inserted.slot, mgr_[m].injured_role.end() - 1, mgr_[m].injured_role.end());
    mgr_[m].injured_role[size_t(inserted.slot)] = 0;
    save_.squad_of(m) = hok_squad(save_, m);
    log_.push_back(player->name + " joins from the talent search");
    refresh_budget_roster(m);
    return true;
}

void Game::talent_search_week() {
    if (national_coach_ >= 0 || mgr_[0].team < 0 || mgr_[0].team >= 52) return;
    uint8_t& counter = save_.state.at(counter_at(0));  // the day loop runs the search of manager 0 (step 7: every manager)
    if (counter == 0) return;
    if (--counter != 0) return;  // DS:4860 (immediate evaluation) is never set in a normal game
    const int level = save_.state.at(kLevelByte);
    // ovr036:01CA (two lines, general entry of the current manager); its date draw is rand(0, 3)
    auto talent_message = [&](const char* first, const char* second) {
        const GameDate d = date_of_season_day(day_ + 1, save_.year);
        native_add_message(save_.state, brng_, {d.day, d.month, d.year, day_ + 1}, MessageKind::TwoLines,
                           {MessageArg::str(first), MessageArg::str(second)}, 0, true);
    };
    if (brng_.range(0, 5 - level / 2) == 0) {
        if (talent_player(brng_.range(40, 90), 80)) {
            talent_message("Ihre Talentsuche war ein Erfolg.", "Ein junger Spieler st|~t zu Ihrem Kader.");
            manager_news_.push_back("Ihre Talentsuche war ein Erfolg.\nEin junger Spieler stößt zu Ihrem Kader.");
        }
    } else {
        talent_message("Ihr Talentsp{her gibt die", "Suche ohne Erfolg auf... Pech...");
        manager_news_.push_back("Ihr Talentspäher gibt die\nSuche ohne Erfolg auf... Pech...");
    }
}

}  // namespace em
