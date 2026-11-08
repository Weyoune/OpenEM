// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The day loop: a running game built from a save and EM.EXE, advanced one day at a time like
// ovl14_2e0f does. It ties the rebuilt parts together; the order of one day is
//   1. season day + 1, date (calendar.hpp)
//   2. events of the day (DS:0828): league matchday of all three leagues (season.hpp; the human
//      club plays with its squad, its goals get scorers, a home game brings spectators and ticket
//      money - finances.hpp), DEB cup best-of-three series over three dates (competitions.hpp);
//      European cup day (6 group rounds from the save, then a final group of the two best of
//      each group, ovl05_1e6f / ovl05_0ef7), play-off day (best-of-five series day by day, five
//      days per round, ovl05_2404; relegation groups of four on the first and third day of each
//      round, ovl05_0ef7(1)); world championship day (ovl05_194b: 5 rounds in two groups of six
//      nations, then quarter-finals, semi-finals and the placement games for places 1-8)
//      Each day with chance 1 / (managers + 3) the transfer list is refreshed (ovl11_34ef).
//   3. finances of the next saveable date (finance_day: interest, contracts on the 1st,
//      month-end booking); a native menu save already includes finances of its displayed day
//   4. daily image, then recovery costs (DS:97EE), training and morale at season-day
//      remainder 0 or 4 modulo 7; WM dates use the separate club/healing phase
//   5. sponsor offers before finances on month_length/2 and the last day of the month
// After the last day of the calendar new_season() rolls over (season_end.hpp): promotion and
// relegation (relegation groups, Oberliga 11th/12th against regional clubs 36-41) swap team
// indices like ovl06_0c29, a new European cup draw, prizes, ageing, contract years, retirement of
// computer club players, cleared statistics, 132 summer finance/healing days and a new cup draw.
// Only manager 0 is played. Injuries: injuries.hpp (twice-weekly club preparation and after
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
#include "game/lineup.hpp"
#include "game/match.hpp"
#include "game/national_team.hpp"
#include "game/schedule.hpp"
#include "game/scorers.hpp"
#include "game/settings.hpp"
#include "game/season.hpp"
#include "game/season_end.hpp"
#include "game/sponsors.hpp"
#include "game/stadium.hpp"
#include "game/save_history.hpp"
#include "game/sale_quote.hpp"
#include "game/training.hpp"
#include "game/tactics.hpp"
#include "game/transfers.hpp"

