// SPDX-License-Identifier: GPL-3.0-only
// Careers with two and four human managers (hot seat, docs/MEHRSPIELER_PLAN.md): two complete seasons with their season
// changes. The session reloads every day from an OpenEM checkpoint (v10); a second game runs the same career without
// reloading, so every day of both must give the same checkpoint. Checked per manager: season review page, history row,
// eternal balance, cup participation, contract talks in manager order, training plan and share holdings.
#include <cstdio>
#include <iostream>
#include <set>
#include <stdexcept>

#include "game/checkpoint.hpp"
#include "game/manager_layout.hpp"
#include "ui/session.hpp"

namespace fs = std::filesystem;
using namespace em;
using namespace em::ui;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        failures++;
    }
}
void require(bool ok, const std::string& what) {
    if (!ok) throw std::runtime_error(what);
}

// The answer of every contract talk: deterministic from the player (some leave, some sign for one or two years).
Game::ContractTerms policy(int player) {
    Game::ContractTerms t;
    t.years = player % 7 == 3 ? 0 : player % 4 == 1 ? 2 : 1;
    return t;
}

uint32_t games_played(const Bytes& state, int m) {  // eternal balance +0x13B: u16 home/away games at pair 4
    const size_t at = manager_block(m) + 0x13B + 4 * 4;
    return le16(state, at) + le16(state, at + 2);
}

