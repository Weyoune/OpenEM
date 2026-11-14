// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The day loop: a running game built from a save and EM.EXE, advanced one day at a time as the
// original game does. Game ties the rebuilt parts together and offers the actions of the
// manager screens to the UI; the order of one day is
//   1. season day + 1, date (calendar.hpp)
//   2. events of the day (from the season calendar): league matchday of all three leagues
//      (season.hpp; the human club plays with its squad, its goals get scorers, a home game
//      brings spectators and ticket money - finances.hpp), DEB cup best-of-three series over
//      three dates (competitions.hpp); European cup day (6 group rounds from the save, then a
//      final group of the two best of each group), play-off day (best-of-five series day by
//      day, five days per round; relegation groups of four on the first and third day of each
//      round); world championship day (5 rounds in two groups of six nations, then
//      quarter-finals, semi-finals and the placement games for places 1-8)
//      Each day with chance 1 / (managers + 3) the transfer list is refreshed.
//   3. finances of the next saveable date (finance_day: interest, contracts on the 1st,
//      month-end booking); a menu save of the original already includes the finances of its
//      displayed day
//   4. daily image, then training recovery costs, training and morale at season-day
//      remainder 0 or 4 modulo 7; WM dates use the separate club/healing phase
//   5. sponsor offers before finances on month_length/2 and the last day of the month
// After the last day of the calendar new_season() rolls over (season_end.hpp): promotion and
// relegation (relegation groups, Oberliga 11th/12th against regional clubs 36-41) swap team
// indices, a new European cup draw, prizes, ageing, contract years, retirement of
// computer club players, cleared statistics, 132 summer finance/healing days and a new cup draw.
// The day loop and the season change run every human manager 0..N-1 in the order of the
// original's manager loops; the WM branch books only the manager who is national coach (none
// if that index is not a human manager). Injuries: injuries.hpp (twice-weekly club
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

// An offer (interest) of a computer club for a player of a human manager (see transfers.hpp).
struct ManagerOffer {
    int player = -1;      // index into HokFile::players
    bool listed = false;  // bit 7: the manager had put him on the transfer list; else bit 6
    int bidder = -1;      // team index (+0x16)
    long amount = 0;      // quote field of older OpenEM checkpoints; new interests have no price yet
};

// Outcome of a purchase or loan action on the transfer market.
enum class PurchaseResult { Invalid, Unavailable, Blocked, SellerRefused, InsufficientFunds,
                            SquadFull, PlayerRefused, Purchased, Negotiating,
                            Unfair, OwnerQuestion };  // the last two: a list player of another human manager
// Transient context between the fee editor and the contract panel of a purchase, as in the
// original. The UI keeps it while the temporary roster row exists; saving and advancing the
// day are unavailable until the purchase is finished or explicitly cancelled.
struct PurchaseDraft {
    int pool_slot=-1,player_id=0,club_slot=-1,old_team=-1;  // transfer-list row, player id, new squad slot, former club
    long fee=0,initial_salary=0;  // raw
    int manager=0;  // the buying manager (the active one when the purchase began)
    int seller=-1;  // the human manager who listed the player, -1 = the computer market
    bool loan=false;
};
// A prepared renewal of an advertising contract: the sponsor decides before the confirmation
// opens. The controller keeps this resolution and cannot save/advance while it is pending.
struct AdvertRenewalDraft {
    size_t slot=0;              // advertising slot
    AdvertContract contract{};  // the contract as renewed
    bool sponsor_stays=false;   // the sponsor agreed to extend
};
// A prepared stadium construction awaiting the manager's confirmation.
struct StadiumBuildDraft {
    int item=-1,target=0,weeks=0,discount=0;  // item 0..10 (9 extensions, 2 place kinds), target level/places, build weeks, discount 0..10
    int32_t nominal_cost=0;                   // raw cost before the discount
};
enum class StadiumBuildResult { Invalid,Unavailable,Blocked,InsufficientFunds,Prepared };
enum class ContractResult { Invalid, Unavailable, Blocked, ShorteningNotConfirmed, PlayerRefused, Signed };
enum class TransferMoveResult { Invalid, Unavailable, Blocked, OwnLimit, PoolFull, SquadFull, Moved };
enum class TalentResult { Unavailable, Running, InsufficientFunds, Started };
enum class BankResult { InvalidAmount, Unavailable, InsufficientCash, CreditLimit, Transferred };
enum class StockResult { Invalid, Unavailable, InsufficientCash, InsufficientShares, Traded };