namespace em {

struct HighscoreRecord;
struct HighscoreTables;

// An offer of a computer club for a player of manager 0 (146e:0c89, see transfers.hpp).
struct ManagerOffer {
    int player = -1;      // index into HokFile::players
    bool listed = false;  // bit 7: the manager had put him on the transfer list; else bit 6
    int bidder = -1;      // team index (+0x16)
    long amount = 0;      // legacy OEM quote field; new interests have no price yet
};

enum class PurchaseResult { Invalid, Unavailable, Blocked, SellerRefused, InsufficientFunds,
                            SquadFull, PlayerRefused, Purchased, Negotiating };
// Transient context for the original fee-editor -> contract-panel lifetime.
// The UI retains this while the temporary native roster row exists; file
// saving/day advancement is unavailable until finish or explicit cancel.
struct PurchaseDraft {
    int pool_slot=-1,player_id=0,club_slot=-1,old_team=-1;
    long fee=0,initial_salary=0;
};
//The native sponsor decides before its confirmation opens. The controller
//retains this prepared resolution and cannot save/advance while it is pending.
struct AdvertRenewalDraft {
    size_t slot=0;
    AdvertContract contract{};
    bool sponsor_stays=false;
};
struct StadiumBuildDraft {
    int item=-1,target=0,weeks=0,discount=0;
    int32_t nominal_cost=0;
};
enum class StadiumBuildResult { Invalid,Unavailable,Blocked,InsufficientFunds,Prepared };
enum class ContractResult { Invalid, Unavailable, Blocked, ShorteningNotConfirmed, PlayerRefused, Signed };
enum class TransferMoveResult { Invalid, Unavailable, Blocked, OwnLimit, PoolFull, SquadFull, Moved };
enum class TalentResult { Unavailable, Running, InsufficientFunds, Started };
enum class BankResult { InvalidAmount, Unavailable, InsufficientCash, CreditLimit, Transferred };
enum class StockResult { Invalid, Unavailable, InsufficientCash, InsufficientShares, Traded };

// A small round-robin table: European cup and relegation groups of four, world championship
// groups of six. Ranked by points, then goal difference, then goals scored.
template <size_t N>
struct RoundRobinTable {
    std::array<int, N> teams{};
    std::array<int, N> points{}, goals_for{}, goals_against{};
    std::array<int, N> ranking = [] { std::array<int, N> o{}; for (size_t i = 0; i < N; i++) o[i] = int(i); return o; }();
    bool ranking_dirty = false;
    void book(int home, int away, MatchResult r) {  // team indices; teams not in the group are ignored
        const size_t h = size_t(std::find(teams.begin(), teams.end(), home) - teams.begin());
        const size_t a = size_t(std::find(teams.begin(), teams.end(), away) - teams.begin());
        if (h >= N || a >= N) return;
        goals_for[h] = uint8_t(goals_for[h] + r.home);
        goals_against[h] = uint8_t(goals_against[h] + r.away);
        goals_for[a] = uint8_t(goals_for[a] + r.away);
        goals_against[a] = uint8_t(goals_against[a] + r.home);
        const int ph = r.home > r.away ? 2 : r.home == r.away ? 1 : 0;
        points[h] = uint8_t(points[h] + ph);
        points[a] = uint8_t(points[a] + 2 - ph);
        ranking_dirty = true;
    }
    std::array<int, N> order() const {  // group positions (indices into teams), best first
        auto o = ranking;
        if (!ranking_dirty) return o;
        // ovl05_1322: exchange sort from the previous saved order, including
        // swaps on complete equality. Goal difference is a signed BYTE.
        const auto key = [&](int i) {
            return std::array<int, 3>{points[size_t(i)],
                int(int8_t(uint8_t(goals_for[size_t(i)] - goals_against[size_t(i)]))), goals_for[size_t(i)]};
        };
        for (size_t i = 0; i + 1 < N; i++)
            for (size_t j = i + 1; j < N; j++) if (key(o[j]) >= key(o[i])) std::swap(o[i], o[j]);
        return o;
    }
    void finish_round() { ranking = order(); ranking_dirty = false; }
};
using GroupTable = RoundRobinTable<4>;

class Game {
public:
    // Native HOK uses seed; an OpenEM checkpoint restores its own RNG states.
    // previous is only the ongoing session of an explicit live native load;
    // it selects the native main-menu return instead of cold day-menu setup.
    Game(const Bytes& exe,const Bytes& save,uint32_t seed=1,const Game* previous=nullptr);

    void advance_day();
    // Random generator states (Borland rand seed DS:5984, GameRandom state DS:E017) for the whole-program differential test.
    uint32_t borland_state() const { return brng_.state(); }
    uint32_t game_rng_state() const { return rng_.state(); }
    void set_game_rng_state(uint32_t s) { rng_ = GameRandom(s); rng_.borland = &brng_; }  // differential tests
    // The name database KB/23.vga; without it the season change keeps the player names.
    void set_database(const Database& db) { db_ = db; }
    // The game as a savegame (HOK): date and matchdays (the next day is the one to play), tables,
    // next pairings, cup draw, manager 0's money, attendance and image, squad and player records.
    // Includes sponsor offers, advertising contracts, accrued interest, European cup and WM
    // tables/results, play-off series and relegation groups.
    Bytes save_bytes() const;
    // Actual native save action (7348:0CAB/12F0): refresh preview/header,
    // consume four Borland draws, and choose a new session nonce/cipher key.
    // Rendering and checkpoint creation use the pure save_bytes() above.
    Bytes native_save_bytes();
    // Native player-detail opening assesses once; flagged market records can
    // consume one Borland draw. Repainting must reuse the returned quote.
    long quote_player_value(int manager,int slot);
    // OpenEM envelope for deterministic continuation; never load this in DOSBox.
    // Rebind external database/contract callbacks after loading, as for native HOK.
    Bytes checkpoint_bytes() const;

