// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The day loop: a running game built from a save and EM.EXE, advanced one day at a time like
// ovl14_2e0f does. It ties the rebuilt parts together; the order of one day is
//   1. season day + 1, date (calendar.hpp)
//   2. events of the day (DS:0828): league matchday of all three leagues (season.hpp; the human
//      club plays with its squad, its goals get scorers, a home game brings spectators and ticket
//      money - finances.hpp), DEB cup round on the first of its three days (competitions.hpp);
//      European cup day (6 group rounds from the save, then a final group of the two best of
//      each group, ovl05_1e6f / ovl05_0ef7), play-off day (best-of-five series day by day, five
//      days per round, ovl05_2404; relegation groups of four on the first and third day of each
//      round, ovl05_0ef7(1)); world championship day (ovl05_194b: 5 rounds in two groups of six
//      nations, then quarter-finals, semi-finals and the placement games for places 1-8)
//      Each day with chance 1 / (managers + 3) the transfer list is refreshed (ovl11_34ef).
//   3. finances of the day (finance_day: interest, contracts on the 1st, month-end booking)
//   4. Mondays: weekly recovery costs (DS:97EE), training and morale of the squad (training.hpp)
//   5. 15th and last day of the month: club image and sponsor offers (sponsors.hpp)
// After the last day of the calendar new_season() rolls over (season_end.hpp): promotion and
// relegation (relegation groups, Oberliga 11th/12th against regional clubs 36-41) swap team
// indices like ovl06_0c29, a new European cup draw, prizes, ageing, contract years, retirement of
// computer club players, cleared statistics and a new cup draw.
// Only manager 0 is played. Injuries: injuries.hpp (Mondays and Fridays in training, after the
// matches of the human club).
#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <vector>

#include "formats/db.hpp"
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "game/competitions.hpp"
#include "game/contracts.hpp"
#include "game/finances.hpp"
#include "game/injuries.hpp"
#include "game/match.hpp"
#include "game/schedule.hpp"
#include "game/scorers.hpp"
#include "game/season.hpp"
#include "game/season_end.hpp"
#include "game/sponsors.hpp"
#include "game/training.hpp"
#include "game/transfers.hpp"

namespace em {

// An offer of a computer club for a player of manager 0 (146e:0c89, see transfers.hpp).
struct ManagerOffer {
    int player = -1;      // index into HokFile::players
    bool listed = false;  // bit 7: the manager had put him on the transfer list; else bit 6
    int bidder = -1;      // team index (+0x16)
    long amount = 0;      // raw (DM x 3), ovl11_05f1
};

// A small round-robin table: European cup and relegation groups of four, world championship
// groups of six. Ranked by points, then goal difference, then goals scored.
template <size_t N>
struct RoundRobinTable {
    std::array<int, N> teams{};
    std::array<int, N> points{}, goals_for{}, goals_against{};
    void book(int home, int away, MatchResult r) {  // team indices; teams not in the group are ignored
        const size_t h = size_t(std::find(teams.begin(), teams.end(), home) - teams.begin());
        const size_t a = size_t(std::find(teams.begin(), teams.end(), away) - teams.begin());
        if (h >= N || a >= N) return;
        goals_for[h] += r.home;
        goals_against[h] += r.away;
        goals_for[a] += r.away;
        goals_against[a] += r.home;
        const int ph = r.home > r.away ? 2 : r.home == r.away ? 1 : 0;
        points[h] += ph;
        points[a] += 2 - ph;
    }
    std::array<int, N> order() const {  // group positions (indices into teams), best first
        std::array<int, N> o{};
        for (size_t i = 0; i < N; i++) o[i] = int(i);
        std::stable_sort(o.begin(), o.end(), [&](int x, int y) {
            const int dx = goals_for[size_t(x)] - goals_against[size_t(x)];
            const int dy = goals_for[size_t(y)] - goals_against[size_t(y)];
            if (points[size_t(x)] != points[size_t(y)]) return points[size_t(x)] > points[size_t(y)];
            if (dx != dy) return dx > dy;
            return goals_for[size_t(x)] > goals_for[size_t(y)];
        });
        return o;
    }
};
using GroupTable = RoundRobinTable<4>;

class Game {
public:
    Game(const Bytes& exe, const Bytes& save, uint32_t seed = 1);

    void advance_day();
    // The name database KB/23.vga; without it the season change keeps the player names.
    void set_database(const Database& db) { db_ = db; }
    // The game as a savegame (HOK): date and matchdays (the next day is the one to play), tables,
    // next pairings, cup draw, manager 0's money, attendance and image, squad and player records.
    // European cup and play-off progress, sponsor offers and contracts are not stored yet.
    Bytes save_bytes();