// A small round-robin table: European cup and relegation groups of four, world championship
// groups of six. Ranked by points, then goal difference, then goals scored.
// Points, goals for and goals against are kept as bytes, as in the original (they wrap at 256).
template <size_t N>
struct RoundRobinTable {
    std::array<int, N> teams{};                                // team indices of the group
    std::array<int, N> points{}, goals_for{}, goals_against{};  // 2 points per win, 1 per draw
    // order after the last finished round (indices into teams), best first
    std::array<int, N> ranking = [] { std::array<int, N> o{}; for (size_t i = 0; i < N; i++) o[i] = int(i); return o; }();
    bool ranking_dirty = false;  // results were booked since the last finish_round()
    // Books one result; team indices; teams not in the group are ignored.
    void book(int home, int away, MatchResult r) {
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
        // The original's exchange sort from the previous saved order, including
        // swaps on complete equality. Goal difference is a signed BYTE.
        const auto key = [&](int i) {
            return std::array<int, 3>{points[size_t(i)],
                int(int8_t(uint8_t(goals_for[size_t(i)] - goals_against[size_t(i)]))), goals_for[size_t(i)]};
        };
        for (size_t i = 0; i + 1 < N; i++)
            for (size_t j = i + 1; j < N; j++) if (key(o[j]) >= key(o[i])) std::swap(o[i], o[j]);
        return o;
    }
    // Ends a round: the current order becomes the starting order of the next sort.
    void finish_round() { ranking = order(); ranking_dirty = false; }
};
using GroupTable = RoundRobinTable<4>;

// A running game: the save state plus everything the original keeps only in memory, the tables
// read from EM.EXE and the random generators. advance_day() plays one day; the other public
// members are the actions and views of the manager screens, which act for the active manager.
class Game {
public:
    // Builds the game from EM.EXE and a save. A HOK save of the original starts the random
    // generators from `seed`; an OpenEM checkpoint restores its own generator states.
    // `previous` is only the ongoing session of an explicit live load of a HOK save;
    // it selects the original's main-menu return instead of a cold day-menu setup.
    Game(const Bytes& exe,const Bytes& save,uint32_t seed=1,const Game* previous=nullptr);

    // Plays one day (see the file comment).
    void advance_day();
    // Random generator states (Borland rand and the game's own generator), for tests that
    // compare a whole run against the original game.
    uint32_t borland_state() const { return brng_.state(); }
    uint32_t game_rng_state() const { return rng_.state(); }
    void set_game_rng_state(uint32_t s) { rng_ = GameRandom(s); rng_.borland = &brng_; }  // tests
    void set_borland_state(uint32_t s) { brng_ = BorlandRandom(s); }  // Manager -> Neuer (Session::add_manager)
    // a world just built by the generator (game/world_generator.hpp): the part of September 10 before the first menu
    void enter_first_menu();
    // one manager's turn of the menu pass (transfer-list roll and daily factor), see menu_pass
    void menu_pass_manager(int manager, bool menu = true);
    // For a manager >= 1 the original's match code also touches the record just before his
    // squad block, i.e. squad record 29 of the previous manager; this installs that quirk as a
    // match hook (see game.cpp).
    void install_phantom_hook(int manager);
    // The name database KB/23.vga; without it the season change keeps the player names.
    void set_database(const Database& db) { db_ = db; }
    // The three cursors of the annual distribution live only in the original's process (not in
    // a HOK); tests that continue a recorded run of the original set the values it started with.
    void set_distribution_cursors(const std::array<uint8_t, 3>& c) { annual_distribution_cursors_ = c; }
    // The game as a savegame (HOK): date and matchdays (the next day is the one to play), tables,
    // next pairings, cup draw, manager 0's money, attendance and image, squad and player records.
    // Includes sponsor offers, advertising contracts, accrued interest, European cup and WM
    // tables/results, play-off series and relegation groups.
    Bytes save_bytes() const;
    // The original's save action: refreshes preview and header, consumes four Borland draws
    // and chooses a new session nonce/cipher key. Rendering and checkpoint creation use the
    // pure save_bytes() above.
    Bytes native_save_bytes();
    // Value quote of squad slot `slot` of `manager` as the player-detail page computes it when
    // it opens; flagged market records can consume one Borland draw. Repainting must reuse the
    // returned quote.
    long quote_player_value(int manager,int slot);
    // OpenEM checkpoint for a deterministic continuation; the original game cannot load it.
    // Rebind external database/contract callbacks after loading, as for a HOK save.
    Bytes checkpoint_bytes() const;