    int season_day() const { return day_; }
    GameDate date() const { return date_of_season_day(day_, save_.year); }
    const League& league(int l) const { return leagues_[size_t(l)]; }
    int next_matchday(int l) const { return matchday_[size_t(l)]; }
    const ManagerBudget& budget() const { return budget_; }
    int image() const { return image_; }
    int game_level() const { return finance_.level; }  // 0 professional .. 5 beginner
    int human_team() const { return human_team_; }
    // Presentation-only event marker; includes club/national matches and forfeits.
    // It is cleared before a day and never affects RNG, finances or checkpoint state.
    bool interactive_match_played() const { return interactive_played_; }
    int euro_champion() const { return euro_champion_; }
    int champion() const { return champion_; }
    int cup_winner() const { return cup_winner_; }
    const std::vector<Series>& cup_series() const { return cup_series_; }
    const std::vector<Series>& playoff_series() const { return series_; }
    int world_champion() const { return wm_champion_; }
    int national_coach() const { return national_coach_; }  // manager 0 or -1
    int national_team() const { return national_coach_ >= 0 ? save_.state.at(0x068C) : -1; }
    std::vector<HokSquadPlayer> national_squad() const { return hok_squad(save_, 5); }
    bool select_national_player(size_t player, bool selected);
    bool auto_select_national_squad();
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
    bool begin_sale_offer(int block,int slot,int16_t native_si,SaleQuote&);
    bool finish_sale_offer(const SaleQuote&,bool sell);
    // Buy an ordinary CPU player from transfer-list slot 0..10. Fee and salary
    // are raw (DM * 3), years 1..4. Native refusals mark the saved manager ban;
    // funds/capacity failures keep ownership and roster intact. Optional sponsor
    // subsidies are declined unless explicitly accepted by this action.
    PurchaseResult buy_transfer_player(size_t slot, long fee, int years, long salary,
                                       bool accept_subsidy = false);
    PurchaseResult begin_transfer_purchase(size_t slot,long fee,PurchaseDraft& draft);
    PurchaseResult finish_transfer_purchase(const PurchaseDraft&,int years,long salary,bool accept_subsidy=false,bool prepare_lineup=true);
    bool cancel_transfer_purchase(const PurchaseDraft&);
    // Loan of a computer transfer-list player for one season (src/game/loan.cpp, ovr035:1118 loan flag).
    // The fee is raw; Purchased means the player joined on loan.
    PurchaseResult loan_transfer_player(size_t slot,long fee,bool accept_subsidy=false,bool prepare_lineup=true);
    // Talent scout (src/game/talent.cpp): price (raw, state 0x18), running counter, start.
    long talent_search_price() const;
    int talent_search_days() const;
    TalentResult start_talent_search();
    // Messages for the manager from the last day step (talent scout), oldest first. They are not part
    // of a checkpoint: the UI reads them right after advancing.
    const std::vector<std::string>& manager_news() const { return manager_news_; }
    // Third contract-dialog row: half a freshly computed salary per month.
    // New cover is locked through the next month-end; injured players cannot
    // change cover. Returns true if the requested state is already set or applied.
    bool set_player_insurance(size_t slot, bool enabled);
    // Explicit salary bid in the ordinary club contract dialog. Shortening an
    // existing deal requires the separate confirmation; refusals save a cooldown.
    ContractResult negotiate_contract(size_t slot, int years, long salary, bool allow_shortening = false);
    // Move an own contracted player to/from the market without an ownership
    // change or fee. At most three own players and eleven total pool records.
    TransferMoveResult list_transfer_player(size_t club_slot,bool prepare_lineup=true);
    TransferMoveResult recall_transfer_player(size_t market_slot,bool prepare_lineup=true);
    void finish_transfer_menu(); // native1BB5: auto lineup/strength refresh occurs on page exit
    void enter_strengths(); // explicit146C(manager,0), distinct from pure rendering
    void enter_club_info(int team);
    int set_team_reporting(int team,int mode); //0 off,1 intermediate results,2 goals
    // Einstellungen page (settings.hpp): switches, game speed. Pure state bytes, no random numbers.
    GameSettings settings() const;
    int toggle_setting(int widget,int manager);
    void set_game_speed(int speed);
    // Highscore page entry (highscore.hpp): stores a fresh image and values the squad, both drawing random numbers.
    HighscoreRecord highscore_record(int manager, const HighscoreTables& tables);
    // Positive raw amount: deposit/repay. Negative: withdraw/borrow. The bank
    // permits withdrawal of all savings, or 6,000,000 raw minus existing debt,
    // whichever is greater (the savings are not added to the credit allowance).
    // Each matrix side is posted independently; the bank's positive counter
    // claim determines outstanding debt even for an asymmetric imported matrix.
    BankResult bank_transfer(long amount);
    // Native bank screen7027:1EAC..20AD. Each direction pays1.5% commission;
    // partial-sale cost basis keeps the original two-stage percentage rounding.
    StockResult trade_shares(size_t company,uint32_t quantity,bool sell);
    ManualLineupResult change_lineup(size_t slot,int selected_line,ManualLineupAction action,const bool* six_override=nullptr,
                                   const std::array<std::array<int,6>,5>* cells_override=nullptr);
    bool set_tactics(const TacticsPlan& plan);
    const TrainingPlan& training_plan() const { return plan_; }
    // Commit a full club plan (kinds 0..8), including its native recovery fee.
    // Invalid plans/phases leave the game unchanged; no random draws or cash posting.
    bool set_training_plan(const TrainingPlan& plan);
    const SponsorOffers& sponsor_offers() const { return offers_; }
    //Original advertising-page entry recomputes/stores full monthly income.
    //It reads existing offers; there is no offer generation or random draw.
    bool enter_advertising();
    // Kind 0: seven boards, possibly several per offer. Kinds 1..4: slots 7..10.
    // A failed acceptance leaves the offers and contracts unchanged.
    bool accept_sponsor_offer(int kind, size_t offer);
    // Resolve an expired option for a whole board group or a single other contract.
    // Returns whether the extension was agreed; the sponsor decision precedes confirmation.
    bool renew_advert_contract(size_t slot, bool accept = true);
    bool begin_advert_renewal(size_t slot,AdvertRenewalDraft& draft);
    bool finish_advert_renewal(const AdvertRenewalDraft&,bool accept);
    const StadiumTables& stadium_tables() const { return stadium_tables_; }
    const std::array<std::array<uint8_t,11>,4>& stadium_refusals() const { return stadium_refusals_; }
    // Explicit live HOK load keeps ongoing process streams and construction
    // gates. OEM instead restores the streams/gates saved in its own envelope.
    void inherit_native_runtime(const Game& previous) {
        stadium_refusals_=previous.stadium_refusals_;
        brng_=previous.brng_;rng_=previous.rng_;
        line_counts_=previous.line_counts_;
        annual_distribution_cursors_=previous.annual_distribution_cursors_;
        for(int kind=0;kind<2;++kind)human_.per_line[kind]=line_counts_[0][size_t(kind)];
    }
    bool set_ticket_price(int price);
    // 5396:087B: immediately edit one native monthly youth grant; no RNG.
    bool adjust_youth_funding(int kind,int x);
    const NativeSaveHistory& native_save_history() const { return save_history_; }
    void set_native_save_history(const NativeSaveHistory& history) {
        if(history.count>50)throw FormatError("native save history: invalid counter");
        save_history_=history;
    }
    StadiumBuildResult begin_stadium_build(int item,int target,StadiumBuildDraft&);
    bool finish_stadium_build(const StadiumBuildDraft&,bool accept);
    // Contract talks at the season end (ovl02_0000 / ovl11_41db) are the manager's: for every
    // squad player whose contract ran out the decider gets the player and the salary he asks
    // (raw) and returns the years offered (0 = let him go). Without one the club offers 2 years.
    using ContractDecider = std::function<int(const ContractPlayer& player, long demand)>;
    void set_contract_decider(ContractDecider d) { contract_decider_ = std::move(d); }
    // Match display (ui/classic_match_view): observes the minute loops of league and cup days; never changes the game.
    void set_match_observer(MatchMinuteObserver o) { match_observer_ = std::move(o); }
    long match_attendance() const { return match_attendance_; }  // the last home game of the human club (ovl08_21e5)
    bool match_sold_out() const { return match_sold_out_; }
    void decline_offer(size_t i);
    const std::vector<std::string>& log() const { return log_; }
    void clear_log() { log_.clear(); }

private:
    void play_league_day();
    void manager_day(bool home_game_today);
    void update_series(int team, int side, int goals_for, int goals_against);
    void record_head_to_head(int home, int away, int home_goals, int away_goals);
    void update_records(int home, int away, int home_goals, int away_goals);
    void book_special_teams(const MatchStats& stats, int side, int manager = 0);
    void play_cup_day();
    bool human_in_next_cup() const;
    void play_euro_day();
    void play_playoff_day();
    void store_playoff_state(int stage, int completed_days);
    void play_wm_day();
    void begin_world_championship();
    void finish_world_championship();
    void refresh_national_team();
    void book_national_match(int own_goals, const MatchStats& stats, int side, int shootout_goals = 0);
    void book_match_condition(const MatchStats& stats, int side, int manager);
    bool book_forfeit(const MatchStats& stats, int manager = 0);
    void book_club_match(int own_goals, int goals_against, int competition, bool league_statistics, int shootout_goals = 0,
                         bool credit_goals = true);
    void credit_human_goal(int competition, bool penalty, bool goal = true, const HokTeam* on_ice = nullptr);
    void credit_opponent_goal(int competition, bool goal, const HokTeam& scorer);
    int last_scorer_slot_ = 0;  // DS:3277 (byte_33277): slot of the last scorer pick, reused when a club without a squad scores
    void reset_wm();
    void new_season();
    void redistribute_players(int mode);
    void refresh_transfer_list();
    TransferMoveResult move_owned_player(size_t slot, bool to_market,bool prepare_lineup);
    void swap_teams(int a, int b);
    void home_game(int guest, int kind, int home_class, int guest_class, int home_rank, int guest_rank,
                   bool league_statistics = false);
    void train_squad();
    void injure(size_t slot, int kind);
    void squad_injuries(bool weekly, bool no_match_today);
    void heal_squad(bool weekly);
    void refresh_budget_roster();
    void clear_insurance_locks();
    void prepare_human_lineup(bool restore_saved_mode=true);
    void match_injuries();
    void injury_minute(int load, const HokTeam* on_ice = nullptr);  // ovl06_364f for one minute of the human club's match
    void make_offers();
    void restore_offers();
    void expire_contracts();
    HokPlayer* player_by_exe_index(int exe_index);  // squad +0x0F = HOK player record + 1
    void remove_squad_record(int block, int slot, int end_slot);
    void refresh_human_team();
    void weekly_team_drift();  // 162c:1c5b(1) for all 64 national and club teams, twice a week (ovl14)
    void talent_search_week();  // seg002:0AC5, src/game/talent.cpp
    bool talent_player(int skill,long percent);  // seg002:089F
    void refresh_menu_manager(int manager);
    void update_sponsors();
    int current_club_image();
    void expire_advert_options();
    int team_rank(int team) const;