    int season_day() const { return day_; }
    GameDate date() const { return date_of_season_day(day_, save_.year); }
    const League& league(int l) const { return leagues_[size_t(l)]; }
    int next_matchday(int l) const { return matchday_[size_t(l)]; }
    const ManagerBudget& budget() const { return budget_; }
    int image() const { return image_; }
    int human_team() const { return human_team_; }
    int euro_champion() const { return euro_champion_; }
    int champion() const { return champion_; }
    int world_champion() const { return wm_champion_; }
    // World championship places 1-8 (team indices) after its last day, else empty.
    const std::vector<int>& world_ranking() const { return wm_ranking_; }
    const RoundRobinTable<6>& world_group(int g) const { return wm_groups_[size_t(g)]; }
    int seasons_completed() const { return seasons_; }
    // Relegation group g (0 = first/second division, 1 = second division/Oberliga) after its six
    // rounds: teams best first; the first two play in the higher division next season.
    const std::array<int, 4>& relegation_order(int g) const { return relegation_upper_[size_t(g)]; }
    const GroupTable& relegation_group(int g) const { return relegation_[size_t(g)]; }
    const HokFile& save() const { return save_; }
    // Open offers for the manager's players. Accepting sells the player to the bidder at once.
    const std::vector<ManagerOffer>& offers() const { return manager_offers_; }
    bool accept_offer(size_t i);
    // Contract talks at the season end (ovl02_0000 / ovl11_41db) are the manager's: for every
    // squad player whose contract ran out the decider gets the player and the salary he asks
    // (raw) and returns the years offered (0 = let him go). Without one the club offers 2 years.
    using ContractDecider = std::function<int(const ContractPlayer& player, long demand)>;
    void set_contract_decider(ContractDecider d) { contract_decider_ = std::move(d); }
    void decline_offer(size_t i);
    const std::vector<std::string>& log() const { return log_; }
    void clear_log() { log_.clear(); }

private:
    void play_league_day();
    void update_series(int team, int side, int goals_for, int goals_against);
    void record_head_to_head(int home, int away, int home_goals, int away_goals);
    void update_records(int home, int away, int home_goals, int away_goals);
    void book_special_teams(const MatchStats& stats, int side);
    void play_cup_day();
    void play_euro_day();
    void play_playoff_day();
    void play_wm_day();
    void reset_wm();
    void new_season();
    void redistribute_players(int mode);
    void refresh_transfer_list();
    void swap_teams(int a, int b);
    void home_game(int guest, int kind, int home_class, int guest_class, int home_rank, int guest_rank);
    void train_squad();
    void injure(size_t slot, int kind);
    void squad_injuries(bool weekly, bool no_match_today);
    void match_injuries();
    void make_offers();
    void expire_contracts();
    HokPlayer* player_by_exe_index(int exe_index);  // squad +0x0F = HOK player record + 1
    void remove_squad_record(int block, int slot, int slots);
    void refresh_human_team();
    void update_sponsors();
    int team_rank(int team) const;

    HokFile save_;
    ScheduleTables schedule_;
    SeasonCalendar calendar_;
    FinanceTables finance_;
    ContractTables contracts_;
    SponsorTables sponsor_tables_;
    TrainingTables training_;
    InjuryTables injuries_;
    std::array<int, 30> injured_role_{};  // role (+0x0A) of an injured player before he got hurt
    bool human_played_ = false;           // the human club played today
    std::vector<ManagerOffer> manager_offers_;
    ContractDecider contract_decider_;
    RatingBounds rating_;
    TrainingPlan plan_{};
    GameRandom rng_;
    BorlandRandom brng_;
    std::array<League, 3> leagues_{};
    std::array<int, 3> matchday_{};  // next league matchday, 1-based
    int day_ = 0;
    int cup_day_ = 0;                // cup days seen so far
    std::vector<HokPairing> cup_draw_;
    int human_team_ = -1;
    HumanSquad human_;
    ManagerBudget budget_;
    SponsorOffers offers_{};
    int image_ = 0;
    std::array<GroupTable, 2> euro_groups_{};
    GroupTable euro_final_{};
    int euro_round_ = 0, euro_champion_ = -1;
    std::vector<Series> series_;
    int playoff_day_ = 0, champion_ = -1, cup_winner_ = -1;
    int seasons_ = 0;
    std::array<RoundRobinTable<6>, 2> wm_groups_{};  // nations 52-57 and 58-63
    std::vector<int> wm_slots_;                       // knock-out slots DS:5D5B, pairs (0,1), (2,3), ...
    std::vector<int> wm_ranking_;
    int wm_round_ = 0, wm_champion_ = -1;             // DS:07AE
    std::array<GroupTable, 2> relegation_{};                 // [0] first/second, [1] second/Oberliga
    std::array<std::array<int, 4>, 2> relegation_upper_{};  // final group order: the first two play above
    std::vector<std::string> log_;
    Database db_;
};

}  // namespace em