    int season_day() const { return day_; }
    GameDate date() const { return date_of_season_day(day_, save_.year); }
    const League& league(int l) const { return leagues_[size_t(l)]; }
    int next_matchday(int l) const { return matchday_[size_t(l)]; }  // 1-based
    // The accessors and UI actions below act for the active manager (hot seat); it is manager
    // 0 unless select_manager() chose another one of the managers() human managers.
    const ManagerBudget& budget() const { return mgr_[size_t(active_manager_)].budget; }
    int image() const { return mgr_[size_t(active_manager_)].image; }
    int game_level() const { return finance_.level; }  // 0 professional .. 5 beginner
    // The rule set of the EM.EXE the game runs with (version 1.00 or 1.30, see game/rules.hpp);
    // rules().label() is the version text of the settings page.
    const GameRules& rules() const { return rules_; }
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
    int national_coach() const { return national_coach_; }  // index of the manager who is national coach, or -1
    int national_team() const { return national_coach_ >= 0 ? save_.state.at(0x068C) : -1; }  // team index of his nation
    std::vector<HokSquadPlayer> national_squad() const { return hok_squad(save_, 5); }  // squad block 5
    // Nominates (or drops) a player for the national squad; false if not allowed.
    bool select_national_player(size_t player, bool selected);
    // Lets the game nominate the national squad automatically.
    bool auto_select_national_squad();
    // ---- WM-only mode "Nur WM" (state 0x025A = 1, see wm_mode.cpp)
    bool wm_only() const;
    bool wm_started() const { return wm_round_ > 0; }  // the flag was clicked: the tournament runs
    int friendlies_played() const;                       // friendlies played before the WM
    // Opening the friendlies page (national coach before the WM) sets every nominee's exhaustion to 70.
    bool enter_friendlies();
    bool preview_friendly();  // first click on a flag: line overview (auto line-up of the national squad)
    int lineup_manager() const;  // the squad of the line-up page: 5 (national squad) for the national coach
    // Result of play_friendly().
    struct FriendlyOutcome {
        bool played = false;
        std::string refusal;  // notice text of the original when the game was refused
        int home = -1, away = -1, home_goals = 0, away_goals = 0, bonus = 0;
        long price = 0;  // raw
    };
    // A friendly of the national team against `opponent` (team index), at home or away.
    FriendlyOutcome play_friendly(int opponent, bool home);
    // ABWEHR/STURM values of lines 1-3 ([kind][line]); for the own nation with its WM squad.
    std::array<std::array<int, 3>, 2> line_overview(int team) const;
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
    // Sale of a player: block 0 = the active manager's club, 4 = the transfer list; `native_si`
    // is the screen's selection index. begin computes the quote, finish sells or declines.
    bool begin_sale_offer(int block,int slot,int16_t native_si,SaleQuote&);
    bool finish_sale_offer(const SaleQuote&,bool sell);
    // Buy an ordinary computer-market player from transfer-list slot 0..10. Fee and salary
    // are raw (DM * 3), years 1..4. Refusals as in the original mark the saved manager ban;
    // funds/capacity failures keep ownership and roster intact. Optional sponsor
    // subsidies are declined unless explicitly accepted by this action.
    PurchaseResult buy_transfer_player(size_t slot, long fee, int years, long salary,
                                       bool accept_subsidy = false);
    // The same purchase in two steps, as the screens do it: the fee offer (which prepares
    // `draft` for the contract panel), then the contract terms or a cancel.
    PurchaseResult begin_transfer_purchase(size_t slot,long fee,PurchaseDraft& draft);
    PurchaseResult finish_transfer_purchase(const PurchaseDraft&,int years,long salary,bool accept_subsidy=false,bool prepare_lineup=true);
    bool cancel_transfer_purchase(const PurchaseDraft&);
    // Loan of a computer transfer-list player for one season (loan.cpp).
    // The fee is raw; Purchased means the player joined on loan.
    PurchaseResult loan_transfer_player(size_t slot,long fee,bool accept_subsidy=false,bool prepare_lineup=true);
    // A bid for a list player of another human manager (purchase.cpp): Blocked, InsufficientFunds,
    // Unfair (outside 60..140 % of the value, no refusal bit) or OwnerQuestion with `offer` for the owner's answer.
    // answer_manager_offer: refusal -> SellerRefused and the bidder's bit; acceptance -> Negotiating (purchase, `draft`
    // for the contract panel) or Purchased (loan). The owner gets the fee, there is no sponsor subsidy.
    PurchaseResult offer_to_manager(size_t slot,long fee,bool loan,PurchaseDraft& offer);
    PurchaseResult answer_manager_offer(const PurchaseDraft& offer,bool accept,PurchaseDraft& draft,bool prepare_lineup=true);
    // Sponsor subsidy offer of the last purchase/loan, raw, 0 = none; not in a checkpoint. The UI asks the
    // question afterwards and books an accepted offer with accept_purchase_subsidy (no further draws).
    long last_purchase_subsidy() const { return last_purchase_subsidy_; }
    bool accept_purchase_subsidy(long raw);
    // Talent scout (talent.cpp): price (raw, state 0x18), running counter, start.
    long talent_search_price() const;
    int talent_search_days() const;
    TalentResult start_talent_search();
    void clear_message_flag(int manager);  // the manager's "new messages" flag (messages.cpp)
    // Messages for the manager from the last day step (talent scout), oldest first. They are not part
    // of a checkpoint: the UI reads them right after advancing.
    // Without an argument: those of the active manager.
    const std::vector<std::string>& manager_news() const { return manager_news_[size_t(active_manager_)]; }
    const std::vector<std::string>& manager_news(int m) const { return manager_news_.at(size_t(m)); }
    // Messages the original shows at once and deletes again (season review), original game text, oldest first.
    const std::vector<std::string>& immediate_messages() const { return immediate_messages_[size_t(active_manager_)]; }
    const std::vector<std::string>& immediate_messages(int m) const { return immediate_messages_.at(size_t(m)); }
    // The manager's season-review page when the last step changed the season, not in a checkpoint.
    // The original shows the page of every manager 0..N-1 in index order before the contract talks.
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
    void finish_transfer_menu(); // leaving the transfer page: automatic lineup and strength refresh
    void enter_strengths(); // opening the strengths page recomputes the team strengths (unlike pure rendering)
    void enter_club_info(int team);  // opening the club information page of `team`
    int set_team_reporting(int team,int mode); // match reports of `team`: 0 off, 1 intermediate results, 2 goals
    // Einstellungen page (settings.hpp): switches, game speed. Pure state bytes, no random numbers.
    GameSettings settings() const;
    int toggle_setting(int widget,int manager);  // flips switch `widget`; returns its new state
    void set_game_speed(int speed);
    // Highscore page entry (highscore.hpp): stores a fresh image and values the squad, both drawing random numbers.
    HighscoreRecord highscore_record(int manager, const HighscoreTables& tables);
    // Positive raw amount: deposit/repay. Negative: withdraw/borrow. The bank
    // permits withdrawal of all savings, or 6,000,000 raw minus existing debt,
    // whichever is greater (the savings are not added to the credit allowance).
    // Each matrix side is posted independently; the bank's positive counter
    // claim determines outstanding debt even for an asymmetric imported matrix.
    BankResult bank_transfer(long amount);
    // Buys or sells `quantity` shares of `company` (0..3) on the bank screen. Each direction
    // pays 1.5 % commission; the cost basis of a partial sale keeps the original's two-stage
    // percentage rounding.
    StockResult trade_shares(size_t company,uint32_t quantity,bool sell);
    // One click of the manual lineup screen for the active manager (see lineup.hpp).
    ManualLineupResult change_lineup(size_t slot,int selected_line,ManualLineupAction action,const bool* six_override=nullptr,
                                   const std::array<std::array<int,6>,5>* cells_override=nullptr,
                                   bool in_match=false);  // in_match: from the board of a running match
    bool set_tactics(const TacticsPlan& plan);
    const TrainingPlan& training_plan() const { return mgr_[size_t(active_manager_)].plan; }  // state 0x070C + 0x1C * m
    // Commit a full club plan (kinds 0..8), including its weekly recovery fee.
    // Invalid plans/phases leave the game unchanged; no random draws or cash posting.
    bool set_training_plan(const TrainingPlan& plan);
    const SponsorOffers& sponsor_offers() const { return mgr_[size_t(active_manager_)].offers; }
    // Opening the advertising page recomputes and stores the full monthly income, as in the
    // original. It reads existing offers; there is no offer generation or random draw.
    bool enter_advertising();
    // Kind 0: seven boards, possibly several per offer. Kinds 1..4: slots 7..10.
    // A failed acceptance leaves the offers and contracts unchanged.
    bool accept_sponsor_offer(int kind, size_t offer);
    // Resolve an expired option for a whole board group or a single other contract.
    // Returns whether the extension was agreed; the sponsor decision precedes confirmation.
    bool renew_advert_contract(size_t slot, bool accept = true);
    // The same renewal in two steps: the sponsor's decision, then the manager's confirmation.
    bool begin_advert_renewal(size_t slot,AdvertRenewalDraft& draft);
    bool finish_advert_renewal(const AdvertRenewalDraft&,bool accept);
    const StadiumTables& stadium_tables() const { return stadium_tables_; }
    // Per manager and stadium item: the construction gate the original keeps only in memory
    // (not saved in a HOK; see stadium_actions.cpp).
    const std::array<std::array<uint8_t,11>,4>& stadium_refusals() const { return stadium_refusals_; }
    // An explicit live load of a HOK save keeps the running random streams and construction
    // gates of the session. An OpenEM checkpoint instead restores the ones saved in it.
    void inherit_native_runtime(const Game& previous) {
        stadium_refusals_=previous.stadium_refusals_;
        brng_=previous.brng_;rng_=previous.rng_;
        line_counts_=previous.line_counts_;
        annual_distribution_cursors_=previous.annual_distribution_cursors_;
        for(size_t m=0;m<mgr_.size();++m)for(int kind=0;kind<2;++kind)mgr_[m].squad.per_line[kind]=line_counts_[m][size_t(kind)];
    }
    bool set_ticket_price(int price);  // DM, 10..30
    // Immediately edits one monthly youth grant; no random draws.
    bool adjust_youth_funding(int kind,int x);
    // History of the original's save slots (see save_history.hpp).
    const NativeSaveHistory& native_save_history() const { return save_history_; }
    void set_native_save_history(const NativeSaveHistory& history) {
        if(history.count>50)throw FormatError("native save history: invalid counter");
        save_history_=history;
    }
    // Stadium construction in two steps: the quote for `item` up to `target`, then the
    // manager's confirmation (which books the cost and starts the countdown).
    StadiumBuildResult begin_stadium_build(int item,int target,StadiumBuildDraft&);
    bool finish_stadium_build(const StadiumBuildDraft&,bool accept);
    // Contract talks at the season end are the manager's: for every
    // squad player whose contract ran out the decider gets the player and the salary he asks
    // (raw) and returns the years offered (0 = let him go). Without one the club offers 2 years.
    using ContractDecider = std::function<int(const ContractPlayer& player, long demand)>;
    void set_contract_decider(ContractDecider d) { contract_decider_ = std::move(d); }
    // The same talks as the original's dialog "Vertragsverhandlung" offers them: the manager answers with
    // years (0 = "Abbruch", the player leaves), a salary (raw; 0 = what the player asks for these years) and the
    // insurance. Used instead of the decider above when set. `slot` is the player's squad slot at that moment.
    struct ContractTerms { int years = 1; long salary = 0; bool insured = false; };
    using ContractTermsDecider = std::function<ContractTerms(const ContractPlayer& player, int slot, long proposal)>;
    void set_contract_terms_decider(ContractTermsDecider d) { contract_terms_decider_ = std::move(d); }
    int daily_luck() const { return daily_luck_; }  // daily factor 85..115, the mood of the dialog (renewal_demand)
    // The manager whose contract talk the deciders above are asked for (the talks run for 0..N-1 in index order).
    int contract_manager() const { return contract_manager_; }
    // Outcome of each contract talk of the last season change, in order over all managers (notices of the dialog).
    struct ContractOutcome { std::string name; bool signed_on = false, refused = false; long fee = 0; bool too_long = false; int manager = 0; };
    const std::vector<ContractOutcome>& contract_outcomes() const { return contract_outcomes_; }
    // Match display (ui/classic_match_view): observes the minute loops of league and cup days; never changes the game.
    void set_match_observer(MatchMinuteObserver o) { match_observer_ = std::move(o); }
    // Live DEB cup draw of a later round: with the cup display switch on, not in the WM-only
    // mode and with four or more clubs, the original asks "An Alle:" whether to follow the draw
    // live. The decider gets the number of clubs and answers; without one the answer is "Kein
    // Gedanke". "Aber klar" uncovers the places after the twenty swaps in Borland order
    // rand(0, n - 1) (drawn again while the place is already open), which the observer then
    // gets (places in uncovering order, club of each place). Called inside advance_day.
    using CupDrawDecider = std::function<bool(int clubs)>;
    using CupRevealObserver = std::function<void(const std::vector<int>& order, const std::vector<int>& places)>;
    void set_cup_draw_decider(CupDrawDecider d) { cup_draw_decider_ = std::move(d); }
    void set_cup_reveal_observer(CupRevealObserver o) { cup_reveal_observer_ = std::move(o); }
    // The end year (state 0xAEB5) was reached in the season change: the game is over (highscore, "Das wars...").
    bool game_over() const { return game_over_; }
    long match_attendance() const { return match_attendance_; }  // spectators of the last home game of the human club
    // The scorer / assist picks for the managers' chances and shoot-out shots of the running day, in order (the
    // match display names the players of its board messages from them; squad slots, -1 none). Cleared when a day starts.
    struct ChanceCredit {
        int team = -1;              // team index of the attacking club
        bool goal = false, shot = false;
        int scorer = -1, assist = -1;  // squad slots, -1 none
    };
    const std::vector<ChanceCredit>& chance_credits() const { return chance_credits_; }
    bool match_sold_out() const { return match_sold_out_; }  // the last home game was sold out
    void decline_offer(size_t i);  // declines open offer i of offers()
    // English diagnostic log of the bookings (not shown in the game screens).
    const std::vector<std::string>& log() const { return log_; }
    void clear_log() { log_.clear(); }

private:
    void play_league_day();
    // Per-manager steps take the manager index m (0..3) last.
    void manager_day(bool home_game_today, int m = 0);  // the daily squad bookkeeping of one manager
    void spin_match_stream();                           // advances the match random stream, once per match day with a manager game
    void update_series(int team, int side, int goals_for, int goals_against);
    void record_head_to_head(int home, int away, int home_goals, int away_goals, int m = 0);
    void update_records(int home, int away, int home_goals, int away_goals);
    void book_special_teams(const MatchStats& stats, int side, int manager = 0);
    void play_cup_day();
    void close_cup_final();  // closes the cup final series at its third game (game index 2)
    bool human_in_next_cup() const;
    bool human_in_next_playoff() const;
    void quick_setup(int home, int away, HokTeam& h, HokTeam& a);
    bool human_in_next_euro() const;
    void play_euro_day();
    void play_playoff_day(bool setup_only = false);  // setup_only: the first round set-up of the phase, run at the step end before it
    void store_playoff_state(int stage, int completed_days);
    void play_wm_day();
    void rest_for_national_match();
    void count_national_caps(const std::vector<HokSquadPlayer>& national);
    std::vector<Tie> national_live_round(const std::vector<HokPairing>& draw, bool knockout, HumanSquad& squad,
                                         std::vector<HokSquadPlayer>& national, MatchStats& live_stats, int level = 1);
    void begin_world_championship();
    void finish_world_championship();
    void refresh_national_team();
    void book_national_match(int own_goals, const MatchStats& stats, int side, int shootout_goals = 0, bool credit_goals = true);
    // one chance of the national team in a live WM minute (as for a club, with squad block 5): scorer and assist picks, goal booking
    void credit_national_chance(bool penalty, bool goal, const HokTeam* on_ice, const std::vector<HokSquadPlayer>* working);
    // the per-minute injury check of the national coach's match (injury_minute for squad block 5)
    void national_injury_minute(int load, const HokTeam* on_ice, std::vector<HokSquadPlayer>& national);
    void book_match_condition(const MatchStats& stats, int side, int manager);
    bool book_forfeit(const MatchStats& stats, int manager = 0);
    void book_club_match(int own_goals, int goals_against, int competition, bool league_statistics, int shootout_goals = 0,
                         bool credit_goals = true, int m = 0);
    void credit_human_goal(int competition, bool penalty, bool goal = true, const HokTeam* on_ice = nullptr, const std::vector<HokSquadPlayer>* working = nullptr, int m = 0);
    void credit_opponent_goal(int competition, bool goal, const HokTeam& scorer);
    bool message_step_pending_ = true;  // a loaded game stands in the menu: the next play step sorts and counts the messages
    bool table_copies_frozen_ = false;   // a play step passed a league day without returning to the menu: the copies of the table stay (HokFile::keep_table_copies)
    bool league_since_refresh_ = false;  // a league day was played since the copies were last refreshed
    int last_scorer_slot_ = 0;  // slot of the last scorer pick, reused when a club without a squad scores
    void reset_wm();
    void wm_only_prepare();  // WM-only mode: jump to the WM and appoint manager 0 (wm_mode.cpp)
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
    void squad_injuries(bool weekly, bool no_match_today, int m = 0, int date_offset = 1);  // the message date: day_ + date_offset
    void heal_squad(bool weekly, int m = 0);
    void reload_squads();  // the decoded squads of managers 0..N-1 from their squad records in the save state
    void refresh_budget_roster(int m = 0);
    void clear_insurance_locks(int m = 0);
    void prepare_human_lineup(bool restore_saved_mode = true, int m = 0);
    void match_injuries(int m = 0);
    void injury_minute(int load, const HokTeam* on_ice = nullptr, int m = 0);  // the injury check of one minute of the human club's match
    void apply_phantom_pairs(int role);  // the pair-table write of a phantom release outside a league day (role < 0: none)
    void goalie_minute(int conceded, int m);  // goalkeeper minutes and goals against at the end of a league match minute
    void make_offers(int m = 0);
    void restore_offers(int m = 0);
    void expire_contracts(int m = 0);
    HokPlayer* player_by_exe_index(int exe_index);  // squad +0x0F = HOK player record + 1
    void remove_squad_record(int block, int slot, int end_slot);
    void refresh_human_team(int m = 0);
    void weekly_team_drift();  // drift of the team values of all 64 national and club teams, twice a week
    void talent_search_week();  // talent.cpp: the weekly scout step of every manager in index order
    void talent_search_step(int m);
    bool talent_player(int skill,long percent,int m = 0);  // creates a found talent (talent.cpp)
    void refresh_menu_manager(int manager);
    void update_sponsors(int m = 0);
    int current_club_image(int m = 0);
    void expire_advert_options(int m = 0);
    int team_rank(int team) const;

