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
// The day loop and the season change run every human manager 0..N-1 in the order of the original's manager loops
// (docs/MEHRSPIELER_PLAN.md steps 7 and 8); the WM branch books the manager DS:A70E holds (146e:0c89 returns at once
// for DS:A70E >= N). Injuries: injuries.hpp (twice-weekly club
// preparation and after matches of the human club).
#include <algorithm>
#include <array>
#include <functional>
#include <optional>
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
#include "game/manager_layout.hpp"
#include "game/match.hpp"
#include "game/national_team.hpp"
#include "game/schedule.hpp"
#include "game/scorers.hpp"
#include "game/settings.hpp"
#include "game/season.hpp"
#include "game/season_end.hpp"
#include "game/season_review.hpp"
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
                            SquadFull, PlayerRefused, Purchased, Negotiating,
                            Unfair, OwnerQuestion };  // the last two: a list player of another human manager
// Transient context for the original fee-editor -> contract-panel lifetime.
// The UI retains this while the temporary native roster row exists; file
// saving/day advancement is unavailable until finish or explicit cancel.
struct PurchaseDraft {
    int pool_slot=-1,player_id=0,club_slot=-1,old_team=-1;
    long fee=0,initial_salary=0;
    int manager=0;  // the buying manager (the active one when the purchase began)
    int seller=-1;  // the human manager who listed the player (ovr035:1528), -1 = the computer market
    bool loan=false;
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
    // The three cursors of the annual distribution live only in the original's process (not in a HOK); the REAL-CODE
    // harness sets the values its recordings start with.
    void set_distribution_cursors(const std::array<uint8_t, 3>& c) { annual_distribution_cursors_ = c; }
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
    // The accessors and UI actions below act for the active manager (hot seat, docs/MEHRSPIELER_PLAN.md step 5);
    // it is manager 0 unless select_manager() chose another one of the managers() human managers.
    const ManagerBudget& budget() const { return mgr_[size_t(active_manager_)].budget; }
    int image() const { return mgr_[size_t(active_manager_)].image; }
    int game_level() const { return finance_.level; }  // 0 professional .. 5 beginner
    int human_team() const { return mgr_[size_t(active_manager_)].team; }
    int managers() const { return managers_; }
    int active_manager() const { return active_manager_; }
    bool select_manager(int m) {  // false (unchanged) outside 0..managers()-1
        if (m < 0 || m >= managers_) return false;
        active_manager_ = m;
        return true;
    }
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
    const std::vector<ManagerOffer>& offers() const { return mgr_[size_t(active_manager_)].transfer_offers; }
    bool accept_offer(size_t i);
    // block 0 = the active manager's club, 4 = the transfer list.
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
    // A bid for a list player of another human manager (purchase.cpp, ovr035:1528..17A5): Blocked, InsufficientFunds,
    // Unfair (outside 60..140 % of the value, no refusal bit) or OwnerQuestion with `offer` for the owner's answer.
    // answer_manager_offer: refusal -> SellerRefused and the bidder's bit; acceptance -> Negotiating (purchase, `draft`
    // for the contract panel) or Purchased (loan). The owner gets the fee, there is no sponsor subsidy.
    PurchaseResult offer_to_manager(size_t slot,long fee,bool loan,PurchaseDraft& offer);
    PurchaseResult answer_manager_offer(const PurchaseDraft& offer,bool accept,PurchaseDraft& draft,bool prepare_lineup=true);
    // Sponsor offer (seg001:223E) of the last purchase/loan, raw, 0 = none; not in a checkpoint. The UI asks the
    // question afterwards and books an accepted offer with accept_purchase_subsidy (no further draws).
    long last_purchase_subsidy() const { return last_purchase_subsidy_; }
    bool accept_purchase_subsidy(long raw);
    // Talent scout (src/game/talent.cpp): price (raw, state 0x18), running counter, start.
    long talent_search_price() const;
    int talent_search_days() const;
    TalentResult start_talent_search();
    void clear_message_flag(int manager);  // DS:5A50 + manager, src/game/messages.cpp
    // Messages for the manager from the last day step (talent scout), oldest first. They are not part
    // of a checkpoint: the UI reads them right after advancing.
    // Without an argument: those of the active manager.
    const std::vector<std::string>& manager_news() const { return manager_news_[size_t(active_manager_)]; }
    const std::vector<std::string>& manager_news(int m) const { return manager_news_.at(size_t(m)); }
    // Messages the original shows at once and deletes again (season review ovr026:09EB), native text, oldest first.
    const std::vector<std::string>& immediate_messages() const { return immediate_messages_[size_t(active_manager_)]; }
    const std::vector<std::string>& immediate_messages(int m) const { return immediate_messages_.at(size_t(m)); }
    // The manager's season-review page (ovl02_0000) when the last step changed the season, not in a checkpoint.
    // ovl02_0000 shows the page of every manager 0..N-1 in index order before the contract talks.
    const std::optional<SeasonReview>& season_review() const { return season_reviews_[size_t(active_manager_)]; }
    const std::optional<SeasonReview>& season_review(int m) const { return season_reviews_.at(size_t(m)); }
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
    const TrainingPlan& training_plan() const { return mgr_[size_t(active_manager_)].plan; }  // state 0x070C + 0x1C * m
    // Commit a full club plan (kinds 0..8), including its native recovery fee.
    // Invalid plans/phases leave the game unchanged; no random draws or cash posting.
    bool set_training_plan(const TrainingPlan& plan);
    const SponsorOffers& sponsor_offers() const { return mgr_[size_t(active_manager_)].offers; }
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
        for(size_t m=0;m<mgr_.size();++m)for(int kind=0;kind<2;++kind)mgr_[m].squad.per_line[kind]=line_counts_[m][size_t(kind)];
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
    // The same talks as the native dialog "Vertragsverhandlung" (ovl11_41db) offers them: the manager answers with
    // years (0 = "Abbruch", the player leaves), a salary (raw; 0 = what the player asks for these years) and the
    // insurance. Used instead of the decider above when set. `slot` is the player's squad slot at that moment.
    struct ContractTerms { int years = 1; long salary = 0; bool insured = false; };
    using ContractTermsDecider = std::function<ContractTerms(const ContractPlayer& player, int slot, long proposal)>;
    void set_contract_terms_decider(ContractTermsDecider d) { contract_terms_decider_ = std::move(d); }
    int daily_luck() const { return daily_luck_; }  // DS:E1A4, the mood of the dialog (renewal_demand)
    // The manager whose contract talk the deciders above are asked for (the talks run for 0..N-1 in index order).
    int contract_manager() const { return contract_manager_; }
    // Outcome of each contract talk of the last season change, in order over all managers (notices of the dialog).
    struct ContractOutcome { std::string name; bool signed_on = false, refused = false; long fee = 0; bool too_long = false; int manager = 0; };
    const std::vector<ContractOutcome>& contract_outcomes() const { return contract_outcomes_; }
    // Match display (ui/classic_match_view): observes the minute loops of league and cup days; never changes the game.
    void set_match_observer(MatchMinuteObserver o) { match_observer_ = std::move(o); }
    long match_attendance() const { return match_attendance_; }  // the last home game of the human club (ovl08_21e5)
    bool match_sold_out() const { return match_sold_out_; }
    void decline_offer(size_t i);
    const std::vector<std::string>& log() const { return log_; }
    void clear_log() { log_.clear(); }

private:
    void play_league_day();
    // Per-manager steps take the manager index m (0..3) last; every caller still passes 0 (docs/MEHRSPIELER_PLAN.md step 4).
    void manager_day(bool home_game_today, int m = 0);  // the squad bookkeeping of sub_60D60 for one manager
    void spin_match_stream();                           // sub_33CE9, once per match day with a manager game
    void update_series(int team, int side, int goals_for, int goals_against);
    void record_head_to_head(int home, int away, int home_goals, int away_goals, int m = 0);
    void update_records(int home, int away, int home_goals, int away_goals);
    void book_special_teams(const MatchStats& stats, int side, int manager = 0);
    void play_cup_day();
    void close_cup_final();  // sub_42756 at game index 2 of the final
    bool human_in_next_cup() const;
    bool human_in_next_playoff() const;
    void quick_setup(int home, int away, HokTeam& h, HokTeam& a);
    bool human_in_next_euro() const;
    void play_euro_day();
    void play_playoff_day(bool setup_only = false);  // setup_only: the first round set-up of the phase, run at the step end before it
    void store_playoff_state(int stage, int completed_days);
    void play_wm_day();
    void begin_world_championship();
    void finish_world_championship();
    void refresh_national_team();
    void book_national_match(int own_goals, const MatchStats& stats, int side, int shootout_goals = 0);
    void book_match_condition(const MatchStats& stats, int side, int manager);
    bool book_forfeit(const MatchStats& stats, int manager = 0);
    void book_club_match(int own_goals, int goals_against, int competition, bool league_statistics, int shootout_goals = 0,
                         bool credit_goals = true, int m = 0);
    void credit_human_goal(int competition, bool penalty, bool goal = true, const HokTeam* on_ice = nullptr, const std::vector<HokSquadPlayer>* working = nullptr, int m = 0);
    void credit_opponent_goal(int competition, bool goal, const HokTeam& scorer);
    bool message_step_pending_ = true;  // a loaded game stands in the menu: the next play step sorts and counts the messages
    bool table_copies_frozen_ = false;   // a play step passed a league day without returning to the menu: the copies of the table stay (HokFile::keep_table_copies)
    bool league_since_refresh_ = false;  // a league day was played since the copies were last refreshed
    int last_scorer_slot_ = 0;  // DS:3277 (byte_33277): slot of the last scorer pick, reused when a club without a squad scores
    void reset_wm();
    void new_season();
    void redistribute_players(int mode);
    void refresh_transfer_list();
    TransferMoveResult move_owned_player(size_t slot, bool to_market,bool prepare_lineup);
    void swap_teams(int a, int b);
    void home_game(int guest, int kind, int home_class, int guest_class, int home_rank, int guest_rank,
                   bool league_statistics = false, int manager = 0);
    void train_squad(int m = 0);
    void manager_stadium_week(int m);
    void manager_finance_day(int m, const GameDate& finance_date, int finance_month_days, bool world_phase);
    void menu_pass(bool menu);  // transfer-list roll and (with a menu) the daily factor of every manager
    void injure(size_t slot, int kind, int m = 0);
    void squad_injuries(bool weekly, bool no_match_today, int m = 0);
    void heal_squad(bool weekly, int m = 0);
    void reload_squads();  // the decoded squads of managers 0..N-1 from their native rows
    void refresh_budget_roster(int m = 0);
    void clear_insurance_locks(int m = 0);
    void prepare_human_lineup(bool restore_saved_mode = true, int m = 0);
    void match_injuries(int m = 0);
    void injury_minute(int load, const HokTeam* on_ice = nullptr, int m = 0);  // ovl06_364f for one minute of the human club's match
    void apply_phantom_pairs(int role);  // the pair-table write of a phantom release outside a league day (role < 0: none)
    void goalie_minute(int conceded, int m);  // 162c:068d / 162c:2337 at the end of a league match minute
    void make_offers(int m = 0);
    void restore_offers(int m = 0);
    void expire_contracts(int m = 0);
    HokPlayer* player_by_exe_index(int exe_index);  // squad +0x0F = HOK player record + 1
    void remove_squad_record(int block, int slot, int end_slot);
    void refresh_human_team(int m = 0);
    void weekly_team_drift();  // 162c:1c5b(1) for all 64 national and club teams, twice a week (ovl14)
    void talent_search_week();  // seg002:0AC5, src/game/talent.cpp: every manager in index order
    void talent_search_step(int m);
    bool talent_player(int skill,long percent,int m = 0);  // seg002:089F
    void refresh_menu_manager(int manager);
    void update_sponsors(int m = 0);
    int current_club_image(int m = 0);
    void expire_advert_options(int m = 0);
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
    int daily_luck_ = 100;                // DS:E1A4: valuation factor 85..115 of the current play step
    bool injuries_in_minutes_ = false;    // the league match already ran the injury checks per minute
    bool interactive_played_ = false;
    bool empty_cup_day_ = false;  // a cup day on which no game was played (every series decided)
    std::array<bool, kMaxManagers> idle_cup_day_{};  // a cup day with games, none of them this manager club's
    bool skip_refresh_once_ = false;  // the next prepare_human_lineup leaves the club record alone
    std::array<std::vector<std::string>, kMaxManagers> manager_news_;
    long last_purchase_subsidy_ = 0;
    PurchaseResult insert_purchase(size_t slot,long fee,PurchaseDraft& draft);  // purchase.cpp: the squad row of a bought list player
    PurchaseResult insert_loan(size_t slot,long fee,int seller,bool accept_subsidy,bool prepare_lineup);  // loan.cpp
    std::array<std::vector<std::string>, kMaxManagers> immediate_messages_;
    std::array<std::optional<SeasonReview>, kMaxManagers> season_reviews_;
    int contract_manager_ = 0;
    ContractDecider contract_decider_;
    ContractTermsDecider contract_terms_decider_;
    std::vector<ContractOutcome> contract_outcomes_;
    MatchMinuteObserver match_observer_;
    long match_attendance_ = 0;
    bool match_sold_out_ = false;
    RatingBounds rating_;
    GameRandom rng_;
    BorlandRandom brng_;
    std::array<League, 3> leagues_{};
    std::array<int, 3> matchday_{};  // next league matchday, 1-based
    int day_ = 0;
    int cup_day_ = 0;                // cup days seen so far
    std::vector<HokPairing> cup_draw_;
    std::vector<Series> cup_series_;  // higher/lower are the first-leg home/away teams here
    int cup_round_ = 1, cup_game_ = 0;
    // The human managers (docs/MEHRSPIELER_PLAN.md); the day loop simulates 0..managers_-1 (step 7).
    struct ManagerState {
        int team = -1;                // manager block +0x14
        HumanSquad squad;             // the squad and its controls in matches
        LineRotation rotation;
        PenaltyTimers timers;         // DS:D096 + 6 * manager (not saved; zero after loading)
        ManagerBudget budget;
        SponsorOffers offers{};
        TrainingPlan plan{};          // state 0x070C + 0x1C * manager (DS:A8A1, seg002:015E)
        int image = 0;
        bool played = false;          // the club played today
        std::vector<ManagerOffer> transfer_offers;  // open interests of computer clubs in his players
        std::array<int, 30> injured_role{};    // role (+0x0A) of an injured player before he got hurt
        std::array<bool, 30> hurt_in_match{};  // injured during the running match: still counted as having played (REAL-CODE period 28)
        bool goalie_tracked = false;  // the running league match booked goalie minutes and goals against minute by minute
    };
    std::array<ManagerState, 4> mgr_{};
    int active_manager_ = 0;  // the manager the UI actions act for (step 5); the day loop plays all managers
    // Match layer (step 6): the manager whose club is `team` (0..managers_-1, else -1) and the human sides of a match.
    int manager_of_team(int team) const;
    bool manager_team(int team) const { return manager_of_team(team) >= 0; }
    const HumanSquad* manager_squad(int team) const;
    HumanSide human_sides(MatchStats* stats, std::array<MatchStats, kMaxManagers>& other_stats) const;
    int managers_ = 1;  // DS:0386 (state 0x0DC6): human managers 1..4 (docs/MEHRSPIELER_PLAN.md; more than one is unverified until step 9)
    // Native BSS is zero; only a visited club/national refresh fills its pair.
    // Legacy OEMv1 retains its historical2/3 import policy in GameCheckpoint.
    std::array<std::array<uint8_t,2>,6> line_counts_{};
    LineRotation national_rotation_;
    PenaltyTimers national_timers_;  // DS:D096 row of the national coach (not saved; zero after loading)
    std::array<GroupTable, 2> euro_groups_{};
    GroupTable euro_final_{};
    int euro_round_ = 0, euro_champion_ = -1;
    std::vector<Series> series_;
    std::vector<HokTableRow> table_rows() const;  // the 48 rows of the three leagues as saved
    int playoff_day_ = 0, champion_ = -1, cup_winner_ = -1;
    bool playoffs_set_up_ = false;  // the first round was set up at the step end (not in checkpoints: play_playoff_day redoes it)
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