    HokFile save_;
    uint64_t exe_fingerprint_ = 0;
    ScheduleTables schedule_;
    SeasonCalendar calendar_;
    FinanceTables finance_;
    StadiumTables stadium_tables_;
    std::array<std::array<uint8_t,11>,4> stadium_refusals_{};
    NativeSaveHistory save_history_;
    std::array<uint8_t,3> annual_distribution_cursors_{};
    ContractTables contracts_;
    SponsorTables sponsor_tables_;
    TrainingTables training_;
    InjuryTables injuries_;
    NationalTeamTables national_tables_;
    std::array<int, 30> injured_role_{};  // role (+0x0A) of an injured player before he got hurt
    int daily_luck_ = 100;                // DS:E1A4: valuation factor 85..115 of the current play step
    bool injuries_in_minutes_ = false;    // the league match already ran the injury checks per minute
    bool human_played_ = false;          // the human club played today
    bool interactive_played_ = false;
    std::vector<ManagerOffer> manager_offers_;
    std::vector<std::string> manager_news_;
    ContractDecider contract_decider_;
    MatchMinuteObserver match_observer_;
    long match_attendance_ = 0;
    bool match_sold_out_ = false;
    RatingBounds rating_;
    TrainingPlan plan_{};
    GameRandom rng_;
    BorlandRandom brng_;
    std::array<League, 3> leagues_{};
    std::array<int, 3> matchday_{};  // next league matchday, 1-based
    int day_ = 0;
    int cup_day_ = 0;                // cup days seen so far
    std::vector<HokPairing> cup_draw_;
    std::vector<Series> cup_series_;  // higher/lower are the first-leg home/away teams here
    int cup_round_ = 1, cup_game_ = 0;
    int human_team_ = -1;
    HumanSquad human_;
    // Native BSS is zero; only a visited club/national refresh fills its pair.
    // Legacy OEMv1 retains its historical2/3 import policy in GameCheckpoint.
    std::array<std::array<uint8_t,2>,6> line_counts_{};
    LineRotation human_rotation_, national_rotation_;
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
    int national_coach_ = -1;
    std::array<GroupTable, 2> relegation_{};                 // [0] first/second, [1] second/Oberliga
    std::array<std::array<int, 4>, 2> relegation_upper_{};  // final group order: the first two play above
    std::vector<std::string> log_;
    Database db_;
};

}  // namespace em