    HokFile save_;
    uint64_t exe_fingerprint_ = 0;
    GameRules rules_;  // version 1.00 or 1.30 of EM.EXE (game/rules.hpp)
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
    int daily_luck_ = 100;                // valuation factor 85..115 of the current play step
    bool injuries_in_minutes_ = false;    // the league match already ran the injury checks per minute
    bool national_hurt_today_ = false;  // a national player was hurt in the live WM round (the original opens the line-up screen)
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
    CupDrawDecider cup_draw_decider_;
    CupRevealObserver cup_reveal_observer_;
    bool game_over_ = false;
    long match_attendance_ = 0;
    std::vector<ChanceCredit> chance_credits_;
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
    // The human managers; the day loop simulates 0..managers_-1.
    struct ManagerState {
        int team = -1;                // manager block +0x14
        HumanSquad squad;             // the squad and its controls in matches
        LineRotation rotation;        // line changes during a match
        PenaltyTimers timers;         // running penalties (not saved; zero after loading, as in the original)
        ManagerBudget budget;         // finances (finances.hpp)
        SponsorOffers offers{};       // open sponsor offers
        TrainingPlan plan{};          // state 0x070C + 0x1C * manager
        int image = 0;                // club image
        bool played = false;          // the club played today
        std::vector<ManagerOffer> transfer_offers;  // open interests of computer clubs in his players
        std::array<int, 30> injured_role{};    // role (+0x0A) of an injured player before he got hurt
        std::array<bool, 30> hurt_in_match{};  // injured during the running match: still counted as having played
        bool goalie_tracked = false;  // the running league match booked goalie minutes and goals against minute by minute
    };
    std::array<ManagerState, 4> mgr_{};
    int active_manager_ = 0;  // the manager the UI actions act for; the day loop plays all managers
    // Match layer: the manager whose club is `team` (0..managers_-1, else -1) and the human sides of a match.
    int manager_of_team(int team) const;
    bool manager_team(int team) const { return manager_of_team(team) >= 0; }
    const HumanSquad* manager_squad(int team) const;
    HumanSide human_sides(MatchStats* stats, std::array<MatchStats, kMaxManagers>& other_stats) const;
    int managers_ = 1;  // state 0x0DC6: human managers 1..4 (more than one is less thoroughly verified)
    // Lines per kind of each squad block. The original starts with zeros; only a visited
    // club/national refresh fills its pair. Version 1 OpenEM checkpoints keep their historical
    // 2/3 import policy in GameCheckpoint.
    std::array<std::array<uint8_t,2>,6> line_counts_{};
    LineRotation national_rotation_;
    std::vector<HokSquadPlayer>* live_national_ = nullptr;  // the working national squad of a running national match (live line-up)
    std::array<bool, 29> national_played_{};  // slots taken off by a live line-up change (their appearance stays booked)
    bool national_out_ = false;  // the coach's nation is out after the quarter-finals (the WM days after them)
    HumanSquad* live_national_human_ = nullptr;  // the national HumanSquad of a running national match (live tactics)
    PenaltyTimers national_timers_;  // running penalties of the national team (not saved; zero after loading)
    std::array<GroupTable, 2> euro_groups_{};
    GroupTable euro_final_{};
    int euro_round_ = 0, euro_champion_ = -1;
    std::vector<Series> series_;
    std::vector<HokTableRow> table_rows() const;  // the 48 rows of the three leagues as saved
    int playoff_day_ = 0, champion_ = -1, cup_winner_ = -1;
    bool playoffs_set_up_ = false;  // the first round was set up at the step end (not in checkpoints: play_playoff_day redoes it)
    int seasons_ = 0;
    std::array<RoundRobinTable<6>, 2> wm_groups_{};  // nations 52-57 and 58-63
    std::vector<int> wm_slots_;                       // knock-out slots, pairs (0,1), (2,3), ...
    std::vector<int> wm_ranking_;                     // final places 1-8
    int wm_round_ = 0, wm_champion_ = -1;             // WM round counter (0 = not started), champion
    int national_coach_ = -1;
    std::array<GroupTable, 2> relegation_{};                 // [0] first/second, [1] second/Oberliga
    std::array<std::array<int, 4>, 2> relegation_upper_{};  // final group order: the first two play above
    std::vector<std::string> log_;
    Database db_;
};

}  // namespace em