void career(const fs::path& data, const std::vector<int>& clubs, uint32_t seed) {
    const std::string tag = std::to_string(clubs.size()) + " managers: ";
    Session s(data);
    const HokFile base = decode_hok(read_file(*find_new_game_base(data)));
    std::vector<NewGameSetup> setup;
    const char* names[4] = {"anna", "bert", "carl", "dora"};
    for (size_t m = 0; m < clubs.size(); m++) setup.push_back({names[m], int(m * 3 % 12), clubs[m]});
    s.start_new_game(setup, seed);
    const int n = s.game().managers();
    require(n == int(clubs.size()), tag + "manager count");
    for (const auto& line : s.game().log())
        check(line.find("simulates manager 0 only") == std::string::npos, tag + "no single-manager warning");

    // per-manager fields of the menu (step 5): training plan 0x070C + 0x1C * m, holdings 0x30 + 0x20 * m
    if (n > 1) {
        const TrainingPlan plan0 = s.game().training_plan();
        require(s.select_manager(n - 1), tag + "select the last manager");
        TrainingPlan other = plan0;
        for (auto& day : other) for (auto& unit : day) unit = uint8_t((unit + 1) % kTrainingKinds);
        s.apply([&](Game& g) { require(g.set_training_plan(other), "training plan"); return ActionResult{}; });
        s.apply([&](Game& g) { require(g.trade_shares(1, 10, false) == StockResult::Traded, "buy shares"); return ActionResult{}; });
        const Bytes& st = s.game().save().state;
        check(le32(st, 0x30 + 0x20 * size_t(n - 1) + 8 + 4) == 10, tag + "holding of the last manager");
        check(le32(st, 0x30 + 8 + 4) == 0, tag + "manager 0 holds no shares");
        check(st.at(0x070C + 0x1C * size_t(n - 1)) == other[0][0], tag + "plan of the last manager in its own block");
        require(s.select_manager(0), tag + "back to manager 0");
        check(s.game().training_plan() == plan0, tag + "manager 0 keeps his plan");
        s.select_manager(n - 1);
        check(s.game().training_plan() == other, tag + "the last manager's plan after a checkpoint reload");
        s.select_manager(0);
    }

    // the same career without reloading
    Bytes exe = read_file(data / "EM.EXE");
    Game continuous(exe, s.game().checkpoint_bytes());
    continuous.set_database(decode_db(read_file(data / "KB" / "23.vga")));
    continuous.set_contract_terms_decider([](const ContractPlayer& p, int, long) { return policy(p.player); });

    std::array<uint32_t, kMaxManagers> games_before{};
    for (int m = 0; m < n; m++) games_before[size_t(m)] = games_played(s.game().save().state, m);
    int seasons = 0, days = 0, mismatches = 0, after = 0;
    while (seasons < 2 || after++ < 40) {  // two season changes and 40 days of the third season
        std::vector<int> talk_managers;
        int guard = 0;
        const auto advance = [&] {
            try {
                return s.advance_day();
            } catch (const std::exception& e) {
                throw std::runtime_error(tag + "session, day " + std::to_string(days + 1) + ": " + e.what());
            }
        };
        while (!advance()) {
            require(++guard < 200, tag + "contract talks end");
            const auto& q = *s.contract_question();
            if (!talk_managers.empty()) check(q.manager >= talk_managers.back(), tag + "talks in manager order");
            talk_managers.push_back(q.manager);
            check(q.manager >= 0 && q.manager < n && q.slot >= 0 && size_t(q.slot) < decode_hok(q.save).squad_of(q.manager).size(),
                  tag + "talk for a squad player of the asked manager");
            int with_review = 0;
            for (int m = 0; m < n; m++) with_review += q.reviews[size_t(m)].has_value();
            check(with_review == n, tag + "every manager's review before the talks");
            require(s.answer_contract(policy(q.player)) || s.contract_question(), tag + "answer");
            if (!s.contract_question()) break;
        }
        try {
            continuous.advance_day();
        } catch (const std::exception& e) {
            throw std::runtime_error(tag + "continuous game, day " + std::to_string(days + 1) + ": " + e.what());
        }
        days++;
        if (s.game().checkpoint_bytes() != continuous.checkpoint_bytes() && mismatches++ < 3)
            check(false, tag + "reloaded and continuous career differ on day " + std::to_string(days) + " (season day " +
                             std::to_string(s.game().season_day()) + ")");
        if (s.game().seasons_completed() == seasons) continue;
        seasons = s.game().seasons_completed();
        const Game& g = s.game();
        const Bytes& st = g.save().state;
        std::set<int> asked(talk_managers.begin(), talk_managers.end());
        std::cerr << tag << "season change " << seasons << ": " << talk_managers.size() << " talks, managers";
        for (int m : asked) std::cerr << " " << m;
        std::cerr << "\n";
        const int year = std::min<int>(le16(st, 0xB951) - 1, 49);  // the row of the season just ended
        std::set<int> cup;
        for (const auto& p : g.save().cup_draw) { cup.insert(p.home); cup.insert(p.away); }
        for (int m = 0; m < n; m++) {
            const std::string who = tag + "manager " + std::to_string(m) + " season " + std::to_string(seasons) + ": ";
            check(g.season_review(m).has_value(), who + "season review page");
            const int team = st.at(manager_block(m) + 0x14);
            const size_t row = manager_block(m) + 0x18 + 4 * size_t(year);
            check(st.at(row) >= 1 && st.at(row) <= 53, who + "history row written");
            if (g.season_review(m)) check(g.season_review(m)->place == st.at(row), who + "review shows his history row");
            const uint32_t games = games_played(st, m);
            check(games >= games_before[size_t(m)] + 44 || team >= 36, who + "eternal balance counts the league season");
            games_before[size_t(m)] = games;
            check(st.at(manager_block(m) + 0xE6) == 1, who + "first cup round");
            check(cup.count(team) == 1, who + "club in the cup draw");
            check(g.save().squad_of(m).size() >= 9, who + "squad kept (" + std::to_string(g.save().squad_of(m).size()) + " players)");
        }
        for (int m = n; m < 6; m++) check(st.at(manager_block(m) + 0xE6) == 30, tag + "no cup round for unused block");
    }
    check(mismatches == 0, tag + std::to_string(mismatches) + " days differ between reloaded and continuous career");
    std::cerr << tag << days << " days played\n";
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("multimanager_tests DATA");
        const fs::path data = path_from_text(argv[1]);
        if (!find_new_game_base(data)) {
            std::cerr << "no fresh start state (BACKUP.HOK) in " << argv[1] << ": skipped\n";
            return 0;
        }
        career(data, {4}, 7);                // one manager (control: the same checks hold for the classic game)
        career(data, {4, 13}, 7);            // two managers: first and second division
        career(data, {2, 7, 15, 27}, 11);    // four managers in all three leagues
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    if (failures) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "multimanager tests passed\n";
    return 0;
}
