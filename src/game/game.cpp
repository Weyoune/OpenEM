// SPDX-License-Identifier: GPL-3.0-only
#include "game/game.hpp"
#include "game/manager_layout.hpp"
#include "game/market.hpp"
#include "game/messages.hpp"
#include "game/result_books.hpp"
#include "game/youth_progress.hpp"
#include "game/youth_intake.hpp"
#include "game/annual_players.hpp"
#include "game/annual_distribution.hpp"
#include "game/checkpoint.hpp"
#include "game/lineup.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace em {

namespace {
constexpr int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};  // DS:0393
constexpr size_t kSquad = squad_block(0), kSquadRecord = 0x34, kTeamRecord = 0x3D;
constexpr size_t kManagerBlock = manager_block(0);  // manager 0, see finances.hpp and game/manager_layout.hpp
// The rest of the human squad on a EuroCup day without the own club sets the fitness from the load byte +0x13 like
// the match set-up (prepare_match_energy: min(90, 100 - load / 8)), not a flat 90 (REAL-CODE Köln periods 14 and 15:
// load 113 -> 86, 98 -> 88; the Mannheim squads had loads up to 80 there). A cup day without the own club rests to a flat 90
// (Köln periods 5 and 12 break with the formula there).
void debug_pclub(const Bytes& s, const char* tag) {  // EM_DEBUG_PCLUB=<player id>: its club byte at points of the season change
    if (const char* pc = std::getenv("EM_DEBUG_PCLUB")) std::fprintf(stderr, "pclub %s %d\n", tag, s.at(0x4FC6 + size_t(std::atoi(pc) - 1) * 33 + 0x1D));
}
uint8_t rested_fitness(uint8_t load) { return std::getenv("EM_OLD_RESTFIT") ? uint8_t(90) : uint8_t(std::min(90, 100 - load / 8)); }
constexpr size_t kSeries = 0x9988, kSeriesRecord = 0x9EC8, kSpecialTeams = 0x0F44;
constexpr int kSeriesTeams = 64;  // DS:B57B .. DS:BAB7
constexpr int kFirstNationalTeam = 52;  // DS:06AD
constexpr size_t kHeadToHead = 0x8F88;  // DS:AB7B, see record_head_to_head
constexpr size_t kEternal = 0x3BF1;      // u32 eternal table points, + team * 0x50 (DS:82A3)
constexpr size_t kTitles = 0x3BA5;       // u8[3] titles won (champion, cup, European cup), + team * 0x50
constexpr size_t kManagerBalance = 0x0F08;  // manager 0's eternal balance, 8 u16 pairs
constexpr size_t kRecordOpponents = 0x9F1C, kRecords = 0xA11C;  // DS:BB0F, DS:BD0F, see update_records
constexpr int kFirstRegionalTeam = 36;  // DS:07C2 + DS:07B3: regional clubs 36-47
constexpr size_t kPlayers = 0x4FC6, kPlayerRecord = 0x21;  // HOK state: player records
constexpr size_t kCalendarCursor = 0xB953;  // u16 DS:A969 (alias 2a9e:4F59), event cursor (ovl14_2e0f)
constexpr size_t kCareerYear = 0xB951;  // u16 DS:03BD, completed career seasons
constexpr size_t kLeagueResults = 0x0796;  // DS:A0CB, 3 leagues x 44 days x 12 score bytes
constexpr size_t kCupGame = 0x031B, kCupScores = 0x031C, kCupWins = 0x037C,
                 kCupPending = 0x077C, kCupLive = 0x8F00;
// ovl05_1322 / ovl05_0ef7: kinds 0 WM, 1 relegation, 2 EuroCup groups, 3 EuroCup final.
// Each kind has two groups of six table slots and two blocks of six rounds x six score bytes.
constexpr size_t kGroupPoints = 0x0628, kGroupGoalsFor = 0x02BB, kGroupGoalsAgainst = 0x02EB,
                 kGroupOrder = 0x05F8, kGroupResults = 0x04D8, kGroupSchedule = 0x03B8;
constexpr size_t kEuroFinalTeams = 0x03AC, kEuroRound = 0x8F86;
constexpr size_t kWorldRound = 0x8F85, kWorldSlots = 0x8EB1, kWorldResults = 0x8F60,
                 kWorldCursor = 0x03B7;  // DS:07AE, 5D5B, 5CAB, CFFB
constexpr size_t kWorldHistory = 0x3BD9;  // DS:828B, sixteen placements per nation, stride 0x50
constexpr size_t kPlayoffTeams = 0x8E91, kPlayoffWins = 0x024B, kPlayoffScores = 0x0205,
                 kPlayoffGame = 0x0259, kPlayoffStage = 0x8F87, kPlayoffCursor = 0x03B6,
                 kPlayoffPending = 0x077D, kPlayoffLive = 0x8F40, kRelegationTeams = 0x039C;
constexpr int kPlayoffBase[3] = {0, 8, 12};
// state offsets of the season review values: champion DS:D0E6, European cup winner DS:0388, and
// the German European cup entrants of ovl05_51a5 (DS:A776 list, DS:A763 slots 3 x 6, 0x80 = free)
constexpr size_t kStateCupRunnerUp = 0x0DCC;  // DS:038B, the losing cup finalist (REAL-CODE period 28)
constexpr size_t kStateCupWinner = 0x0DC7, kStateChampion = 0x0DC8, kStateEuroWinner = 0x0DC9, kStateEntrants = 0x8E33,
                 kStateEntrantSlots = 0x8E3D;

template <size_t N>
RoundRobinTable<N> native_group(const Bytes& state, int kind, int group, const std::array<int, N>& teams) {
    RoundRobinTable<N> table{teams};
    const size_t base = size_t(kind * 12 + group * 6);
    for (size_t i = 0; i < teams.size(); i++) {
        table.points[i] = state.at(kGroupPoints + base + i);
        table.goals_for[i] = state.at(kGroupGoalsFor + base + i);
        table.goals_against[i] = state.at(kGroupGoalsAgainst + base + i);
    }
    std::array<bool, N> seen{};
    bool valid = true;
    for (size_t i = 0; i < N; i++) {
        const int place = state.at(kGroupOrder + base + i);
        if (place < 0 || size_t(place) >= N || seen[size_t(place)]) { valid = false; break; }
        seen[size_t(place)] = true;
    }
    if (valid)
        for (size_t i = 0; i < N; i++) table.ranking[i] = state.at(kGroupOrder + base + i);
    // Unused native zero blocks have no permutation yet; retain identity.
    return table;
}

template <size_t N>
void store_native_group(Bytes& state, const RoundRobinTable<N>& table, int kind, int group) {
    const size_t base = size_t(kind * 12 + group * 6);
    const auto order = table.order();
    for (size_t i = 0; i < table.teams.size(); i++) {
        state.at(kGroupPoints + base + i) = uint8_t(table.points[i]);
        state.at(kGroupGoalsFor + base + i) = uint8_t(table.goals_for[i]);
        state.at(kGroupGoalsAgainst + base + i) = uint8_t(table.goals_against[i]);
        state.at(kGroupOrder + base + i) = uint8_t(order[i]);
    }
}

void store_group_schedule(Bytes& state, const GroupTable& table, int kind, int group,
                          const ScheduleTables& schedule) {
    for (int round = 0; round < 6; round++) {
        const auto pairs = round_robin_pairings(schedule.teams4, round);
        for (size_t pair = 0; pair < pairs.size(); pair++) {
            const size_t at = kGroupSchedule + size_t(kind * 0x48 + group * 0x24 + round * 6) + pair * 2;
            state.at(at) = uint8_t(table.teams[size_t(pairs[pair].home)]);
            state.at(at + 1) = uint8_t(table.teams[size_t(pairs[pair].away)]);
        }
    }
}

void store_cup_participants(HokFile& save, const std::vector<HokPairing>& draw, int round) {
    save.cup_draw = draw;
    save.cup_round = round;
    // the original writes the new pairs and one end marker; the rest of the 32 bytes keeps the previous round (REAL-CODE period 7)
    if (draw.size() * 2 < 32) save.state.at(0x8E51 + draw.size() * 2) = 0xFF;
    for (size_t pair = 0; pair < draw.size(); pair++) {
        save.state.at(0x8E51 + pair * 2) = uint8_t(draw[pair].home);
        save.state.at(0x8E52 + pair * 2) = uint8_t(draw[pair].away);
    }
    save.state.at(0x8ED1) = uint8_t(round);
}


}  // namespace

Game::Game(const Bytes& exe,const Bytes& save,uint32_t seed,const Game* previous)
    : save_(decode_hok(game_save_hok(exe, save))),
      exe_fingerprint_(game_data_fingerprint(exe)),
      schedule_(load_schedule_tables(exe)),
      calendar_(load_season_calendar(exe)),
      finance_(load_finance_tables(exe, save_)),
      contracts_(load_contract_tables(exe)),
      sponsor_tables_(load_sponsor_tables(exe)),
      training_(load_training_tables(exe)),
      injuries_(load_injury_tables(exe)),
      national_tables_(load_national_team_tables(exe)),
      rating_(load_rating_bounds(exe)),
      rng_(seed),
      brng_(seed) {
    stadium_tables_=load_stadium_tables(exe);
    contracts_.level = training_.level = finance_.level;
    sponsor_tables_.economy = int32_t(le32(save_.state, 0x06B4));  // saved DS:02C0
    for (int l = 0; l < 3; l++) {
        leagues_[size_t(l)] = league_from_save(save_, l);
        matchday_[size_t(l)] = save_.matchday[l];
    }
    // The preview name is truncated to19 bytes and may duplicate another
    // team's name (notably national previews). Native load uses manager+14.
    mgr_[0].team = save_.state.at(kManagerBlock + 0x14);
    managers_ = std::clamp(int(save_.state.at(kManagerCount)), 1, kMaxManagers);
    if (managers_ > 1) log_.push_back("warning: " + std::to_string(managers_) + " managers in the save; OpenEM simulates manager 0 only so far");
    if(mgr_[0].team<0||mgr_[0].team>=kHokTeams)throw FormatError("HOK: invalid manager club");
    // the header holds the calendar year of the save (1994 in MONAT4, February), DS:03BB the year
    // the season started; the day loop counts from the season start
    if (save_.season_year > 0) save_.year = save_.season_year;
    for (int m = 0; m < kMaxManagers; m++) {  // the match controls of every human squad
        ManagerState& mg = mgr_[size_t(m)];
        mg.squad.squad = &save_.squad_of(m);
        mg.squad.players = &save_.players;
        mg.squad.level = finance_.level;
        mg.squad.selection_rng = &brng_;
        mg.squad.minimum_players = 9;
        mg.squad.rotation = &mg.rotation;
        mg.squad.timers = &mg.timers;
    }
    rng_.borland = &brng_;
    mgr_[0].budget = budget_from_save(save_, 0);
    mgr_[0].offers = sponsor_offers_from_save(save_.state, 0);
    for (int d = 0; d < kTrainingDays; d++)
        for (int u = 0; u < kTrainingUnits; u++) plan_[size_t(d)][size_t(u)] = save_.state[0x070C + size_t(d * 4 + u)];
    mgr_[0].image = le16(save_.state, 0x0DCD + 0x173);
    // Managers 1.. (only with more than one manager): club, money, sponsor offers and image for the UI actions
    // (step 5), the match layer (step 6) and the day loop (step 7).
    for (int m = 1; m < managers_; m++) {
        ManagerState& mg = mgr_[size_t(m)];
        mg.team = save_.state.at(manager_block(m) + 0x14);
        if (mg.team < 0 || mg.team >= kHokTeams) throw FormatError("HOK: invalid manager club");
        mg.budget = budget_from_save(save_, m);
        mg.offers = sponsor_offers_from_save(save_.state, m);
        mg.image = le16(save_.state, manager_block(m) + 0x173);
    }
    cup_draw_ = save_.cup_draw;
    for (size_t g = 0; g < 2; g++) euro_groups_[g] = native_group(save_.state, 2, int(g), save_.euro_groups[g]);
    // the save's day has not been played yet: the next advance_day() handles it
    day_ = save_.date.day_index - 1;
    for (int d = 1; d <= day_; d++) {
        cup_day_ += calendar_.event(d) == kEventCup;
        playoff_day_ += calendar_.event(d) == kEventPlayoff;
    }
    euro_round_ = std::clamp(int(save_.state.at(kEuroRound)), 0, 12);
    cup_round_ = save_.cup_round;
    cup_game_ = std::clamp(int(save_.state.at(kCupGame)), 0, 2);
    if (cup_round_ == 5 && cup_day_ >= 15) cup_winner_ = save_.state.at(kStateCupWinner);
    // a final closed early (play_cup_day): the winner stands twice in the cup grid
    if (cup_round_ == 5 && cup_winner_ < 0 && save_.state.at(0x8E51) == save_.state.at(0x8E52) && save_.state.at(0x8E51) < kHokTeams)
        cup_winner_ = save_.state.at(0x8E51);
    if (cup_game_ > 0) {
        for (size_t pair = 0; pair < cup_draw_.size(); pair++) {
            const HokPairing& p = cup_draw_[pair];
            Series s;
            s.higher = cup_game_ % 2 == 0 ? p.home : p.away;
            s.lower = s.higher == p.home ? p.away : p.home;
            const int played = cup_winner_ >= 0 && p.home != p.away ? 3 : cup_game_;  // winner against winner: the replayed games only
            for (int game = 0; game < played; game++) {
                const size_t o = kCupScores + size_t(game) * 32 + pair * 2;
                const int a = save_.state.at(o), b = save_.state.at(o + 1);
                if (a == 100 || b == 100) continue;
                if (a == 0 && b == 0 && cup_round_ == 5 && !std::getenv("EM_OLD_FINALCLOSE")) continue;  // the unplayed game of a closed final
                const bool a_home = (game % 2 == 0) == (s.higher == p.home);
                const int marker = (a / 30 + b / 30) * 30;
                const MatchResult r{(a_home ? a : b) % 30, (a_home ? b : a) % 30, marker >= 60, marker};
                s.games.push_back(r);
                if ((r.home > r.away) == (game % 2 == 0)) s.wins_higher++; else s.wins_lower++;
            }
            cup_series_.push_back(s);
        }
        // (EM_OLD_FINALCLOSE: a final decided after two games had its winner at once; now the third final day books it)
        if (std::getenv("EM_OLD_FINALCLOSE") && cup_round_ == 5 && cup_winner_ < 0 && cup_series_.size() == 1 && (cup_series_[0].wins_higher == 2 || cup_series_[0].wins_lower == 2))
            cup_winner_ = cup_series_[0].winner();
    }
    // after the last league day the save already holds the set-up of the play-off phase (play_playoff_day(true))
    playoffs_set_up_ = playoff_day_ == 0 && matchday_[0] > 4 * (kHokLeagueSize - 1) && !std::getenv("EM_OLD_PLAYOFFSETUP");
    if (playoff_day_ > 0 || playoffs_set_up_) {
        const int stage = std::min(std::max(playoff_day_ - 1, 0) / 5, 2);
        const int base = kPlayoffBase[stage];
        for (int pair = 0; pair < (4 >> stage); pair++) {
            const size_t slot = size_t(base + pair * 2);
            const int a = save_.state.at(kPlayoffTeams + slot), b = save_.state.at(kPlayoffTeams + slot + 1);
            Series s;
            // quarter finals by the table; later rounds by the bracket: the winner of the feeding slot (its column 0 after the
            // round) holds the seed - semi-final 1 from quarter final 1, semi-final 2 from quarter final 2, the final from semi-final 1
            // sub_3F1C9 swaps every pair every day: after days 1 and 3 the lower seed stands first, after days 2 and 4 (and
            // after the round end, which swaps a lower-seed winner back) the higher one; the finished final keeps the winner
            // first, marked with 0x14 when it was the lower seed
            const int completed = std::clamp(playoff_day_ - stage * 5, 0, 5);
            const bool a_higher = completed == 5 ? (stage < 2 || save_.state.at(kPlayoffWins + slot) != 0x14) : completed % 2 == 0;
            s.higher = a_higher ? a : b;
            s.lower = s.higher == a ? b : a;
            const int wa = std::min(int(save_.state.at(kPlayoffWins + slot)), 3);
            const int wb = std::min(int(save_.state.at(kPlayoffWins + slot + 1)), 3);
            s.wins_higher = s.higher == a ? wa : wb;
            s.wins_lower = s.higher == a ? wb : wa;
            for (int game = 0; game < wa + wb; game++) {
                const size_t o = kPlayoffScores + size_t(game * 14) + slot;
                const int ga = save_.state.at(o), gb = save_.state.at(o + 1);
                const bool a_home = (game % 2 == 0) == (s.higher == a);
                const int marker = (ga / 30 + gb / 30) * 30;
                s.games.push_back({(a_home ? ga : gb) % 30, (a_home ? gb : ga) % 30, marker >= 60, marker});
            }
            series_.push_back(s);
        }
        if (stage == 2 && !series_.empty() && (series_[0].wins_higher == 3 || series_[0].wins_lower == 3))
            champion_ = series_[0].winner();
        for (size_t group = 0; group < relegation_.size(); group++) {
            std::array<int, 4> teams{};
            for (size_t i = 0; i < teams.size(); i++) teams[i] = save_.state.at(kRelegationTeams + group * 4 + i);
            relegation_[group] = native_group(save_.state, 1, int(group), teams);
            if (playoff_day_ >= 13) {
                const auto order = relegation_[group].order();
                for (size_t i = 0; i < teams.size(); i++) relegation_upper_[group][i] = teams[size_t(order[i])];
            }
        }
    }
    if (euro_round_ >= 6) {
        std::array<int, 4> teams{};
        for (size_t i = 0; i < teams.size(); i++) teams[i] = save_.state.at(kEuroFinalTeams + i);
        euro_final_ = native_group(save_.state, 3, 0, teams);
        if (euro_round_ >= 12) euro_champion_ = euro_final_.teams[size_t(euro_final_.order()[0])];
    }
    reset_wm();
    wm_round_ = std::clamp(int(save_.state.at(kWorldRound)), 0, 7);
    const size_t national_manager = kManagerBlock + 5 * 0x1AF;
    if (save_.state.at(0x0788) == 5 && save_.state.at(national_manager + 0x14) == save_.state.at(0x068C))
        national_coach_ = save_.state.at(national_manager + 0xE4) == 0 ? 0 : -1;
    if (wm_round_ == 7) {
        bool final_played = true;
        for (size_t pair = 0; pair < 4; pair++) {
            const size_t o = kWorldResults + 16 + pair * 2;
            // a played game leaves its bytes (a level score after the quick shoot-out is possible, a 0:0 carries the
            // shoot-out marker; REAL-CODE period 30)
            final_played &= (save_.state.at(o) != 0 || save_.state.at(o + 1) != 0) && save_.state.at(o) % 30 >= save_.state.at(o + 1) % 30;
        }
        if (final_played) wm_round_ = 8;
    }
    for (size_t g = 0; g < wm_groups_.size(); g++)
        wm_groups_[g] = native_group(save_.state, 0, int(g), wm_groups_[g].teams);
    if (wm_round_ >= 5) {
        wm_slots_.resize(24);
        for (size_t i = 0; i < wm_slots_.size(); i++) {
            const int team = save_.state.at(kWorldSlots + i);
            wm_slots_[i] = team == 0xFF ? -1 : team;
        }
        if (wm_round_ >= 8) {
            wm_ranking_.assign(wm_slots_.begin() + 16, wm_slots_.end());
            wm_champion_ = wm_ranking_[0];
        }
    }
    if (is_game_checkpoint(save)) {
        const GameCheckpoint checkpoint = decode_game_checkpoint(save);
        if (checkpoint.working_state.size() != save_.state.size())
            throw FormatError("OpenEM checkpoint: missing working state");
        // The decoded models come from the current exported HOK. Their raw
        // views retain the exact synchronization phase of the running game.
        std::copy(checkpoint.working_state.begin(), checkpoint.working_state.end(), save_.state.begin());
        // save_bytes derives current league pairings in its export copy only.
        // After round44 those writes stop and the retained menu cache returns.
        // Restore that DTO from the working phase, not the exported preview.
        for(size_t league=0;league<3;++league)for(size_t pair=0;pair<6;++pair){
            const size_t at=0x8dfd+league*12+pair*2;
            save_.pairings[league][pair]={save_.state.at(at),save_.state.at(at+1)};
        }
        rng_ = GameRandom(checkpoint.game_random);
        brng_ = BorlandRandom(checkpoint.borland_random);
        rng_.borland = &brng_;
        line_counts_=checkpoint.line_counts;
        stadium_refusals_=checkpoint.stadium_refusals;
        save_history_=checkpoint.save_history;
        annual_distribution_cursors_=checkpoint.annual_distribution_cursors;
        last_scorer_slot_=checkpoint.last_scorer_slot;
        message_step_pending_=checkpoint.message_step_pending;
        table_copies_frozen_=checkpoint.table_copies_frozen;league_since_refresh_=checkpoint.league_since_refresh;
        for(int kind=0;kind<2;kind++)mgr_[0].squad.per_line[kind]=line_counts_[0][size_t(kind)];
        mgr_[0].rotation = checkpoint.club_rotation;
        national_rotation_ = checkpoint.national_rotation;
        mgr_[0].timers = checkpoint.club_timers;
        national_timers_ = checkpoint.national_timers;
        mgr_[0].injured_role = checkpoint.injured_roles;
        for (size_t slot = 0; slot < save_.squad.size(); slot++)
            save_.squad[slot].status = SquadStatus(checkpoint.squad_status.at(slot));
        seasons_ = checkpoint.seasons;
        mgr_[0].budget.team_strength = checkpoint.team_strength;
        mgr_[0].budget.players = checkpoint.players;
        mgr_[0].budget.squad_costs = checkpoint.squad_costs;
        mgr_[0].budget.pool_costs = checkpoint.pool_costs;
        for (const auto& offer : checkpoint.offers)
            mgr_[0].transfer_offers.push_back({offer.player, offer.listed, offer.bidder, offer.amount});
        mgr_[0].squad.penalty_effort_adjustment = penalty_effort_adjustment(save_, mgr_[0].team);
        configure_human_tactics(mgr_[0].squad, save_, 0);
    } else {
        restore_offers();
        if(previous){
            //5CB4:27BF ->1D1D: live return keeps loaded A, count pairs and
            //cached team fields; only59B5:26DC automatic lineup is rerun.
            inherit_native_runtime(*previous);
            auto_lineup(save_,0);
            mgr_[0].squad.penalty_effort_adjustment=penalty_effort_adjustment(save_,mgr_[0].team);
            configure_human_tactics(mgr_[0].squad,save_,0);
        }else {
            prepare_human_lineup();  // the original recomputes the club after loading (DOSBox FINEV_loaded: 321 -> 320)
            // The cold club model already uses its active pair. Persist that
            // same pair; otherwise a checkpoint would replace it with BSS0/0.
            line_counts_[0]={uint8_t(mgr_[0].squad.per_line[0]),uint8_t(mgr_[0].squad.per_line[1])};
        }
    }
    for (int m = 1; m < managers_; m++) restore_offers(m);  // the interests saved for managers 1.. (no draws)
}

Bytes Game::checkpoint_bytes() const {
    GameCheckpoint checkpoint;
    checkpoint.hok = save_bytes();
    checkpoint.working_state = save_.state;
    checkpoint.exe_fingerprint = exe_fingerprint_;
    checkpoint.game_random = rng_.state();
    checkpoint.borland_random = brng_.state();
    checkpoint.line_counts=line_counts_;
    checkpoint.stadium_refusals=stadium_refusals_;
    checkpoint.save_history=save_history_;
    checkpoint.annual_distribution_cursors=annual_distribution_cursors_;
    checkpoint.last_scorer_slot=uint8_t(last_scorer_slot_);
    checkpoint.message_step_pending=message_step_pending_;
    checkpoint.table_copies_frozen=table_copies_frozen_;checkpoint.league_since_refresh=league_since_refresh_;
    checkpoint.club_rotation = mgr_[0].rotation;
    checkpoint.national_rotation = national_rotation_;
    checkpoint.club_timers = mgr_[0].timers;
    checkpoint.national_timers = national_timers_;
    checkpoint.injured_roles = mgr_[0].injured_role;
    for (size_t slot = 0; slot < save_.squad.size(); slot++)
        checkpoint.squad_status.at(slot) = uint8_t(save_.squad[slot].status);
    checkpoint.seasons = seasons_;
    checkpoint.team_strength = mgr_[0].budget.team_strength;
    checkpoint.players = mgr_[0].budget.players;
    checkpoint.squad_costs = mgr_[0].budget.squad_costs;
    checkpoint.pool_costs = mgr_[0].budget.pool_costs;
    for (const auto& offer : mgr_[0].transfer_offers)
        checkpoint.offers.push_back({offer.player, offer.bidder, offer.listed, offer.amount});
    return encode_game_checkpoint(checkpoint);
}

int Game::manager_of_team(int team) const {
    if (team < 0) return -1;
    for (int m = 0; m < managers_; m++)
        if (mgr_[size_t(m)].team == team) return m;
    return -1;
}

const HumanSquad* Game::manager_squad(int team) const {
    const int m = manager_of_team(team);
    return m >= 0 ? &mgr_[size_t(m)].squad : nullptr;
}

// Manager 0 books into `stats`; the clubs of managers 1.. play with their squads and keep their statistics in other_stats
// (step 7 books every manager from the statistics of his match: HumanSide::match_stats, the home side's for a game of two
// managers, with the manager's side).
HumanSide Game::human_sides(MatchStats* stats, std::array<MatchStats, kMaxManagers>& other_stats) const {
    HumanSide side{mgr_[0].team, &mgr_[0].squad, stats};
    for (int m = 1; m < managers_; m++)
        side.others[size_t(m - 1)] = {mgr_[size_t(m)].team, &mgr_[size_t(m)].squad, &other_stats[size_t(m)]};
    return side;
}

int Game::team_rank(int team) const {
    for (int l = 0; l < 3; l++) {
        const League& lg = leagues_[size_t(l)];
        // the position byte of the table row (+0x31); within the season it equals the place in the table order, after the
        // season change it still holds the last season's place (REAL-CODE period 31: the gate of matchday 1)
        if (team >= lg.first_team && team < lg.first_team + kHokLeagueSize)
            return std::getenv("EM_OLD_RANK") ? int(std::find(lg.ranking.begin(), lg.ranking.end(), team - lg.first_team) - lg.ranking.begin())
                                              : lg.rows[size_t(team - lg.first_team)].position;
    }
    return 5;
}

void Game::home_game(int guest, int kind, int home_class, int guest_class, int home_rank, int guest_rank,
                     bool league_statistics, int manager) {
    AttendanceMatch m;
    m.home_class = home_class;
    m.away_class = guest_class;
    m.home_strength = attendance_strength(save_.teams[size_t(mgr_[manager].team)]);
    m.away_strength = attendance_strength(save_.teams[size_t(guest)]);
    m.home_rank = home_rank;
    m.away_rank = guest_rank;
    m.guest = guest;
    m.kind = kind;
    m.league_statistics = league_statistics;
    const uint8_t previous_home_games = uint8_t(mgr_[manager].budget.home_games);
    if (std::getenv("EM_DEBUG_GATE"))
        std::fprintf(stderr, "gate vs %d: class %d/%d strength %d/%d rank %d/%d kind %d places %d/%d price %d games %d image %d\n", guest, m.home_class, m.away_class,
                     m.home_strength, m.away_strength, m.home_rank, m.away_rank, kind, int(mgr_[manager].budget.places[0]), int(mgr_[manager].budget.places[1]),
                     int(mgr_[manager].budget.ticket_price), int(mgr_[manager].budget.home_games), mgr_[manager].image);
    const long spectators = home_attendance(finance_, mgr_[manager].budget, m, brng_);
    match_attendance_ = spectators;
    match_sold_out_ = spectators == long(mgr_[manager].budget.places[0]) + mgr_[manager].budget.places[1];
    if (league_statistics)
        save_.state.at(manager_block(manager) + 0xFE + previous_home_games) = uint8_t(spectators / 1000);
    char line[160];
    std::snprintf(line, sizeof line, "  %ld spectators against %s, %ld DM tickets", spectators,
                  save_.teams[size_t(guest)].name.c_str(), long(mgr_[manager].budget.ticket_price) * spectators);
    log_.push_back(line);
}

// Series (shown on "Totale"): DS:B57B + team * 0x15 (state 0x9988), 7 kinds x {all, home, away};
// the manager's records at DS:BABB + manager * 0x15 (state 0x9EC8). Kinds: won, lost, drawn, not
// won, not lost, without goal against, without goal scored. Checked against MONAT1's screen.
void Game::update_series(int team, int side, int goals_for, int goals_against) {
    book_series(save_.state, team, side, goals_for, goals_against);
}

// Power play and short-handed figures of manager 0 (u16 at state 0x0F44.., "Totale").
void Game::book_special_teams(const MatchStats& st, int side, int manager) {
    const int other = 1 - side;
    // goals in power play, goals against in power play, goals short-handed, goals against
    // short-handed, power plays, penalty kills (order of the screen; MONAT1: 6 0 0 11 36 39)
    const int add[6] = {st.power_play_goals[side], st.short_handed_goals[other], st.short_handed_goals[side],
                        st.power_play_goals[other], st.power_plays[side], st.penalty_kills[side]};
    for (size_t i = 0; i < 6; i++) {
        const size_t o = kSpecialTeams + size_t(manager) * 0x1AF + 2 * i;
        const int v = le16(save_.state, o) + add[i];
        save_.state[o] = uint8_t(v);
        save_.state[o + 1] = uint8_t(v >> 8);
    }
}

// ovr038:2630 (sub_60D60), after the gate: the squad bookkeeping draws one rand(0, 4) per squad slot (a hit stores the
// skill-based home line, +2 := +0x15 & 0x7F) and, for a home game, one rand(0, 15 + 4 * (30 - x)) for the stadium warning flag.
// sub_60D60 is called for every pair of the day in pairing order and runs its manager loop (0..N-1, then 5) inside, so with
// several managers the gates and bookkeepings follow the pairs, within a pair the manager index (asm ovr038:2D4B..2D64).
// sub_33CE9 then spins the game random stream rand(100, 300) times (REAL-CODE probe of the original); it is called once at
// the start of the minute loop (ovr030:2552), not per manager: spin_match_stream() after all pairs. It runs on every day with
// a game of a human club (league and cup).
void Game::manager_day(bool home_game_today, int m) {
    g_draw_scope = "manager-day";
    const size_t squad_at = squad_block(m);
    auto& squad = save_.squad_of(m);
    int slots = 0;
    for (int s = 0; s < kHokSquadSlots; s++) slots += save_.state.at(squad_at + size_t(s) * 0x34 + 0x0F) != 0;
    for (int s = 0; s < slots; s++)
        if (brng_.range(0, 4) == 0) {
            const uint8_t line = save_.state.at(squad_at + size_t(s) * 0x34 + 0x15) & 0x7F;
            save_.state.at(squad_at + size_t(s) * 0x34 + 2) = line;
            if (size_t(s) < squad.size()) squad[size_t(s)].home_line = line;  // mgr_[m].squad.squad points at this vector
        }
    const int places_low = le16(save_.state, manager_block(m) + 0x133), places_high = le16(save_.state, manager_block(m) + 0x137);
    int x = int(int16_t(uint16_t(places_high - places_low))) / 1000;
    x = std::clamp(x, 0, 30);
    if (home_game_today && brng_.range(0, 15 + 4 * (30 - x)) == 0) save_.state.at(manager_block(m) + 0xF1) |= 1;
    if (std::getenv("EM_DEBUG_MDAY")) std::fprintf(stderr, "manager_day day %d manager %d slots %d home %d B%zu\n", day_, m, slots, int(home_game_today), g_borland_log ? g_borland_log->size() : size_t(0));
}

void Game::spin_match_stream() {
    g_draw_scope = "randomize";
    const int spin = brng_.range(100, 300);
    for (int i = 0; i < spin; i++) rng_.range(0, 1);
}

void Game::play_league_day() {
    // the 28 clubs outside the leagues (36..63) do not play and are rested like before a cup day (REAL-CODE period 17:
    // energy 90 after the weekly drift)
    if (!std::getenv("EM_OLD_LEAGUE_REST"))
        for (int t = 36; t < 64; t++)
            for (auto& kind : save_.teams[size_t(t)].energy)
                for (auto& e : kind) e = 90;
    // 4F6E:161D mode0 prepares all three caches before any masked league
    // game, using each raw round even when another league is already at45.
    for(size_t league=0;league<3;++league){
        const int md=matchday_[league];if(md<1)continue;
        const auto pairs=round_robin_pairings(schedule_.teams12,md-1);
        for(size_t pair=0;pair<pairs.size();++pair){
            const int home=int(league)*12+pairs[pair].home,away=int(league)*12+pairs[pair].away;
            save_.pairings[league][pair]={home,away};
            const size_t at=0x8dfd+league*12+pair*2;
            save_.state.at(at)=uint8_t(home);save_.state.at(at+1)=uint8_t(away);
        }
    }
    // ovl06_2522: all matches of all leagues are played at once, minute by minute (the random stream is shared)
    std::vector<LeagueDay> league_days;
    std::vector<int> day_league;
    for (int l = 0; l < 3; l++) {
        const int md = matchday_[size_t(l)];
        if (md < 1 || md > 4 * (kHokLeagueSize - 1)) continue;
        league_days.push_back({&leagues_[size_t(l)], md - 1});
        day_league.push_back(l);
    }
    // ovl14: the gate of the human home game is booked before the matches are played (REAL-CODE whole-program test).
    // sub_60D60 per pair in pairing order (leagues 0..2), its manager loop inside (see manager_day): a forfeiting manager
    // (+0xF0 = 100, sub_60CC0) gets no gate and no bookkeeping, an away game no gate and no stadium flag.
    bool any_plays = false;
    for (size_t d = 0; d < league_days.size(); d++) {
        const auto pre_pairs = round_robin_pairings(schedule_.teams12, league_days[d].matchday);
        for (const auto& p : pre_pairs) {
            const int hi = league_days[d].league->first_team + p.home, ai = league_days[d].league->first_team + p.away;
            for (int m = 0; m < managers_; m++) {
                ManagerState& mg = mgr_[size_t(m)];
                if (hi != mg.team && ai != mg.team) continue;
                if (mg.squad.minimum_players > 0 && selected_match_players(mg.squad) < mg.squad.minimum_players) continue;  // forfeit: no gate
                any_plays = true;
                save_.state.at(0x784) = uint8_t(hi);  // DS:A88D: the home team of the manager game (REAL-CODE)
                bool gate_booked = false;
                if (hi == mg.team) {
                    g_draw_scope = "gate";
                    home_game(ai, 0, team_class(finance_, hi), team_class(finance_, ai), team_rank(hi), team_rank(ai), true, m);
                    gate_booked = true;
                }
                manager_day(gate_booked, m);
            }
        }
    }
    if (any_plays) spin_match_stream();
    g_draw_scope = "league-day";
    MatchStats stats;
    // the injury check of the original runs in the match minute; every chance picks scorer and assist (ovl14_2349)
    for (int m = 0; m < managers_; m++) {
        HumanSquad& sq = mgr_[size_t(m)].squad;
        sq.on_minute = [this, m](int, int load, const HokTeam& team) { injury_minute(load, &team, m); };
        sq.on_chance = [this, m](bool goal, const HokTeam& team, const std::vector<HokSquadPlayer>& working) { credit_human_goal(0, false, goal, &team, &working, m); };
        sq.on_opponent_chance = [this](bool goal, const HokTeam& team) { credit_opponent_goal(0, goal, team); };
    }
    std::vector<std::vector<std::pair<int, int>>> played;
    std::array<MatchStats, kMaxManagers> other_stats{};  // the matches of managers 1..
    const HumanSide side = human_sides(&stats, other_stats);
    const auto day_results = play_matchdays(league_days, save_.teams, schedule_, rng_, side, &brng_,
                                           match_observer_ ? &match_observer_ : nullptr, &played);
    for (int m = 0; m < managers_; m++) {
        HumanSquad& sq = mgr_[size_t(m)].squad;
        sq.on_minute = nullptr;
        sq.on_chance = nullptr;
        sq.on_opponent_chance = nullptr;
    }
    injuries_in_minutes_ = true;
    for (size_t d = 0; d < league_days.size(); d++) {
        const int l = day_league[d];
        const int md = league_days[d].matchday + 1;
        League& lg = *league_days[d].league;
        const auto pairs = round_robin_pairings(schedule_.teams12, md - 1);
        const auto& results = day_results[d];
        // the teams as actually played (pair-table error of the original, DEVIATIONS.md); the pair table keeps them (DS:5D7B)
        auto team_of = [&](size_t k, int side, int fallback) {
            return d < played.size() && k < played[d].size() ? (side == 0 ? played[d][k].first : played[d][k].second) : fallback;
        };
        for (size_t k = 0; k < pairs.size(); k++) {
            save_.state.at(0x8dfd + size_t(l) * 12 + k * 2) = uint8_t(team_of(k, 0, lg.first_team + pairs[k].home));
            save_.state.at(0x8dfd + size_t(l) * 12 + k * 2 + 1) = uint8_t(team_of(k, 1, lg.first_team + pairs[k].away));
            save_.pairings[size_t(l)][k] = {team_of(k, 0, lg.first_team + pairs[k].home), team_of(k, 1, lg.first_team + pairs[k].away)};
        }
        matchday_[size_t(l)]++;
        // ovl13 table booking (sub_5C067, twice per game): the morale byte +0x17 follows the goal difference, clamped to 47..53;
        // the original books all games of a league before the scorers of that league
        for (size_t k = 0; k < pairs.size(); k++) {
            const int hi = team_of(k, 0, lg.first_team + pairs[k].home), ai = team_of(k, 1, lg.first_team + pairs[k].away);
            const int diff = results[k].home - results[k].away;
            auto morale = [&](int team, int dd) {
                const int step = int(brng_.range(0, 2 * std::abs(dd)));
                save_.teams[size_t(team)].bonus = std::clamp(save_.teams[size_t(team)].bonus + (dd > 0 ? step : -step), 47, 53);
            };
            morale(hi, diff);
            morale(ai, -diff);
        }
        for (size_t k = 0; k < pairs.size(); k++) {
            const int hi = team_of(k, 0, lg.first_team + pairs[k].home), ai = team_of(k, 1, lg.first_team + pairs[k].away);
            const size_t result_offset = kLeagueResults + size_t(l) * 0x210 + size_t(md - 1) * 12 + k * 2;
            save_.state.at(result_offset) = uint8_t(results[k].home);
            save_.state.at(result_offset + 1) = uint8_t(results[k].away);
            update_series(hi, 0, results[k].home, results[k].away);
            update_series(ai, 1, results[k].away, results[k].home);
            update_records(hi, ai, results[k].home, results[k].away);
            // 1238:114f: scorers of the computer clubs (player records), home first
            // a team of another league in an Oberliga pair (pair-table error) finds no player of this league: no draws
            auto in_league = [&](int t) { return t >= lg.first_team && t < lg.first_team + kHokLeagueSize; };
            // a manager club scores through the picks of its match (ovl14_2349), never through the computer credit
            if (!manager_team(hi) && in_league(hi)) credit_club_scorers(save_.players, save_.state, hi, results[k].home, results[k].away, true, brng_);
            if (!manager_team(ai) && in_league(ai)) credit_club_scorers(save_.players, save_.state, ai, results[k].away, results[k].home, true, brng_);
            // the bookings of every manager of the pair, in manager index order (the order of sub_60D60; with the goals
            // credited minute by minute these bookings draw nothing)
            MatchStats* pair_stats = side.match_stats(hi, ai);
            for (int m = 0; m < managers_; m++) {
                const int own_team = mgr_[size_t(m)].team;
                if (hi != own_team && ai != own_team) continue;
                const int own_side = hi == own_team ? 0 : 1;
                const MatchStats& st = pair_stats ? *pair_stats : stats;
                record_head_to_head(hi, ai, results[k].home, results[k].away, m);
                const bool forfeited = st.forfeit_side != 0;
                if (forfeited && st.forfeit_side == own_side + 1) book_forfeit(st, m);
                if (!forfeited) {
                    book_special_teams(st, own_side, m);
                    book_match_condition(st, own_side, m);
                }
                char line[160];
                std::snprintf(line, sizeof line, "league matchday %d: %s - %s %d:%d%s", md, save_.teams[size_t(hi)].name.c_str(),
                              save_.teams[size_t(ai)].name.c_str(), results[k].home, results[k].away,
                              results[k].shootout ? " (shoot-out)" : "");
                log_.push_back(line);
                const int own = own_side == 0 ? results[k].home : results[k].away;
                if (!forfeited) book_club_match(own, own_side == 0 ? results[k].away : results[k].home, 0, true, 0, false, m);
            }
        }
        // ovl13_3855 (after all games of the league day): the manager's series records follow his club's series
        for (int m = 0; m < managers_; m++) update_series_records(save_.state, mgr_[size_t(m)].team, m);
    }
}

// A human club (the club of any manager, docs/MEHRSPIELER_PLAN.md step 6) still plays in the current cup round (an open
// series of the round, or a pair of a fresh draw).
bool Game::human_in_next_cup() const {
    if (cup_draw_.empty() || cup_winner_ >= 0) return false;
    if (cup_game_ == 0)
        return std::any_of(cup_draw_.begin(), cup_draw_.end(), [&](const HokPairing& p) { return manager_team(p.home) || manager_team(p.away); });
    return std::any_of(cup_series_.begin(), cup_series_.end(), [&](const Series& s) {
        return s.wins_higher < 2 && s.wins_lower < 2 && (manager_team(s.higher) || manager_team(s.lower));
    });
}

// A human club plays on the next play-off day: in a best-of-five series (the first eight of the first division, then
// the winners) or, on the first and third day of a round, in a relegation group (11th/12th with the 1st/2nd below).
bool Game::human_in_next_playoff() const {
    const int day = playoff_day_, stage = day / 5;
    if (stage > 2) return false;
    const bool relegation_day = day % 5 == 0 || day % 5 == 2;
    if (day == 0) {  // the groups and the quarter finals are formed from the final tables
        for (int m = 0; m < managers_; m++) {
            const int own = mgr_[size_t(m)].team;
            for (int l = 0; l < 3; l++) {
                const League& lg = leagues_[size_t(l)];
                if (own < lg.first_team || own >= lg.first_team + kHokLeagueSize) continue;
                const int pos = lg.rows[size_t(own - lg.first_team)].position;
                if ((l == 0 && pos < 8) || (l < 2 && pos >= 10) || (l > 0 && pos < 2)) return true;
                break;
            }
        }
        return false;
    }
    if (relegation_day)
        for (const GroupTable& g : relegation_)
            for (int team : g.teams) if (manager_team(team)) return true;
    if (day % 5 == 0) {  // the next round is formed from the winners of this one
        for (const Series& s : series_) if (manager_team(s.winner())) return true;
        return false;
    }
    return std::any_of(series_.begin(), series_.end(), [&](const Series& s) {
        return s.wins_higher < 3 && s.wins_lower < 3 && (manager_team(s.higher) || manager_team(s.lower));
    });
}

// A human club plays on the next EuroCup day (group phase rounds 0-5, final group 6-11).
bool Game::human_in_next_euro() const {
    if (euro_round_ >= 12) return false;
    if (euro_round_ < 6) {
        for (const auto& group : euro_groups_)
            for (int team : group.teams) if (manager_team(team)) return true;
        return false;
    }
    for (int team : euro_final_.teams) if (manager_team(team)) return true;
    return false;
}

// sub_42756 at game index 2 of the final (the third final day after a 2:0, or right after game 3): the winner's manager gets
// the round reached (+0xE6 = 6), DS:0387 / DS:038B book winner and runner-up (then the "DEB-Pokalsieger" screen), the wins
// are cleared, the game index stays 2, the grid pair keeps its places (restored after the screen) and DS:D0E4 = 1
// (REAL-CODE period 57; the reading of period 28 was a snapshot inside this routine)
void Game::close_cup_final() {
    const Series& s = cup_series_[0];
    cup_winner_ = s.winner();
    save_.state.at(kStateCupWinner) = uint8_t(cup_winner_);
    save_.state.at(kStateCupRunnerUp) = uint8_t(s.winner() == s.higher ? s.lower : s.higher);
    for (int m = 0; m < managers_; m++)
        if (cup_winner_ == mgr_[size_t(m)].team) save_.state.at(manager_block(m) + 0xE6) = uint8_t(cup_round_ + 1);
    std::fill(save_.state.begin() + long(kCupWins), save_.state.begin() + long(kCupWins + 32), uint8_t(0));
    for (int game = 0; game < 3; game++)  // no 100 mark for an unplayed game
        if (save_.state.at(kCupScores + size_t(game) * 32) == 100)
            save_.state.at(kCupScores + size_t(game) * 32) = save_.state.at(kCupScores + size_t(game) * 32 + 1) = 0;
    cup_game_ = 2;
    save_.state.at(kCupGame) = 2;
    save_.state.at(kCupPending) = 1;
    log_.push_back("DEB cup winner: " + save_.teams[size_t(cup_winner_)].name);
    store_cup_participants(save_, cup_draw_, cup_round_);
}

void Game::play_cup_day() {
    cup_day_++;
    // A final closed after two games leaves the winner twice in the cup grid; a remaining cup day of the calendar still
    // runs sub_42756, which plays that pair (winner against winner) as game 1 of a new series and books it like any cup
    // game: index 1, scores, one win, DS:D0E4 = 1 (REAL-CODE period 29)
    // (the pair "winner against winner" only exists in a save taken inside sub_42756, as in period 28's recording on the
    // "DEB-Pokalsieger" screen; a final played through in C++ never reaches it)
    const bool replay = cup_winner_ >= 0 && cup_round_ == 5 && cup_draw_.size() == 1 && cup_draw_[0].home == cup_winner_ &&
                        cup_draw_[0].away == cup_winner_ && cup_game_ < 2;
    // sub_4379C on the third final day after a 2:0 (game index 2, nothing open): no match screen, sub_42756(0) closes the final
    // (Amiga FUN_0013213a, DOS sub_42756; REAL-CODE period 57): winner and runner-up booked, wins 0, game index stays 2,
    // the grid pair is restored as read at the start, DS:D0E4 = 1
    if (cup_round_ == 5 && cup_winner_ < 0 && cup_series_.size() == 1 && cup_game_ == 2 && !std::getenv("EM_OLD_FINALCLOSE") &&
        (cup_series_[0].wins_higher == 2 || cup_series_[0].wins_lower == 2)) {
        close_cup_final();
        // the closing day still sets the match load of the managers like a cup day without them (REAL-CODE Köln period 36)
        if (!std::getenv("EM_OLD_CLOSELOAD"))
            for (int m = 0; m < managers_; m++) {
                const ManagerState& mg = mgr_[size_t(m)];
                if (mg.team >= 0 && mg.team < kHokTeams)
                    save_.state.at(manager_block(m) + 0xF0) = uint8_t(squad_match_load(save_.teams[size_t(mg.team)], mg.squad));
            }
        return;
    }
    if (cup_draw_.empty() || (cup_winner_ >= 0 && !replay)) return;
    if (cup_game_ == 0) {
        cup_series_.clear();
        for (const HokPairing& p : cup_draw_) {
            Series s;
            s.higher = p.home; s.lower = p.away;
            cup_series_.push_back(s);
        }
        // only a new draw clears the grid (sub_42B5D); the replayed final keeps the old columns
        if (!replay) std::fill(save_.state.begin() + long(kCupScores), save_.state.begin() + long(kCupWins + 32), uint8_t(0));
    }
    std::vector<HokPairing> draw;
    std::vector<size_t> active;
    for (size_t pair = 0; pair < cup_series_.size(); pair++) {
        const Series& s = cup_series_[pair];
        if (s.wins_higher == 2 || s.wins_lower == 2) continue;
        const bool first_home = s.games.size() % 2 == 0;
        draw.push_back({first_home ? s.higher : s.lower, first_home ? s.lower : s.higher});
        active.push_back(pair);
    }
    MatchStats stats;
    std::array<MatchStats, kMaxManagers> other_stats{};  // the matches of managers 1..
    const HumanSide side = human_sides(&stats, other_stats);
    const bool live = save_.state.at(0xA526) != 0;  // Einstellungen: DEB cup display
    std::array<bool, kMaxManagers> gate_before{};
    if (std::getenv("EM_DEBUG_CUP")) {
        std::fprintf(stderr, "cup day %d (game %d round %d): human %d draw:", cup_day_, cup_game_, cup_round_, mgr_[0].team);
        for (const HokPairing& p : draw) std::fprintf(stderr, " %d-%d", p.home, p.away);
        std::fprintf(stderr, "\n");
    }
    auto in_pair = [](const HokPairing& p, int team) { return p.home == team || p.away == team; };
    auto manager_in_draw = [&](int m) {
        return std::any_of(draw.begin(), draw.end(), [&](const HokPairing& p) { return in_pair(p, mgr_[size_t(m)].team); });
    };
    bool any_in_draw = false;
    for (int m = 0; m < managers_; m++) any_in_draw |= manager_in_draw(m);
    // per pair in draw order, the managers of a pair in index order, one spin after all (sub_60D60, see manager_day)
    bool any_plays = false;
    for (const HokPairing& p : draw)
        for (int m = 0; m < managers_; m++) {
            ManagerState& mg = mgr_[size_t(m)];
            if (!in_pair(p, mg.team)) continue;
            if (mg.squad.minimum_players > 0 && selected_match_players(mg.squad) < mg.squad.minimum_players) continue;
            if (live && p.home == mg.team) {  // the gate of a home game is booked before the matches, as in the league (REAL-CODE)
                g_draw_scope = "gate";
                home_game(p.away, 1, team_class(finance_, p.home), team_class(finance_, p.away), team_rank(p.home), team_rank(p.away), false, m);
                gate_before[size_t(m)] = true;
            }
            manager_day(gate_before[size_t(m)], m);
            any_plays = true;
        }
    if (any_plays) spin_match_stream();
    // a cup day on which every series is already decided plays no game: no live round, no spin (REAL-CODE period 49)
    empty_cup_day_ = draw.empty();
    // a cup day on which only other clubs play (the human series is decided) skips the evening refresh too (REAL-CODE period 81:
    // the weekly drift of the club record reaches the next day's month end, 146c refreshes only at the end of the step)
    for (int m = 0; m < managers_; m++)
        idle_cup_day_[size_t(m)] = !draw.empty() && mgr_[size_t(m)].team >= 0 && !std::getenv("EM_OLD_IDLECUP") && !manager_in_draw(m);
    if (live && (!draw.empty() || std::getenv("EM_OLD_EMPTYCUP")) && !any_in_draw) {
        // without a human club the live round still starts with the match load of the human squads (manager +0xF0,
        // REAL-CODE period 11) and the stream spin of the day (REAL-CODE period 7)
        for (int m = 0; m < managers_; m++) {
            const ManagerState& mg = mgr_[size_t(m)];
            if (mg.team >= 0 && mg.team < kHokTeams)
                save_.state.at(manager_block(m) + 0xF0) = uint8_t(squad_match_load(save_.teams[size_t(mg.team)], mg.squad));
        }
        if (!std::getenv("EM_OLD_CUP_MSG")) {  // the cup display passes the menu routine (REAL-CODE period 20)
            // but it does not sort: REAL-CODE period 91 (write watch on DS:D294) shows no swap on two cup days without the
            // human club; every earlier period stays exact without it
            if (std::getenv("EM_OLD_CUPSORT")) native_sort_messages(save_.state);
            if (std::getenv("EM_OLD_MSGSTEP")) native_message_step(save_.state);  // the count-down of this day already ran in the day loop (period 44); counting here too doubled it (period 64)
        }
        spin_match_stream();
    }
    if (live)
        for (int m = 0; m < managers_; m++) {
            HumanSquad& sq = mgr_[size_t(m)].squad;
            sq.on_minute = [this, m](int, int load, const HokTeam& team) { injury_minute(load, &team, m); };
            sq.on_chance = [this, m](bool goal, const HokTeam& team, const std::vector<HokSquadPlayer>& working) { credit_human_goal(1, false, goal, &team, &working, m); };
            sq.on_opponent_chance = [this](bool goal, const HokTeam& team) { credit_opponent_goal(1, goal, team); };
        }
    int loop_minutes = 60;
    const auto ties = live ? play_cup_round_live(save_.teams, draw, rng_, side, &brng_, match_observer_ ? &match_observer_ : nullptr, &loop_minutes) : play_cup_round(save_.teams, draw, rng_, side);
    if (live) injuries_in_minutes_ = true;  // the injury checks ran inside the minutes
    for (int m = 0; m < managers_; m++) {
        HumanSquad& sq = mgr_[size_t(m)].squad;
        sq.on_minute = nullptr;
        sq.on_chance = nullptr;
        sq.on_opponent_chance = nullptr;
    }
    for (size_t match = 0; match < ties.size(); match++) {
        const Tie& t = ties[match];
        const size_t pair = active[match];
        Series& s = cup_series_[pair];
        s.games.push_back(t.result);
        if (t.winner() == s.higher) s.wins_higher++; else s.wins_lower++;
        const size_t o = kCupScores + size_t(cup_game_) * 32 + pair * 2;
        save_.state.at(o) = uint8_t(t.result.home + t.result.decision_marker);
        save_.state.at(o + 1) = uint8_t(t.result.away);
        // ovl13_2f93 books the series in the cup mode (DS:D0C2 = 8) like in the league mode (REAL-CODE test)
        if (!live) {  // with the cup display on, the original books neither the series nor the records (REAL-CODE)
            update_series(t.home, 0, t.result.home, t.result.away);
            update_series(t.away, 1, t.result.away, t.result.home);
            update_records(t.home, t.away, t.result.home, t.result.away);  // DS:D0C2 = 8 counts too
        }
        // every manager of the tie in index order; without the cup display the goals are credited here (draws), so with
        // several managers the picks follow the ties, within a tie the manager index (assumption, docs/MEHRSPIELER_PLAN.md step 7)
        MatchStats* tie_stats = side.match_stats(t.home, t.away);
        for (int m = 0; m < managers_; m++) {
            const int own_team = mgr_[size_t(m)].team;
            if (t.home != own_team && t.away != own_team) continue;
            const int own_side = t.home == own_team ? 0 : 1;
            const MatchStats& st = tie_stats ? *tie_stats : stats;
            const bool forfeited = st.forfeit_side != 0;
            if (forfeited && st.forfeit_side == own_side + 1) book_forfeit(st, m);
            if (!forfeited) {
                book_match_condition(st, own_side, m);
                book_special_teams(st, own_side, m);
                mgr_[size_t(m)].played = true;
            }
            record_head_to_head(t.home, t.away, t.result.home, t.result.away, m);
            if (!forfeited) book_club_match(own_side == 0 ? t.result.home : t.result.away,
                            own_side == 0 ? t.result.away : t.result.home, 1, false,
                            t.result.shootout_goals[own_side], !live, m);
            char line[160];
            std::snprintf(line, sizeof line, "DEB cup: %s - %s %d:%d%s", save_.teams[size_t(t.home)].name.c_str(),
                          save_.teams[size_t(t.away)].name.c_str(), t.result.home, t.result.away,
                          t.result.shootout ? " (shoot-out)" : "");
            log_.push_back(line);
            if (std::getenv("EM_DEBUG_CUP")) std::fprintf(stderr, "%s marker %d\n", line, t.result.decision_marker);
            if (!forfeited && own_side == 0 && !gate_before[size_t(m)])
                home_game(t.away, 1, team_class(finance_, t.home), team_class(finance_, t.away), team_rank(t.home),
                          team_rank(t.away), false, m);
        }
    }
    if (live && (!draw.empty() || std::getenv("EM_OLD_EMPTYCUP"))) {  // (no game on the day: nothing of this, REAL-CODE period 49) after a cup display game (REAL-CODE): clubs outside the round are rested to 90; everybody in the round keeps the state of the minutes and of the overtime breaks (+19 at minute 61 and 66)
        for (int team = 0; team < 64; team++) {
            if (manager_team(team)) continue;
            bool in_round = false;
            for (const Tie& t : ties) in_round |= t.home == team || t.away == team;
            if (!in_round) prepare_match_energy(save_.teams[size_t(team)], nullptr);  // energy 90, no mode bits, shifts 0
        }
        int pair_home = -1;  // DS:A88D: the home team of the first manager game of the round (manager index order)
        for (int m = 0; m < managers_; m++) {
        const int own_team = mgr_[size_t(m)].team;
        auto& own_squad = save_.squad_of(m);
        const size_t own_block = squad_block(m);
        bool human_in = false;
        for (const Tie& t : ties)
            if (t.home == own_team || t.away == own_team) human_in = true;
        if (human_in) {
            int played = 0;
            int opponent = -1;
            const MatchStats* own_stats = &stats;
            for (const Tie& t : ties)
                if (t.home == own_team || t.away == own_team) {
                    opponent = t.home == own_team ? t.away : t.home;
                    if (const MatchStats* s = side.match_stats(t.home, t.away)) own_stats = s;
                }
            for (int line = 0; line < 4; line++) played += own_stats->line_minutes[0][line];
            // The round's minute loop (ovl06_2522) runs on while any pair is in overtime, and then plays the whole overtime to
            // minute 70 (cup table after 65 and 70). Its breaks before minute 61 and 66 rest every pair with +19, the managed one
            // too after its own game has ended; the breaks its own game played already came in the minutes (MatchRun::step).
            // REAL-CODE Köln periods 3, 4, 11, 21, 23 and Mannheim 6, 34, 63, 96: the former squad-day rule only matched by chance.
            const int loop_breaks = loop_minutes > 60 ? 2 : 0;
            const int own_breaks = played >= 66 ? 2 : played >= 61 ? 1 : 0;
            const int recover = 19 * std::max(0, loop_breaks - own_breaks);
            if (std::getenv("EM_DEBUG_CUP")) std::fprintf(stderr, "post-cup: loop %d played %d recover %d day %d game %d round %d next-event %d prev-event %d weekday-mod7 %d year %d\n", loop_minutes, played, recover, day_, cup_game_, cup_round_, int(calendar_.event(day_ + 1)), int(calendar_.event(day_ - 1)), day_ % 7, save_.year);
            if (recover > 0) {
                for (int team : {opponent, own_team})
                    if (team >= 0)
                        for (auto& kind : save_.teams[size_t(team)].energy)
                            for (auto& e : kind) e = uint8_t(std::min(90, e + recover));
                for (size_t slot = 0; slot < own_squad.size() && slot < size_t(kHokSquadSlots); slot++) {
                    uint8_t& f = save_.state.at(own_block + slot * kSquadRecord + 0x1B);
                    f = uint8_t(std::min(90, int(f) + recover));
                    own_squad[slot].fitness = f;
                }
            }
            if (pair_home < 0)
                pair_home = draw.empty() ? 0 : [&] { for (const HokPairing& p : draw) if (p.home == own_team || p.away == own_team) return p.home; return 0; }();
        }
        if (!human_in) {  // a round without the human club rests it like every club outside the round, squad fitness included (REAL-CODE period 7)
            HokTeam& own = save_.teams[size_t(own_team)];
            const int mode = own.flags & 0x07;  // the managed club keeps its line mode
            prepare_match_energy(own, nullptr);
            own.flags |= mode;
            for (size_t slot = 0; slot < own_squad.size() && slot < size_t(kHokSquadSlots); slot++) {
                if (save_.state.at(own_block + slot * kSquadRecord + 0x0F) == 0) continue;
                const uint8_t f = uint8_t(90);
                save_.state.at(own_block + slot * kSquadRecord + 0x1B) = f;
                own_squad[slot].fitness = f;
            }
            // the head-to-head booking still runs for the human club's (decided) pair of the round, with the pair turned for
            // the game that is not played and a 0:0 (REAL-CODE period 7)
            for (size_t pair = 0; pair < cup_series_.size() && pair < cup_draw_.size(); pair++)
                if (cup_draw_[pair].home == own_team || cup_draw_[pair].away == own_team)
                    record_head_to_head(cup_draw_[pair].home, cup_draw_[pair].away, 0, 0, m);
        }
        }
        save_.state.at(0x784) = pair_home >= 0 ? uint8_t(pair_home) : uint8_t(0xFF);
    }
    cup_game_++;
    bool open = false;
    for (size_t pair = 0; pair < cup_series_.size(); pair++) {
        const Series& s = cup_series_[pair];
        const bool finished = s.wins_higher == 2 || s.wins_lower == 2;
        open |= !finished;
        if (cup_game_ < 3) {
            // The native draw and all result columns follow the next game's home side.
            std::swap(cup_draw_[pair].home, cup_draw_[pair].away);
            for (int game = 0; game < cup_game_; game++)
                std::swap(save_.state.at(kCupScores + size_t(game) * 32 + pair * 2),
                          save_.state.at(kCupScores + size_t(game) * 32 + pair * 2 + 1));
        }
        // the original swaps the wins with the pair after every game; with the same club on both places only the game
        // parity tells the sides apart
        const bool first = s.higher == s.lower ? cup_game_ % 2 == 0 : cup_draw_[pair].home == s.higher;
        save_.state.at(kCupWins + pair * 2) = uint8_t(first ? s.wins_higher : s.wins_lower);
        save_.state.at(kCupWins + pair * 2 + 1) = uint8_t(first ? s.wins_lower : s.wins_higher);
        save_.state.at(kCupLive + pair * 2) = save_.state.at(kCupLive + pair * 2 + 1) = uint8_t(finished && !live ? 100 : 0);
        if (finished)
            for (int game = cup_game_; game < 3; game++)
                save_.state.at(kCupScores + size_t(game) * 32 + pair * 2) =
                    save_.state.at(kCupScores + size_t(game) * 32 + pair * 2 + 1) = 100;
    }
    // (EM_OLD_FINALCLOSE: the former reading of period 28, booking and closing right after a decided second game)
    const bool old_close = std::getenv("EM_OLD_FINALCLOSE") != nullptr;
    if (old_close && cup_game_ < 3 && cup_series_.size() == 1 && !open && cup_winner_ < 0) {
        const Series& s = cup_series_[0];
        cup_winner_ = s.winner();
        save_.state.at(kStateCupWinner) = uint8_t(cup_winner_);
        save_.state.at(kStateCupRunnerUp) = uint8_t(s.winner() == s.higher ? s.lower : s.higher);
        log_.push_back("DEB cup winner: " + save_.teams[size_t(cup_winner_)].name);
    }
    if (old_close && cup_winner_ >= 0 && cup_series_.size() == 1 && cup_round_ == 5 && !replay) {
        // sub_42756 closes the decided final: game index and wins back to 0, no 100 for the unplayed game, the loser's
        // place in the cup grid gets the winner (REAL-CODE period 28: 01 01 at DS:5CFB)
        cup_game_ = 0;
        save_.state.at(kCupWins) = save_.state.at(kCupWins + 1) = 0;
        for (int game = 0; game < 3; game++)
            if (save_.state.at(kCupScores + size_t(game) * 32) == 100) save_.state.at(kCupScores + size_t(game) * 32) = save_.state.at(kCupScores + size_t(game) * 32 + 1) = 0;
        save_.state.at(0x8E51) = save_.state.at(0x8E52) = uint8_t(cup_winner_);
        cup_draw_[0] = {cup_winner_, cup_winner_};  // saved into the cup grid as well
        cup_series_.clear();  // nothing is open any more (a loaded save rebuilds no series either)
        save_.state.at(kCupGame) = 0;
        save_.state.at(kCupPending) = 0;
        save_.state.at(kCupLive) = save_.state.at(kCupLive + 1) = 0;
        return;
    }
    if (cup_game_ == 3) {
        std::vector<int> winners;
        for (const Series& s : cup_series_) winners.push_back(s.winner());
        // ovl05_5646: remember the next round reached (6 denotes the cup winner).
        for (int m = 0; m < managers_; m++)
            if (std::find(winners.begin(), winners.end(), mgr_[size_t(m)].team) != winners.end())
                save_.state.at(manager_block(m) + 0xE6) = uint8_t(cup_round_ + 1);
        if (winners.size() == 1) {
            if (!old_close) {  // game 3 of the final: sub_42756 closes in the same call (see close_cup_final)
                cup_game_ = 2;
                close_cup_final();
                return;
            }
            cup_winner_ = winners[0];
            cup_game_ = 2;  // ovl05_5646 leaves the final results visible at index 2
            log_.push_back("DEB cup winner: " + save_.teams[size_t(cup_winner_)].name);
        } else {
            // the original first puts each winner on the home byte of its pair (in place); the new draw then overwrites the
            // front of the list, so the tail keeps these values (REAL-CODE period 7)
            for (size_t pair = 0; pair < cup_series_.size() && pair < 16; pair++)
                if (save_.state.at(0x8E51 + pair * 2) != uint8_t(winners[pair])) save_.state.at(0x8E51 + pair * 2) = uint8_t(winners[pair]);
            cup_draw_ = draw_cup_round(winners, brng_);
            // the draw of the final ends with two coins (REAL-CODE period 54, after the 20 swaps; with the cup display on)
            if (winners.size() == 2 && live && !std::getenv("EM_OLD_FINALCOINS")) { brng_.range(0, 1); brng_.range(0, 1); }
            cup_series_.clear();
            cup_round_++;
            save_.state.at(0x8F84) = uint8_t(cup_round_);
            cup_game_ = 0;
            std::fill(save_.state.begin() + long(kCupScores), save_.state.begin() + long(kCupWins + 32), uint8_t(0));
        }
    }
    save_.state.at(kCupGame) = uint8_t(cup_game_);
    save_.state.at(kCupPending) = uint8_t(open || cup_game_ == 0);
    // DS:07AD (native round mirror) is written only with a new draw: after the season change it stays 0 through round 1 (REAL-CODE period 33)
    store_cup_participants(save_, cup_draw_, cup_round_);
}

// 1000:3d1a on squad slot `slot` (see injuries.hpp). Recovery leaves lineup
// selection to the saved manual/automatic mode, just like 146e:1732.
void Game::injure(size_t slot, int kind, int m) {
    HokSquadPlayer& sp = save_.squad_of(m)[slot];
    uint8_t* r = save_.state.data() + squad_block(m) + slot * kSquadRecord;
    HokPlayer& pl = save_.players[size_t(sp.player)];
    if (pl.form > 20) pl.form -= brng_.range(10, 15);
    const Injury in = draw_injury(injuries_, kind, finance_.level, brng_);
    r[0x0D] = uint8_t(in.weeks);
    r[0x09] |= 2;
    r[0x17] = uint8_t(in.type);
    if (r[0x0A]) mgr_[m].injured_role[slot] = r[0x0A];
    r[0x0A] = 0;
    sp.role = 0;
    char line[160];
    if (in.weeks == kCareerOver)
        std::snprintf(line, sizeof line, "%s: %s, career over", pl.name.c_str(), injuries_.names[size_t(in.type)].c_str());
    else
        std::snprintf(line, sizeof line, "%s: %s, out for %d weeks", pl.name.c_str(),
                      injuries_.names[size_t(in.type)].c_str(), in.weeks);
    log_.push_back(line);
    const long premium = int32_t(le32(save_.state, squad_block(m) + slot * kSquadRecord + 0x30));
    if (premium != 0) {
        // 146e:0ba9, used after both training and match injuries: native
        // CL=2 left shift pays FOUR premiums, despite the manual's six.
        const long payment = int32_t(uint32_t(premium) << 2);
        mgr_[m].budget.balance += payment;
        log_.push_back(pl.name + " injury insurance payment " + std::to_string(payment / 3) + " DM");
    }
}

// 146e:1732: the summer uses healing without training injuries or training.
void Game::heal_squad(bool weekly, int m) {
    for (size_t i = 0; i < save_.squad_of(m).size() && i < size_t(kHokSquadSlots); i++) {
        uint8_t* r = save_.state.data() + squad_block(m) + i * kSquadRecord;
        if (save_.squad_of(m)[i].player < 0) continue;
        if (weekly && (r[0x09] & 2) && r[0x0D] != kCareerOver && r[0x0D] != 0) r[0x0D]--;
        if ((r[0x09] & 3) && r[0x0D] == 0) {
            r[0x09] &= 0xFC;
            if (mgr_[m].injured_role[i]) {
                // Do not put him back into a manual lineup. The normal
                // lineup preparation selects him if automatics are enabled.
                mgr_[m].injured_role[i] = 0;
                log_.push_back(save_.players[size_t(save_.squad_of(m)[i].player)].name + " is fit again");
            }
        }
    }
}

// 146e:0c89 runs on season_day % 7 == 0 or (season_day + 3) % 7 == 0.
// Phase A (transfer-list entries of manager 0) and the per-slot player mood loop (injury roll, wish to leave, mood decay,
// contract wish) of the original daily manager routine, with the messages of these events (src/game/messages.cpp;
// player byte +0x1E holds the entry index). Phase A walks the shared transfer list (block 4), not manager m's squad: the
// original runs it only in the call of manager 0 (146e:0caf: DS:A70E != 0 jumps past it, docs/MEHRSPIELER_PLAN.md step 7).
void Game::squad_injuries(bool weekly, bool no_match_today, int m) {
    uint8_t* st = save_.state.data();
    // phase A: the entries of the transfer list: exhaustion decay, ownership toggle, interest expiry
    for (int slot = 0; m == 0 && slot < kTransferListSlots; slot++) {
        uint8_t* r = st + squad_block(4) + size_t(slot) * kSquadRecord;
        if (!r[0x0F]) break;  // sub_24D56(4, 0): the occupied prefix
        if (r[0x13] > 0x38) r[0x13] = uint8_t(r[0x13] - 10);
        if ((r[0x1A] & 0x7F) == 1 && brng_.range(0, 2) == 0) r[0x1A] ^= 1;
        uint8_t* pl = st + kPlayers + size_t(r[0x0F] - 1) * kPlayerRecord;
        if (brng_.range(0, 3) == 0 && r[0x09] > 0x1F && pl[0x1E] != 0) {
            r[0x09] &= 0x1F;
            native_delete_message(save_.state, pl[0x1E]);  // ovr036:0180: the entry goes with the handle (REAL-CODE period 18)
        }
    }
    // heal_squad(weekly) runs after the loop below
    // the messages of this loop (src/game/messages.cpp), each with its date draw rand(0, 3)
    const GameDate today = date_of_season_day(day_ + 1, save_.year);
    const MessageDate message_date{today.day, today.month, today.year, day_ + 1};
    auto native_name = [](const uint8_t* pl) { std::string t; for (size_t k = 0; k < 19 && pl[k]; k++) t += char(pl[k]); return t; };
    for (size_t i = 0; i < save_.squad_of(m).size() && i < size_t(kHokSquadSlots); i++) {
        if (save_.squad_of(m)[i].player < 0) continue;
        uint8_t* r = st + squad_block(m) + i * kSquadRecord;
        if (std::getenv("EM_DEBUG_TINJ"))
            std::fprintf(stderr, "tinj B%zu day %d slot %zu 09=%02x 0D=%02x 0A=%02x 13=%02x\n", g_borland_log ? g_borland_log->size() : size_t(0), day_, i, r[0x09], r[0x0D], r[0x0A], r[0x13]);
        if (r[0x0D] == 0 && r[0x09] == 0 && training_injury(r[0x13], no_match_today, finance_.level, brng_)) {
            injure(i, 0, m);
            // seg002:0FD1..1042: "Verletzung au~erhalb des Spieles:" / "NAME  (injury)" (general, ovr036:01CA)
            const uint8_t* pl = st + kPlayers + size_t(r[0x0F] - 1) * kPlayerRecord;
            native_add_message(save_.state, brng_, message_date, MessageKind::TwoLines,
                               {MessageArg::str("Verletzung au~erhalb des Spieles:"),
                                MessageArg::str(native_name(pl) + "  (" + native_from_utf8(injuries_.names[size_t(r[0x17]) % injuries_.names.size()]) + ")")},
                               m, true);
        }
        if (day_ >= 233) continue;
        uint8_t* pl = st + kPlayers + size_t(r[0x0F] - 1) * kPlayerRecord;
        // the wish to leave: older players with a good record, checked against rand(350,450)
        int cx = 100 - (pl[0x18] == 0 ? 15 : 0);
        const int diff = int(r[0x11]) - int(r[0x10]);
        if (diff > 0) cx -= diff >> 1;
        const int limit = int(pl[0x14]) * cx / 10;
        if (brng_.range(350, 450) < limit && !(r[0x18] & 0x80) && r[0x18] < 100) {
            r[0x18] |= 0x80;
            // seg002:110C..112A: "NAME k}ndigt an, da~ er seinen Vertrag^nicht mehr verl{ngern wird." (general)
            native_add_message(save_.state, brng_, message_date, MessageKind::NoRenewal, {MessageArg::str(native_name(pl)), MessageArg::str("")}, m, true);
        }
        if (r[0x0D] != 0) r[0x0A] = 0;  // an injured player loses his role
        if (brng_.range(0, 3) == 0 && r[0x09] > 0x1F && pl[0x1E] != 0) {
            r[0x09] &= 0x1F;
            native_delete_message(save_.state, pl[0x1E]);
        }
        if (r[0x18] > 99 && !(r[0x18] & 0x80)) {
            if (brng_.range(0, 5) == 0 && pl[0x1E] != 0) {
                r[0x18] = uint8_t(brng_.range(9, 17));
                native_delete_message(save_.state, pl[0x1E]);
            }
        } else if (r[0x18] != 0 && !(r[0x18] & 0x80)) {
            r[0x18]--;
        }
        if (r[0x0C] != 0) continue;
        const int skill = (r[0x10] + r[0x11] + r[0x12]) / 3;
        int age_term = std::min(std::abs(25 - int(pl[0x14])), 8);
        age_term = 100 - age_term * 100 / 8;
        const int games = std::min(int((r[0x1C] | r[0x1D] << 8) + (r[0x1E] | r[0x1F] << 8) + (r[0x20] | r[0x21] << 8)) / 36, 5);
        int wish = (skill * 60 + age_term * 40) / 100;
        wish -= wish * games * 10 / 100;
        if (brng_.range(0, wish) == 0 && brng_.range(0, 3) == 0 && pl[0x1E] == 0 && !(r[0x18] & 0x80) && r[0x0B] == 1) {
            const int c = brng_.range(0, 100);
            const int years = c > 90 ? 4 : c > 70 ? 3 : 2;
            r[0x18] = uint8_t(years + 100);  // the new-contract wish
            // seg002:13CB..1471: "NAME bietet Ihnen an, von <years> auf <wish>^Jahre zu verl{ngern." (entry index to +0x1E)
            pl[0x1E] = uint8_t(native_add_message(save_.state, brng_, message_date, MessageKind::ContractOffer,
                                                  {MessageArg::str(native_name(pl)), MessageArg::str("von " + std::to_string(r[0x0B]) + " auf " + std::to_string(years))},
                                                  m, false, r[0x0F]));
        }
    }
    heal_squad(weekly, m);  // REAL-CODE period 12: the healing comes after the player loop (an injured player draws no training injury on his last week)
}

void Game::refresh_budget_roster(int m) {
    const ManagerBudget current = budget_from_save(save_, m);
    mgr_[m].budget.players = current.players;
    mgr_[m].budget.squad_costs = current.squad_costs;
    mgr_[m].budget.pool_costs = current.pool_costs;
}

void Game::clear_insurance_locks(int m) {
    // ovl08_1d61 clears bit 31 of the stored value at each month-end.
    for (size_t slot = 0; slot < 29; slot++)
        if (save_.state.at(squad_block(m) + slot * kSquadRecord + 0x0F))
            save_.state.at(squad_block(m) + slot * kSquadRecord + 0x2F) &= 0x7F;
}

// ovl06_364f once per minute of a match of the human club (here after the match, so a player hurt
// in it misses the next match on), ovl06_2385 picks the player.
void Game::match_injuries(int m) {
    if (selected_match_players(mgr_[m].squad) < 9) return;  // no minutes were played in a forfeit
    const int load = save_.state[manager_block(m) + 0xF0];
    for (int minute = 0; minute < 60; minute++) injury_minute(load, nullptr, m);
}

// One minute of the check (the original runs it inside the minute loop of the match, before the goal chances).
void Game::injury_minute(int load, const HokTeam* on_ice, int m) {
    int with_role = 0;
    for (const HokSquadPlayer& sp : save_.squad_of(m)) with_role += sp.player >= 0 && sp.role != 0;
    if (with_role < 5) return;
    if (!match_injury_minute(load, finance_.level, brng_)) return;
    // ovr030:23B6 (sub_45215): up to 151 picks; a pick on the ice (for a watched team, sub_5F431) is chosen when rand(0,10)
    // exceeds 3 (byte +0x11 below +0x10) or 7; a goalkeeper (position / 25 == 0, sub_12380) then only when rand(0,2) is 0
    for (int tries = 0; tries <= 150; tries++) {
        const size_t slot = size_t(brng_.range(0, int(save_.squad_of(m).size()) - 1));
        const HokSquadPlayer& sp = save_.squad_of(m)[slot];
        const uint8_t* r = save_.state.data() + squad_block(m) + slot * kSquadRecord;
        if (sp.player < 0 || sp.role == 0 || sp.line == 0 || (r[0x09] & 2)) continue;
        if (std::getenv("EM_DEBUG_INJ")) std::fprintf(stderr, "injury pick slot %zu line %d role %d lines on ice %d/%d\n", slot, sp.line, sp.role, on_ice ? on_ice->current_line[0] : -1, on_ice ? on_ice->current_line[1] : -1);
        if (on_ice && (sp.line & 0x7F) != on_ice->current_line[(sp.line >> 7) & 1] + 1) continue;  // ovr030:2444: only players on the ice
        const bool weaker = r[0x11] < r[0x10];
        if (brng_.range(0, 10) <= (weaker ? 3 : 7)) continue;
        if (std::min(2, save_.players[size_t(sp.player)].position / 25) == 0 && brng_.range(0, 2) != 0) continue;
        injure(slot, 1, m);
        if (slot < mgr_[m].hurt_in_match.size()) mgr_[m].hurt_in_match[slot] = true;
        // the original opens the line-up screen; leaving it runs sub_55DD2: the active line quotas DS:CC73 follow the
        // raw ones DS:CC43 of the manager (normalised to a sum of 8; REAL-CODE period 28; 8 bytes per manager as in tactics.cpp)
        for (size_t kind = 0; kind < 2; kind++) {
            std::array<uint8_t, 4> raw{};
            for (size_t i = 0; i < 4; i++) raw[i] = save_.state.at(0x028B + size_t(m) * 8 + kind * 4 + i);
            const auto active = normalize_tactics_quotas(raw);
            for (size_t i = 0; i < 4; i++) save_.state.at(0x025B + size_t(m) * 8 + kind * 4 + i) = active[i];
        }
        break;
    }
}

HokPlayer* Game::player_by_exe_index(int exe_index) {
    const uint8_t* raw = save_.state.data() + kPlayers + size_t(exe_index - 1) * kPlayerRecord;
    for (HokPlayer& pl : save_.players)
        if (pl.raw == raw) return &pl;
    return nullptr;
}

// ovl09_25d6 with force: include the end record in the shift, then clear
// only its player ID. Other opaque bytes of the native end record survive.
void Game::remove_squad_record(int block, int slot, int end_slot) {
    uint8_t* base = save_.state.data() + kSquad + size_t(block) * 0x618;
    std::memmove(base + size_t(slot) * kSquadRecord, base + size_t(slot + 1) * kSquadRecord,
                 size_t(end_slot - slot) * kSquadRecord);
    base[size_t(end_slot) * kSquadRecord + 0x0F] = 0;
}

// The offer part of 146e:0c89 (transfers.hpp): first the players manager 0 has listed (block 4,
// bit 7), then the rest of his squad (bit 6, until season day 232). 1000:3bb8 is called with the
// slot number where it reads a player record, so the strength it matches belongs to player
// record "slot" of DS:8253, not to the offered player - kept as in the original.
void Game::make_offers(int m) {
    uint8_t* st = save_.state.data();
    auto pending = [&](const HokPlayer* pl) {
        for (const ManagerOffer& o : mgr_[m].transfer_offers)
            if (o.player == int(pl - save_.players.data())) return true;
        return false;
    };
    auto strength_of_record = [&](int exe_index) {  // DS:8253 + index * 0x21, +0x15..+0x17
        const uint8_t* p = st + kPlayers + size_t(exe_index - 1) * kPlayerRecord;
        return (p[0x15] + p[0x16] + p[0x17]) / 3;
    };
    auto offer = [&](int block, int slot, bool listed) {
        uint8_t* r = st + squad_block(block) + size_t(slot) * kSquadRecord;
        HokPlayer* pl = player_by_exe_index(r[0x0F]);
        if (std::getenv("EM_DEBUG_OFFER"))
            std::fprintf(stderr, "offer B%zu day %d block %d slot %d 09=%02x pending %d\n", g_borland_log ? g_borland_log->size() : size_t(0), day_, block, slot, r[0x09], pl ? int(pending(pl)) : -1);
        if (!pl) return;
        const long value = player_worth(contracts_, contract_player(save_, block, slot), Worth::Value, brng_);
        const long v = value / 10000;
        // seg002:0E36 / 1537: the draws come first; a player whose record already carries a message (+0x1E, the
        // interest of an earlier offer) is skipped only afterwards (REAL-CODE period 17)
        if (listed) {
            if (brng_.range(0, 180) >= v || brng_.range(0, 2) == 0) return;
        } else {
            if (brng_.range(0, 200) >= v || brng_.range(0, 450) != 0) return;
        }
        if (st[kPlayers + size_t(r[0x0F] - 1) * kPlayerRecord + 0x1E] != 0) return;
        if (r[0x09] & 0xC0) return;  // a saved interest must not be overwritten by a new bidder
        if (pending(pl)) return;
        r[0x09] |= listed ? 0x80 : 0x40;
        const bool foreign = !listed && v > 90 && brng_.range(0, 10) == 0;
        const int bidder = pick_bidder(save_.teams, strength_of_record(slot), foreign, brng_);
        r[0x16] = uint8_t(bidder);
        {  // seg002:0E92..0EE2: "TEAM ist an SPIELER^interessiert.^" (ovr036:0206, its date draw rand(0, 3)); the
            // entry index goes to the player record +0x1E
            const GameDate d = date_of_season_day(day_ + 1, save_.year);
            auto cstr = [&](size_t at, size_t n) { std::string t; for (size_t k = 0; k < n && st[at + k]; k++) t += char(st[at + k]); return t; };
            const size_t record = kPlayers + size_t(r[0x0F] - 1) * kPlayerRecord;
            const int handle = native_add_message(save_.state, brng_, {d.day, d.month, d.year, day_ + 1}, MessageKind::Interest,
                {MessageArg::str(cstr(0x17E7 + size_t(bidder) * 61, 22)), MessageArg::str(cstr(record, 20))}, m, false, r[0x0F]);
            st[record + 0x1E] = uint8_t(handle);
        }
        ManagerOffer o{int(pl - save_.players.data()), listed, bidder, 0};
        // Native interest creation has no sale-price draw; price begins on click.
        mgr_[m].transfer_offers.push_back(o);
        char line[160];
        std::snprintf(line,sizeof line,"transfer interest: %s for %s",save_.teams[size_t(bidder)].name.c_str(),pl->name.c_str());
        log_.push_back(line);
    };
    for (int slot = 0; slot < kTransferListSlots; slot++) {
        const uint8_t* r = st + squad_block(4) + size_t(slot) * kSquadRecord;
        if (!r[0x0F] || st[kPlayers + size_t(r[0x0F] - 1) * kPlayerRecord + 0x1A] != m) continue;  // the manager's (player +0x1A = manager)
        if (r[0x18] & 0x80) continue;
        offer(4, slot, true);
    }
    if (day_ >= 233) return;
    for (size_t slot = 0; slot < save_.squad_of(m).size() && slot < size_t(kHokSquadSlots); slot++) {
        const uint8_t* r = st + squad_block(m) + slot * kSquadRecord;
        if (std::getenv("EM_DEBUG_OFFER") && (!r[0x0F] || r[0x0C] != 0 || (r[0x18] & 0x80)))
            std::fprintf(stderr, "offer skip day %d slot %zu 0F=%02x 0C=%02x 18=%02x\n", day_, slot, r[0x0F], r[0x0C], r[0x18]);
        if (!r[0x0F] || r[0x0C] != 0 || (r[0x18] & 0x80)) continue;
        offer(m, int(slot), false);
    }
}

void Game::restore_offers(int m) {
    // HOK retains interest and bidder only. Import performs no price valuation
    // or random draws; the native price belongs to a later row-click modal.
    auto restore = [&](int block, int slots, uint8_t flag, bool listed) {
        for (int slot = 0; slot < slots; slot++) {
            const size_t at = squad_block(block) + size_t(slot) * kSquadRecord;
            if (!(save_.state.at(at + 9) & flag) || !save_.state.at(at + 15)) continue;
            HokPlayer* player = player_by_exe_index(save_.state.at(at + 15));
            const int bidder = save_.state.at(at + 0x16);
            if (!player || player->raw[0x1A] != m || bidder >= int(save_.teams.size()) || bidder == mgr_[m].team) continue;
            const int index = int(player - save_.players.data());
            if (std::any_of(mgr_[m].transfer_offers.begin(), mgr_[m].transfer_offers.end(), [&](const auto& o) { return o.player == index; })) continue;
            mgr_[m].transfer_offers.push_back({index, listed, bidder, 0});
        }
    };
    restore(4, kTransferListSlots, 0x80, true);
    restore(m, kHokSquadSlots, 0x40, false);
}

// The contract dialog of ovl02_0000 for manager 0, after the contract years went down: every
// squad player at 0 years is asked for a new contract (ovl11_41db; demand as renewal_demand after
// season day 232). If the manager offers nothing or the player refuses (player_accepts, offer =
// demand + 10 %), he leaves: the club gets half his market value (ovl11_3d36(record, 0) / 2,
// added to DS:97EA), his record goes (ovl09_25d6) and he is a free player (+0x1A = 6, no team)
// for ovl16_0480. A player who accepts gets the years and the salary.
void Game::expire_contracts(int m) {
    contract_outcomes_.clear();
    constexpr int kMatchdays = 44;
    for (size_t slot = 0; slot < save_.squad_of(m).size();) {
        uint8_t* r = save_.state.data() + squad_block(m) + slot * kSquadRecord;
        if (r[0x0B] != 0 || !r[0x0F]) {
            slot++;
            continue;
        }
        const ContractPlayer cp = contract_player(save_, m, int(slot));
        HokSquadPlayer& sp = save_.squad_of(m)[slot];
        HokPlayer& pl = save_.players[size_t(sp.player)];
        if (!std::getenv("EM_OLD_CONTRACTS")) {
            // ovl02_0000 (ovr026:0DC5..0E71, Amiga FUN_00133378) proposes 1 year at ovl11_3d36(record, 1) * (147 - age) / 100
            // (no random draw); ovl11_41db shows it. "Mach' ich!" on the unchanged proposal takes it without the acceptance
            // roll (ovr035:4C3C) and drops a pending message of the player (ovl12_0180). REAL-CODE period 58: 25 of 25.
            const long proposal = long(int32_t(player_worth(contracts_, cp, Worth::Salary, brng_) * (147 - int(pl.age)) / 100));
            ContractTerms terms;
            if (contract_terms_decider_) terms = contract_terms_decider_(cp, int(slot), proposal);
            else terms.years = contract_decider_ ? contract_decider_(cp, proposal) : 1;  // the default takes the proposal
            // The insurance line of ovl11_41db writes the premium (+0x30, ovl11_3d36(record, 1) / 2) at once and marks it
            // (+0x2F bit 7) when the player signs; the native lock (bit 7 or flags & 3 == 2) keeps it as it is.
            const bool was_insured = le32(save_.state, size_t(r - save_.state.data()) + 0x30) != 0;
            const bool insurance = contract_terms_decider_ && terms.years > 0 && terms.insured != was_insured &&
                                   !(r[0x2F] & 0x80) && (r[0x09] & 3) != 2;
            auto change_insurance = [&] {
                if (!insurance) return;
                const long premium = terms.insured ? player_worth(contracts_, cp, Worth::Salary, brng_) / 2 : 0;
                for (int b = 0; b < 4; b++) r[0x30 + b] = uint8_t(uint32_t(premium) >> (8 * b));
            };
            auto signed_on = [&](int years, long salary, const std::string& text) {
                r[0x0B] = uint8_t(years);
                r[0x18] = 0;
                for (int b = 0; b < 4; b++) r[0x28 + b] = uint8_t(salary >> (8 * b));
                if (insurance && terms.insured && le32(save_.state, size_t(r - save_.state.data()) + 0x30)) r[0x2F] |= 0x80;
                sp.contract_years = years;
                sp.salary = salary / 3;
                log_.push_back(pl.name + text + std::to_string(salary / 3) + " DM");
                contract_outcomes_.push_back({pl.name, true, false, 0});
                slot++;
            };
            if (terms.years == 1 && (terms.salary == 0 || terms.salary == proposal)) {
                change_insurance();
                uint8_t* praw = save_.state.data() + kPlayers + size_t(r[0x0F] - 1) * kPlayerRecord;
                if (praw[0x1E] >= 1 && praw[0x1E] <= 24) native_delete_message(save_.state, praw[0x1E]);
                signed_on(1, proposal, " stays for 1 year at ");
                continue;
            }
            bool refused = false, too_long = false;
            if (contract_terms_decider_ && terms.years >= 1) {
                // ovl11_41db as the season change calls it (ovr035:4A6D..49F2, 4AD8..4D7D): a new number of years asks
                // season_change_demand at once, writes it into the record (+0x0B, +0x28) and "Mach' ich!" takes it without
                // the acceptance roll; a typed salary is rolled for (ovl11_3a3b) unless it reaches the record's salary.
                // More than four years: "SOOOO lange..." and the player goes. A refusal draws the wish counter.
                long record_salary = proposal, offer = proposal;
                if (terms.years != 1) record_salary = offer = std::max(season_change_demand(contracts_, save_, m, cp, terms.years, daily_luck_, brng_), proposal);
                bool roll = false;
                if (terms.salary) { offer = terms.salary; roll = offer < record_salary; }
                change_insurance();
                if (terms.years > 4) too_long = true;
                else if (!roll || player_accepts(contracts_, cp, terms.years, offer, kMatchdays, kMatchdays, 233, brng_)) {
                    uint8_t* praw = save_.state.data() + kPlayers + size_t(r[0x0F] - 1) * kPlayerRecord;
                    if (praw[0x1E] >= 1 && praw[0x1E] <= 24) native_delete_message(save_.state, praw[0x1E]);
                    signed_on(terms.years, offer, " signs for " + std::to_string(terms.years) + " years at ");
                    continue;
                } else refused = true;
                r[0x18] = uint8_t(brng_.range(10, 18));  // the wish counter starts again
            } else if (terms.years >= 1) {  // the old decider: other years, the player decides (player_accepts, ovl11_3a3b)
                long offer = terms.salary;
                if (terms.years != 1) {
                    const long demand = renewal_demand(contracts_, cp, terms.years, 100, kMatchdays, kMatchdays, 233, brng_);
                    if (!offer) offer = demand;
                }
                if (player_accepts(contracts_, cp, terms.years, offer, kMatchdays, kMatchdays, 233, brng_)) {
                    signed_on(terms.years, offer, " signs for " + std::to_string(terms.years) + " years at ");
                    continue;
                }
                r[0x18] = uint8_t(brng_.range(10, 18));  // a refusal: the wish counter starts again
                refused = true;
            }
            if (contract_terms_decider_) {  // ovl12_0180 at the end of ovl11_41db: the player's pending message goes
                uint8_t* praw = save_.state.data() + kPlayers + size_t(r[0x0F] - 1) * kPlayerRecord;
                if (praw[0x1E] >= 1 && praw[0x1E] <= 24) native_delete_message(save_.state, praw[0x1E]);
            }
            contract_outcomes_.push_back({pl.name, false, refused, 0, too_long});
        }
        const long demand = std::getenv("EM_OLD_CONTRACTS") ? renewal_demand(contracts_, cp, 2, 100, kMatchdays, kMatchdays, 233, brng_) : 0;
        const int years = std::getenv("EM_OLD_CONTRACTS") ? (contract_decider_ ? contract_decider_(cp, demand) : 2) : 0;
        const long offer = demand + demand / 10;
        if (years > 0 && player_accepts(contracts_, cp, years, offer, kMatchdays, kMatchdays, 233, brng_)) {
            r[0x0B] = uint8_t(years);
            for (int b = 0; b < 4; b++) r[0x28 + b] = uint8_t(offer >> (8 * b));
            sp.contract_years = years;
            sp.salary = offer / 3;
            log_.push_back(pl.name + " signs for " + std::to_string(years) + " years at " + std::to_string(offer / 3) + " DM");
            slot++;
            continue;
        }
        const long fee = player_worth(contracts_, cp, Worth::Value, brng_) / 2;
        mgr_[m].budget.balance += fee;
        if (!contract_outcomes_.empty() && !contract_outcomes_.back().signed_on) contract_outcomes_.back().fee = fee;
        const int exe_index = r[0x0F];
        remove_squad_record(m, int(slot), kHokSquadSlots - 1);
        save_.squad_of(m).erase(save_.squad_of(m).begin() + long(slot));
        std::move(mgr_[m].injured_role.begin() + long(slot) + 1, mgr_[m].injured_role.end(), mgr_[m].injured_role.begin() + long(slot));
        mgr_[m].injured_role.back() = 0;
        save_.state[kPlayers + size_t(exe_index - 1) * kPlayerRecord + 0x1A] = 6;
        save_.state[kPlayers + size_t(exe_index - 1) * kPlayerRecord + 0x1D] = 0xFF;
        pl.team = -1;
        log_.push_back(pl.name + " leaves the club, " + std::to_string(fee / 3) + " DM transfer fee");
    }
}

bool Game::begin_sale_offer(int block,int slot,int16_t native_si,SaleQuote& quote){
    if(national_coach_>=0)return false;
    if(block!=0&&block!=4)return false;
    const int a=active_manager_;
    return begin_native_sale(save_,contracts_,brng_,block==4?4:a,slot,native_si,quote,a);
}
bool Game::finish_sale_offer(const SaleQuote& quote,bool sell){
    const int a=quote.manager;
    if(a<0||a>=managers_)return false;
    if(!current_native_sale(save_,quote))return false;
    const size_t cash=manager_block(a)+0x18f;
    if(sell)for(size_t byte=0;byte<4;++byte)save_.state.at(cash+byte)=uint8_t(uint32_t(mgr_[a].budget.balance)>>(8*byte));
    int buyer=-1;
    if(!finish_native_sale(save_,brng_,quote,sell,&buyer))return false;
    if(sell){
        HokPlayer* player=player_by_exe_index(quote.id);
        if(!player)throw FormatError("Sale: missing global player");
        player->team=player->raw[0x1d]==255?-1:player->raw[0x1d];
        mgr_[a].budget.balance=int32_t(le32(save_.state,cash));
        if(quote.block==a){
            std::move(mgr_[a].injured_role.begin()+quote.slot+1,mgr_[a].injured_role.end(),mgr_[a].injured_role.begin()+quote.slot);
            mgr_[a].injured_role.back()=0;
        }
        save_.squad_of(a)=hok_squad(save_,a);
        if(buyer>=0&&buyer<int(save_.teams.size())){
            auto& team=save_.teams[size_t(buyer)];
            for(int kind=0;kind<2;++kind){team.skill1[kind]=team.raw[0x18+kind];team.skill2[kind]=team.raw[0x1a+kind];}
        }
        refresh_budget_roster(a);
        log_.push_back(player->name+" sold for "+std::to_string(quote.amounts.net/3)+" DM");
    }
    mgr_[a].transfer_offers.clear();restore_offers(a);return true;
}
bool Game::accept_offer(size_t index) {
    const int a=active_manager_;
    if(index>=mgr_[a].transfer_offers.size())return false;
    const auto offer=mgr_[a].transfer_offers[index];const auto& player=save_.players.at(size_t(offer.player));
    const int id=int((player.raw-save_.state.data()-kPlayers)/kPlayerRecord)+1;
    const int block=offer.listed?4:a;
    for(int slot=0;slot<(offer.listed?11:29);++slot){
        if(save_.state.at(squad_block(block)+size_t(slot)*52+15)!=id)continue;
        SaleQuote quote;
        return begin_sale_offer(offer.listed?4:0,slot,0,quote)&&finish_sale_offer(quote,true);
    }
    return false;
}

PurchaseResult Game::buy_transfer_player(size_t slot, long fee, int years, long salary, bool accept_subsidy) {
    if (years < 1 || years > 4 || salary <= 0) return PurchaseResult::Invalid;
    PurchaseDraft draft;
    const auto result = begin_transfer_purchase(slot, fee, draft);
    if (result != PurchaseResult::Negotiating) return result;
    return finish_transfer_purchase(draft, years, salary, accept_subsidy);
}

bool Game::set_player_insurance(size_t slot, bool enabled) {
    const int a = active_manager_;
    if (slot >= 29 || slot >= save_.squad_of(a).size() || national_coach_ >= 0 || mgr_[a].team >= kFirstNationalTeam)
        return false;
    const size_t at = squad_block(a) + slot * kSquadRecord;
    const int id = save_.state.at(at + 0x0F);
    const HokPlayer* player = id ? player_by_exe_index(id) : nullptr;
    if (!player || player->raw[0x1A] != a) return false;
    if (save_.state.at(at + 0x0C) != 0 || (save_.state.at(at + 0x18) & 0x80)) return false;
    const bool insured = le32(save_.state, at + 0x30) != 0;
    if (insured == enabled) return true;
    // Preserve ovl11_41db's exact low-two-bit test, including its native
    // flags==3 exception, and the one-month lock in the stored value.
    if ((save_.state.at(at + 0x2F) & 0x80) || (save_.state.at(at + 0x09) & 3) == 2) return false;
    const long premium = enabled ? player_worth(contracts_, contract_player(save_, a, int(slot)),
                                                Worth::Salary, brng_) / 2 : 0;
    for (int byte = 0; byte < 4; byte++) save_.state.at(at + 0x30 + size_t(byte)) = uint8_t(uint32_t(premium) >> (8 * byte));
    if (enabled && premium != 0) save_.state.at(at + 0x2F) |= 0x80;
    refresh_budget_roster(a);
    log_.push_back(player->name + (enabled ? " injury insurance enabled at " : " injury insurance cancelled at ") +
                   std::to_string(premium / 3) + " DM per month");
    return true;
}

TransferMoveResult Game::list_transfer_player(size_t slot,bool prepare_lineup) { return move_owned_player(slot, true,prepare_lineup); }
TransferMoveResult Game::recall_transfer_player(size_t slot,bool prepare_lineup) { return move_owned_player(slot, false,prepare_lineup); }
void Game::finish_transfer_menu(){prepare_human_lineup(false,active_manager_);refresh_budget_roster(active_manager_);}

bool Game::set_training_plan(const TrainingPlan& plan) {
    const int a = active_manager_;
    if (national_coach_ >= 0 || mgr_[a].team >= kFirstNationalTeam) return false;
    for (const auto& day : plan) for (uint8_t kind : day) if (kind >= kTrainingKinds) return false;
    // The plan (state 0x070C) is one field so far; whether the original keeps one per manager is open
    // (docs/MEHRSPIELER_PLAN.md). The recovery fee goes to the active manager.
    plan_ = plan;
    for (size_t day = 0; day < plan.size(); day++)
        for (size_t unit = 0; unit < plan[day].size(); unit++) save_.state.at(0x070C + day * 4 + unit) = plan[day][unit];
    mgr_[a].budget.open_97ee = training_recovery_cost(training_, plan_, mgr_[a].budget.league);
    for (int byte = 0; byte < 4; byte++)
        save_.state.at(manager_block(a) + 0x193 + size_t(byte)) = uint8_t(uint32_t(mgr_[a].budget.open_97ee) >> (8 * byte));
    log_.push_back("training plan saved, recovery " + std::to_string(mgr_[a].budget.open_97ee * 2 / 3) + " DM per week");
    return true;
}

StockResult Game::trade_shares(size_t company,uint32_t quantity,bool sell) {
    const int a=active_manager_;
    if(company>=kHokShares||quantity==0||quantity==UINT32_MAX)return StockResult::Invalid;
    if(national_coach_>=0||mgr_[a].team>=kFirstNationalTeam)return StockResult::Unavailable;
    // The holdings (state 0x30) are one table so far; whether the original keeps them per manager is open.
    const size_t holding=0x30+company*8;
    const uint32_t old_quantity=le32(save_.state,holding+4),old_cost=le32(save_.state,holding);
    const uint32_t price=le16(save_.state,0xB0+company*72+70);
    if(!price)return StockResult::Invalid;
    if(sell){if(quantity>old_quantity)return StockResult::InsufficientShares;}
    else if(mgr_[a].budget.balance<=0||quantity>uint32_t(mgr_[a].budget.balance)/price)return StockResult::InsufficientCash;
    const uint32_t gross=quantity*price;
    const uint32_t fee=(gross*15u)/1000u;
    uint32_t next_quantity,next_cost;
    if(sell){
        next_quantity=old_quantity-quantity;
        const uint32_t remaining_percent=(next_quantity*100u)/old_quantity;
        next_cost=(old_cost*remaining_percent)/100u;
        mgr_[a].budget.balance=int32_t(uint32_t(mgr_[a].budget.balance)+gross-fee);
    }else{
        next_quantity=old_quantity+quantity;next_cost=old_cost+gross+fee;
        // The native buy limit reserves no commission, so a maximum purchase
        // can overdraw the account by its fee. Preserve that behavior.
        mgr_[a].budget.balance=int32_t(uint32_t(mgr_[a].budget.balance)-gross-fee);
    }
    const auto store=[&](size_t at,uint32_t value){for(int byte=0;byte<4;byte++)save_.state.at(at+size_t(byte))=uint8_t(value>>(8*byte));};
    store(holding,next_cost);store(holding+4,next_quantity);
    store(manager_block(a)+0x18F,uint32_t(mgr_[a].budget.balance));
    save_.finance.shares[company]=next_quantity;save_.finance.share_cost[company]=int32_t(next_cost)/3;
    if(a==0)save_.finance.balance=mgr_[0].budget.balance/3;  // HokFile::finance shows manager 0
    log_.push_back(std::string(sell?"sold ":"bought ")+std::to_string(quantity)+" shares of company "+std::to_string(company));
    return StockResult::Traded;
}

ManualLineupResult Game::change_lineup(size_t slot,int selected_line,ManualLineupAction action,const bool* six_override,
                                     const std::array<std::array<int,6>,5>* cells_override){
    const int a=active_manager_;
    if(national_coach_>=0||mgr_[a].team>=kFirstNationalTeam)throw FormatError("Aufstellung ist in dieser Phase nicht verfügbar.");
    const auto result=manual_lineup(save_,a,slot,selected_line,action,six_override,cells_override);
    if(result.changed){
        refresh_human_team(a);mgr_[a].squad.penalty_effort_adjustment=penalty_effort_adjustment(save_,mgr_[a].team);
    }
    return result;
}

bool Game::set_tactics(const TacticsPlan& plan){
    const int a=active_manager_;
    if(national_coach_>=0||mgr_[a].team>=kFirstNationalTeam)return false;
    commit_tactics(save_,plan,a);
    configure_human_tactics(mgr_[a].squad,save_,a);refresh_human_team(a);
    mgr_[a].squad.penalty_effort_adjustment=penalty_effort_adjustment(save_,mgr_[a].team);
    return true;
}

BankResult Game::bank_transfer(long amount) {
    const int a = active_manager_;
    if (amount == 0) return BankResult::InvalidAmount;
    if (national_coach_ >= 0 || mgr_[a].team >= kFirstNationalTeam) return BankResult::Unavailable;
    if (amount > 0 && amount > mgr_[a].budget.balance) return BankResult::InsufficientCash;
    // the debt matrix: manager block +0x19B + 4 * party; the bank is party 4, its row holds the claims on manager a
    const size_t bank_manager = manager_block(4), own = manager_block(a);
    const size_t bank_claim = bank_manager + 0x19B + 4 * size_t(a);
    const int32_t bank_counter = int32_t(le32(save_.state, bank_claim));
    const int64_t available = std::max(int64_t(0), std::max(int64_t(mgr_[a].budget.bank),
                               int64_t(6000000) - std::max(int64_t(0), int64_t(bank_counter))));
    if (amount < 0 && -int64_t(amount) > available) return BankResult::CreditLimit;
    // Native four-sided posting, with defined 32-bit wrap just like the two
    // word ADD/ADC and SUB/SBB operations in ovl15_15d7.
    mgr_[a].budget.balance = int32_t(uint32_t(mgr_[a].budget.balance) - uint32_t(amount));
    mgr_[a].budget.bank = int32_t(uint32_t(mgr_[a].budget.bank) + uint32_t(amount));
    const uint32_t bank_cash = le32(save_.state, bank_manager + 0x18F) + uint32_t(amount);
    auto store = [&](size_t at, uint32_t value) {
        for (int byte = 0; byte < 4; byte++) save_.state.at(at + size_t(byte)) = uint8_t(value >> (8 * byte));
    };
    store(own + 0x18F, uint32_t(mgr_[a].budget.balance));
    store(own + 0x19B + 4 * 4, uint32_t(mgr_[a].budget.bank));
    store(bank_manager + 0x18F, bank_cash);
    store(bank_claim, uint32_t(bank_counter) - uint32_t(amount));
    log_.push_back("bank " + std::string(amount > 0 ? "deposit/repayment " : "withdrawal/loan ") +
                   std::to_string((amount > 0 ? int64_t(amount) : -int64_t(amount)) / 3) + " DM");
    return BankResult::Transferred;
}

TransferMoveResult Game::move_owned_player(size_t slot, bool to_market,bool prepare_lineup) {
    constexpr int club_end = 29, pool_end = kTransferListSlots - 1;
    if (slot >= size_t(to_market ? club_end : kTransferListSlots)) return TransferMoveResult::Invalid;
    const int a = active_manager_;
    if (national_coach_ >= 0 || mgr_[a].team >= kFirstNationalTeam) return TransferMoveResult::Unavailable;
    const size_t club = squad_block(a);
    const size_t source_block = to_market ? club : squad_block(4);
    const size_t target_block = to_market ? squad_block(4) : club;
    const size_t source = source_block + slot * kSquadRecord;
    const int id = save_.state.at(source + 15);
    const HokPlayer* player = id ? player_by_exe_index(id) : nullptr;
    if (!player || player->raw[0x1A] != a || player->team != mgr_[a].team)
        return TransferMoveResult::Unavailable;
    if ((save_.state.at(source + 0x18) & 0x80) ||
        (save_.state.at(source + 9) & (to_market ? 0x40 : 0x80))) return TransferMoveResult::Blocked;
    if (to_market) {
        int own = 0;
        for (size_t at = 0; at < 12; at++) {
            const int other_id = save_.state.at(target_block + at * kSquadRecord + 15);
            const HokPlayer* other = other_id ? player_by_exe_index(other_id) : nullptr;
            own += other && other->raw[0x1A] == a;
        }
        if (own >= 3) return TransferMoveResult::OwnLimit;
        if (save_.state.at(target_block + size_t(pool_end) * kSquadRecord + 15)) return TransferMoveResult::PoolFull;
    } else {
        int count = 0;
        for (int at = 0; at < club_end; at++) count += save_.state.at(target_block + size_t(at) * kSquadRecord + 15) != 0;
        if (count >= club_end) return TransferMoveResult::SquadFull;
    }
    const HokFile current = decode_hok(save_bytes());
    // Synchronize only the club records, preserving unrelated raw working state.
    std::copy_n(current.state.begin() + long(club), 30 * kSquadRecord, save_.state.begin() + long(club));
    std::array<uint8_t, kSquadRecord> incoming;
    std::copy_n(current.state.begin() + long(source), kSquadRecord, incoming.begin());
    if (to_market) incoming[0x0A] = 0;
    // ovl09_2cd1 generates a temporary profile before the existing full record
    // is copied over it. Its five random draws still belong to the career.
    (void)brng_.range(80, 120); (void)brng_.range(35, 65);
    (void)brng_.range(1, 13); (void)brng_.range(1, 3); (void)brng_.range(0, 1);
    const int end = to_market ? pool_end : club_end;
    const int natural = std::min(2, player->position / 25);
    int insert = 0;
    while (insert < end) {
        const int other_id = save_.state.at(target_block + size_t(insert) * kSquadRecord + 15);
        const HokPlayer* other = other_id ? player_by_exe_index(other_id) : nullptr;
        if (!other || std::min(2, other->position / 25) > natural) break;
        insert++;
    }
    uint8_t* destination = save_.state.data() + target_block + size_t(insert) * kSquadRecord;
    if (destination[15] && insert < end)
        std::memmove(destination + kSquadRecord, destination, size_t(end - insert) * kSquadRecord);
    std::copy(incoming.begin(), incoming.end(), destination);
    remove_squad_record(to_market ? a : 4, int(slot), to_market ? club_end : pool_end);
    auto& injured_role = mgr_[a].injured_role;
    if (to_market) {
        std::move(injured_role.begin() + long(slot) + 1, injured_role.end(), injured_role.begin() + long(slot));
        injured_role.back() = 0;
    } else {
        std::move_backward(injured_role.begin() + insert, injured_role.end() - 1, injured_role.end());
        injured_role[size_t(insert)] = 0;
    }
    save_.squad_of(a) = hok_squad(save_, a);
    if(prepare_lineup)prepare_human_lineup(false, a);
    refresh_budget_roster(a);
    log_.push_back(player->name + (to_market ? " listed for transfer" : " returns from transfer list"));
    return TransferMoveResult::Moved;
}

ContractResult Game::negotiate_contract(size_t slot, int years, long salary, bool allow_shortening) {
    if (slot >= 29 || years < 1 || years > 4 || salary <= 0) return ContractResult::Invalid;
    const int a = active_manager_;
    if (slot >= save_.squad_of(a).size() || national_coach_ >= 0 || mgr_[a].team >= kFirstNationalTeam || day_ + 1 > 232)
        return ContractResult::Unavailable;
    const size_t at = squad_block(a) + slot * kSquadRecord;
    const int id = save_.state.at(at + 0x0F);
    const HokPlayer* player = id ? player_by_exe_index(id) : nullptr;
    if (!player || player->raw[0x1A] != a || save_.state.at(at + 0x0C) != 0) return ContractResult::Unavailable;
    if (save_.state.at(at + 0x18) != 0) return ContractResult::Blocked;
    const ContractPlayer current = contract_player(save_, a, int(slot));
    if (years < current.years && !allow_shortening) return ContractResult::ShorteningNotConfirmed;
    // ovl11_41db bypasses the random player check for equal years and an
    // unchanged or higher salary, including an entirely unchanged contract.
    const bool guaranteed = years == current.years && salary >= current.salary;
    if (!guaranteed && !player_accepts(contracts_, current, years, salary,
                                      matchday_[size_t(std::clamp(mgr_[a].budget.league, 0, 2))], 44, day_ + 1, brng_)) {
        save_.state.at(at + 0x18) = uint8_t(brng_.range(10, 18));
        log_.push_back(player->name + " refuses contract; talks paused for " +
                       std::to_string(save_.state.at(at + 0x18)) + " squad updates");
        return ContractResult::PlayerRefused;
    }
    save_.state.at(at + 0x0B) = uint8_t(years);
    for (int byte = 0; byte < 4; byte++) save_.state.at(at + 0x28 + size_t(byte)) = uint8_t(uint32_t(salary) >> (8 * byte));
    save_.squad_of(a).at(slot).contract_years = years;
    save_.squad_of(a).at(slot).salary = salary / 3;
    refresh_budget_roster(a);
    log_.push_back(player->name + " signs contract for " + std::to_string(years) + " years at " +
                   std::to_string(salary / 3) + " DM per month");
    return ContractResult::Signed;
}

void Game::decline_offer(size_t index) {
    const int a=active_manager_;
    if(index>=mgr_[a].transfer_offers.size())return;
    const auto offer=mgr_[a].transfer_offers[index];const auto& player=save_.players.at(size_t(offer.player));
    const int id=int((player.raw-save_.state.data()-kPlayers)/kPlayerRecord)+1,block=offer.listed?4:0;
    for(int slot=0;slot<(offer.listed?11:29);++slot){
        if(save_.state.at(squad_block(offer.listed?4:a)+size_t(slot)*52+15)!=id)continue;
        SaleQuote quote;if(begin_sale_offer(block,slot,0,quote))(void)finish_sale_offer(quote,false);return;
    }
}

void Game::train_squad(int m) {
    mgr_[m].budget.balance -= mgr_[m].budget.open_97ee;  // weekly recovery costs (146e:0c89)
    for (size_t i = 0; i < save_.squad_of(m).size(); i++) {
        HokSquadPlayer& sp = save_.squad_of(m)[i];
        if (sp.player < 0) continue;
        uint8_t* r = save_.state.data() + squad_block(m) + i * kSquadRecord;
        TrainingPlayer p;
        p.position = std::min(2, save_.players[size_t(sp.player)].position / 25);
        p.condition = r[0x10];
        p.technique = r[0x11];
        p.nibbles = r[0x19];
        p.exhaustion = r[0x13];
        p.morale = r[0x0E];
        p.trend = r[0x14];
        train_week(training_, plan_, p, brng_);
        morale_week(p, brng_);
        r[0x10] = uint8_t(p.condition);
        r[0x11] = uint8_t(p.technique);
        r[0x19] = uint8_t(p.nibbles);
        r[0x13] = uint8_t(p.exhaustion);
        r[0x0E] = uint8_t(p.morale);
        r[0x14] = uint8_t(p.trend);
        sp.skill_a = p.condition;
        sp.skill_b = p.technique;
        sp.nibbles = p.nibbles;
        sp.exhaustion = p.exhaustion * 2 / 3;
        sp.raw_exhaustion = p.exhaustion;
    }
    if (std::getenv("EM_OLD_TRAINREFRESH")) refresh_human_team(m);  // the training does not refresh the club record (REAL-CODE period 49: the drift of the day reaches the month end)
}

// ovl14_2e0f restores the saved automatic-lineup mode before entering the next
// manager menu. Recompute after loading as well: a HOK can contain an older lineup.
void Game::prepare_human_lineup(bool restore_saved_mode, int m) {
    if(restore_saved_mode)save_.state.at(0x078A + 2 * size_t(m)) = save_.state.at(0x078B + 2 * size_t(m));  // DS:0375/0376 + 2 * manager
    auto_lineup(save_, m);
    if (!skip_refresh_once_) refresh_human_team(m);
    skip_refresh_once_ = false;
    mgr_[m].squad.penalty_effort_adjustment = penalty_effort_adjustment(save_, mgr_[m].team);
    configure_human_tactics(mgr_[m].squad, save_, m);
}

// 162c:146c (called by the day loop): the human club's team record follows its squad. Per kind
// the line strengths (FUN_162c_16bf, human branch, not in play, no man advantage) of lines 0..2
// are summed and divided by 3. The monthly income (finances) uses these values.
void Game::weekly_team_drift() {
    // ovl14 day loop: on the two training days the original runs 162c:1c5b(1) over all clubs (skills with a small trend, bounds 1..99)
    // and re-rolls the form of the 64 teams (national teams only the form); REAL-CODE test realcode_rerate (mode 1).
    g_draw_scope = "team-drift";
    for (int t = 0; t < 64; t++) {
        if (t < kFirstNationalTeam) {
            rerate_team(save_.teams[size_t(t)], team_class(finance_, t), rating_, rng_, 1);
        } else {
            save_.teams[size_t(t)].form[0] = int(rng_.range(45, 55));
            save_.teams[size_t(t)].form[1] = int(rng_.range(45, 55));
            save_.teams[size_t(t)].energy[0][0] = int(rng_.range(45, 55));
        }
    }
}

void Game::refresh_human_team(int m) {
    // From the first WM day on, the WM branch of the day loop runs with manager index 5; without a human national coach
    // 162c:146c is no longer called for the club, whose record keeps the value of the last club day into the season-end
    // rerate (REAL-CODE period 30: 69/68/67/68 -> 65/65/63/63, the strength 256 of the summer bookings)
    if (wm_round_ > 0 && national_coach_ < 0 && !std::getenv("EM_OLD_WMREFRESH")) return;
    HokTeam& team = save_.teams[size_t(mgr_[m].team)];
    // 162c:146c starts with the recount of the players on the ice (sub_46386 via 24A5E; running penalties counted by
    // sub_42E90(team, 0)): after a game the record gets the count of the lines then on the ice (REAL-CODE period 45: 4 in
    // the last minute with an injured player assigned, 5 after the evening lineup)
    if (!std::getenv("EM_OLD_DAYRECOUNT")) {
        const int running = ((team.penalty_code & 7) != 0) + (((team.penalty_code >> 4) & 7) != 0);
        int per_line[2] = {line_counts_[size_t(m)][0], line_counts_[size_t(m)][1]};
        recount_skaters(team, save_.squad_of(m), save_.players, running, per_line);
        if (std::getenv("EM_DAYRECOUNT_LINES")) {  // the per-line counts DS:CC37 (written by the original as well; unverified here)
            line_counts_[size_t(m)][0] = uint8_t(per_line[0]); line_counts_[size_t(m)][1] = uint8_t(per_line[1]);
            mgr_[m].squad.per_line[0] = per_line[0]; mgr_[m].squad.per_line[1] = per_line[1];
        }
    }
    for (int kind = 0; kind < 2; kind++) {
        int sum[3] = {};
        for (int line = 0; line < 3; line++) {
            const LineValues v = line_strength(team, mgr_[m].squad, line, kind, false, 0);
            sum[0] += v.skill1;
            sum[1] += v.skill2;
            sum[2] += v.form;
        }
        team.skill1[kind] = sum[0] / 3;
        team.skill2[kind] = sum[1] / 3;
        team.form[kind] = sum[2] / 3;
    }
    mgr_[m].budget.team_strength = team.skill1[0] + team.skill1[1] + team.skill2[0] + team.skill2[1];
    if (std::getenv("EM_DEBUG_STRENGTH")) std::fprintf(stderr, "strength day %d [%s]: %d (skaters %d) s1 %d/%d s2 %d/%d at %zu\n", day_, g_draw_scope, mgr_[m].budget.team_strength, team.skaters, team.skill1[0], team.skill1[1], team.skill2[0], team.skill2[1], (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0));
}

bool Game::enter_advertising() {
    const int a=active_manager_;
    const long income=monthly_income(mgr_[a].budget,true);
    if(income==mgr_[a].budget.stored_income)return false;
    mgr_[a].budget.stored_income=income;return true;
}

bool Game::accept_sponsor_offer(int kind, size_t offer) {
    const int a = active_manager_;
    if (kind < 0 || kind >= kSponsorKinds || offer >= kSponsorOffers) return false;
    AdRecord& candidate = mgr_[a].offers[size_t(kind)][offer];
    if (candidate.amount <= 0 || candidate.months <= 0) return false;
    // ovl04_0ee0: the two facility-specific contracts require the corresponding
    // stadium extension at level 2 (DS:9771 / DS:9775).
    if ((kind == 3 && mgr_[a].budget.stadium[0] != 2) || (kind == 4 && mgr_[a].budget.stadium[2] != 2)) return false;
    auto free = [](const AdvertContract& a) { return a.amount == 0 || (a.flags & 0x80) != 0; };
    if (kind == 0) {
        if (candidate.boards < 1 || candidate.boards > 7) return false;
        if (std::count_if(mgr_[a].budget.adverts.begin(), mgr_[a].budget.adverts.begin() + 7, free) < candidate.boards) return false;
        // DS:017C (state 0x01F2) is the saved native board-contract serial.
        const int owner = save_.state.at(0x01F2);
        int left = candidate.boards;
        for (auto it = mgr_[a].budget.adverts.begin(); it != mgr_[a].budget.adverts.begin() + 7 && left; ++it)
            if (free(*it)) {
                *it = {candidate.amount, candidate.months, candidate.renewal, owner, candidate.sponsor, candidate.min_image};
                left--;
            }
        save_.state.at(0x01F2) = uint8_t(owner + 1);
        //4495:19A4..1AAE exchanges whole records for every later lower amount.
        //Indirect swaps can reverse equal-amount contracts; stable_sort differs.
        bool changed;
        do {
            changed=false;
            for(size_t i=0;i<7;++i)for(size_t j=i+1;j<7;++j)
                if(mgr_[a].budget.adverts[i].amount>mgr_[a].budget.adverts[j].amount){
                    std::swap(mgr_[a].budget.adverts[i],mgr_[a].budget.adverts[j]);changed=true;
                }
        }while(changed);
    } else {
        AdvertContract& contract = mgr_[a].budget.adverts[size_t(kind + 6)];
        if (!free(contract)) return false;
        contract = {candidate.amount, candidate.months, candidate.renewal, candidate.boards,
                    candidate.sponsor, candidate.min_image};
    }
    candidate.amount = 0;  // ovl04_0ee0 retains the other offer bytes
    // ovl04_0caa redraws the sponsor screen's full income after the decision.
    mgr_[a].budget.stored_income = monthly_income(mgr_[a].budget, true);
    return true;
}

bool Game::renew_advert_contract(size_t slot, bool accept) {
    AdvertRenewalDraft draft;
    if(!begin_advert_renewal(slot,draft))return false;
    return finish_advert_renewal(draft,accept);
}

bool Game::begin_advert_renewal(size_t slot,AdvertRenewalDraft& draft) {
    const int a = active_manager_;
    if (slot >= mgr_[a].budget.adverts.size()) return false;
    const AdvertContract original = mgr_[a].budget.adverts[slot];
    if (original.amount <= 0 || original.months_left != 0 || (original.flags & 0x7F) == 0) return false;
    // The native sponsor decision precedes the manager's confirmation, including
    // its random draws even if the manager later declines the offered extension.
    const long required = long(brng_.range(90, 120)) * original.min_image / 100;
    const bool sponsor_stays = required < current_club_image(a);
    draft={slot,original,sponsor_stays};return true;
}

bool Game::finish_advert_renewal(const AdvertRenewalDraft& draft,bool accept) {
    const int a = active_manager_;
    const size_t slot=draft.slot;
    if(slot>=mgr_[a].budget.adverts.size())return false;
    const auto& current=mgr_[a].budget.adverts[slot];const auto& original=draft.contract;
    if(original.amount<=0||original.months_left!=0||(original.flags&0x7f)==0||
       current.amount!=original.amount||current.months_left!=original.months_left||current.flags!=original.flags||
       current.board_owner!=original.board_owner||current.sponsor!=original.sponsor||current.min_image!=original.min_image)return false;
    const bool stays = accept && draft.sponsor_stays;
    AdvertContract resolved = original;
    resolved.flags = 0;  // the option can only be exercised once
    if (stays) resolved.months_left = original.flags & 0x7F;
    else resolved.amount = 0;
    for (size_t i = 0; i < mgr_[a].budget.adverts.size(); i++) {
        if (i != slot && !(slot < 7 && i < 7 && mgr_[a].budget.adverts[i].board_owner == original.board_owner)) continue;
        mgr_[a].budget.adverts[i] = resolved;  // ovl04_0ee0 copies the complete ten-byte record
    }
    mgr_[a].budget.stored_income = monthly_income(mgr_[a].budget, true);  // ovl04_0caa
    return stays;
}

void Game::expire_advert_options(int m) {
    // ovl10_1cd2: options left unresolved when leaving the day's menu expire (the menu runs once per manager, ovr038:3022).
    for (AdvertContract& contract : mgr_[size_t(m)].budget.adverts)
        if (contract.months_left == 0 && contract.flags != 0) {
            contract.amount = 0;
            contract.flags = 0;
            contract.board_owner = 255;
        }
}

int Game::current_club_image(int m) {
    ImageInputs in;
    const auto competition = image_competition_factors(save_, m);
    in.in_competition = competition.playoffs;
    in.cup = competition.cup;
    in.euro = competition.euro;
    in.league = mgr_[m].budget.league;
    in.matchday = matchday_[size_t(std::clamp(mgr_[m].budget.league, 0, 2))];
    const int team = mgr_[m].team;
    const League& league = leagues_[size_t(std::clamp(in.league, 0, 2))];
    const int row_slot = team - league.first_team;
    for (int j = std::max(in.matchday - 5, 1); j < in.matchday; j++) {
        // A relegated regional club has no row in the three simulated leagues.
        // Do not interpret neighbouring Game members as an Oberliga table row.
        if (row_slot < 0 || row_slot >= kHokLeagueSize) break;
        const HokTableRow& row = league.rows.at(size_t(row_slot));
        in.recent_results.push_back(fever_curve_position(row, j));
    }
    std::vector<ContractPlayer> image_squad;
    for (int slot = 0; slot < kHokSquadSlots; slot++) {
        int p = -1;
        ContractPlayer c = contract_player(save_, m, slot, &p);
        if (p >= 0) image_squad.push_back(c);
    }
    in.stadium = stadium_score(sponsor_tables_, mgr_[m].budget.stadium, mgr_[m].budget.places);
    return club_image(in, image_squad, contracts_, brng_);
}

void Game::update_sponsors(int m) {
    // ovl08_2ba7 uses the image already stored by the preceding daily
    // preparation; calculating a new image here changes both phase and RNG.
    generate_sponsor_offers(sponsor_tables_, mgr_[m].image, false, mgr_[m].offers, brng_);
    char line[96];
    std::snprintf(line, sizeof line, "image %d, sponsor offers renewed", mgr_[m].image);
    log_.push_back(line);
}

void Game::play_euro_day() {
    if (euro_round_ >= 12) return;
    const bool human_euro = human_in_next_euro();
    const int r = euro_round_++;  // DS:07AF
    // DS:0228 requests full EuroCup display. An active human participant also
    // selects the minute path for the whole event; pure CPU events can be fast.
    bool minute_path = save_.state.at(0xA527) != 0;
    if (r < 6) {
        for (const auto& group : euro_groups_)
            for (int team : group.teams) minute_path |= save_.teams.at(size_t(team)).raw[0x16] != 0;
    } else {
        for (int team : euro_final_.teams) minute_path |= save_.teams.at(size_t(team)).raw[0x16] != 0;
    }
    auto game = [&](GroupTable& table, int home, int away, int group, int pairing) {
        MatchStats stats;
        MatchResult res;
        if (minute_path) {  // every manager's club plays with his squad (one statistics object per game, booked per manager below)
            res = league_match(save_.teams[size_t(home)], save_.teams[size_t(away)], rng_, 0,
                               manager_squad(home), manager_squad(away), &stats);
        } else {
            auto& h = save_.teams[size_t(home)]; auto& a = save_.teams[size_t(away)];
            prepare_match_energy(h, nullptr); prepare_match_energy(a, nullptr);
            setup_computer_team(h, a); setup_computer_team(a, h);
            res = quick_competition_match(h, a, home < 64, away < 64, rng_, 0, false);
        }
        table.book(home, away, res);
        // ovl05_1322 records the last actually played EuroCup phase for every
        // owned participant: 2 in the groups, 4 in the final group. Merely
        // appearing in a new draw leaves the native sentinel 30 unchanged.
        for (int team : {home, away}) {
            const int owner = save_.teams[size_t(team)].raw[0x16];
            if (owner >= 1 && owner <= 6)
                save_.state.at(kManagerBlock + size_t(owner - 1) * 0x1AF + 0xE7) = uint8_t(r < 6 ? 2 : 4);
        }
        const size_t o = kGroupResults + size_t(r < 6 ? 2 : 3) * 0x48 + size_t(group) * 0x24 +
                         size_t(r % 6) * 6 + size_t(pairing) * 2;
        save_.state.at(o) = uint8_t(res.home);
        save_.state.at(o + 1) = uint8_t(res.away);
        // every manager of the game in index order; the goals are credited here (draws): with several managers the picks
        // follow the games of the day, within a game the manager index (assumption, docs/MEHRSPIELER_PLAN.md step 7)
        for (int m = 0; m < managers_; m++) {
            const int own_team = mgr_[size_t(m)].team;
            if (home != own_team && away != own_team) continue;
            const int own_side = home == own_team ? 0 : 1;
            const bool forfeited = stats.forfeit_side != 0;
            if (forfeited && stats.forfeit_side == own_side + 1) book_forfeit(stats, m);
            if (!forfeited) {
                book_match_condition(stats, own_side, m);
                book_special_teams(stats, own_side, m);
                book_club_match(own_side == 0 ? res.home : res.away,
                                own_side == 0 ? res.away : res.home, 2, false, 0, true, m);
                mgr_[size_t(m)].played = true;
            }
            record_head_to_head(home, away, res.home, res.away, m);
            char line[160];
            std::snprintf(line, sizeof line, "European cup: %s - %s %d:%d", save_.teams[size_t(home)].name.c_str(),
                          save_.teams[size_t(away)].name.c_str(), res.home, res.away);
            log_.push_back(line);
            if (!forfeited && own_side == 0)  // DS:D0C2 = 3 in the groups, 5 in the final round
                home_game(away, r < 6 ? 2 : 3, team_class(finance_, home), team_class(finance_, away), team_rank(home), 5, false, m);
        }
    };
    if (r < 6) {
        for (size_t g = 0; g < 2; g++)
            for (size_t i = 0; i < save_.euro_rounds[g][size_t(r)].size(); i++) {
                const HokPairing& p = save_.euro_rounds[g][size_t(r)][i];
                game(euro_groups_[g], p.home, p.away, int(g), int(i));
            }
        for (auto& group : euro_groups_) group.finish_round();
        if (r == 5) {  // ovl05_0ef7(3): the two best of each group play the final round
            const auto a = euro_groups_[0].order(), b = euro_groups_[1].order();
            euro_final_ = GroupTable{};
            euro_final_.teams = {euro_groups_[0].teams[size_t(a[0])], euro_groups_[0].teams[size_t(a[1])],
                                 euro_groups_[1].teams[size_t(b[0])], euro_groups_[1].teams[size_t(b[1])]};
            for (size_t slot = 0; slot < 4; slot++)
                save_.state.at(kEuroFinalTeams + slot) = uint8_t(euro_final_.teams[slot]);
            store_group_schedule(save_.state, euro_final_, 3, 0, schedule_);
            // the final group is stored in the five-team layout: third pair of every round 6:2, order slots 4 and 5
            // identical (REAL-CODE period 15; only when the group is formed, native saves keep their own bytes)
            for (int round = 0; round < 6; round++) {
                save_.state.at(kGroupSchedule + size_t(3 * 0x48 + round * 6) + 4) = 6;
                save_.state.at(kGroupSchedule + size_t(3 * 0x48 + round * 6) + 5) = 2;
            }
            save_.state.at(kGroupOrder + 3 * 12 + 4) = 4;
            save_.state.at(kGroupOrder + 3 * 12 + 5) = 5;
            std::fill(save_.state.begin() + long(kGroupResults + 3 * 0x48),
                      save_.state.begin() + long(kGroupResults + 4 * 0x48), uint8_t(0));
            std::string line = "European cup final round:";
            for (int t : euro_final_.teams) line += " " + save_.teams[size_t(t)].name + ";";
            log_.push_back(line);
        }
    } else if (r < 12) {  // final round: double round robin of four (letter table DS:0474)
        const auto pairs = round_robin_pairings(schedule_.teams4, r - 6);
        for (size_t i = 0; i < pairs.size(); i++) {
            const Pairing& p = pairs[i];
            game(euro_final_, euro_final_.teams[size_t(p.home)], euro_final_.teams[size_t(p.away)], 0, int(i));
        }
        euro_final_.finish_round();
        if (r == 11) {
            euro_champion_ = euro_final_.teams[size_t(euro_final_.order()[0])];
            log_.push_back("European cup winner: " + save_.teams[size_t(euro_champion_)].name);
        }
    }
    // every EuroCup day leaves the pairs of the round in the slots DS:5D5B (state 0x8EB1): home, away, home, away per group;
    // the final round has one group of four and FF behind it (REAL-CODE periods 13-15)
    if (r < 6) {
        for (size_t g = 0; g < 2; g++)
            for (size_t i = 0; i < 2 && i < save_.euro_rounds[g][size_t(r)].size(); i++) {
                save_.state.at(0x8EB1 + g * 4 + i * 2) = uint8_t(save_.euro_rounds[g][size_t(r)][i].home);
                save_.state.at(0x8EB1 + g * 4 + i * 2 + 1) = uint8_t(save_.euro_rounds[g][size_t(r)][i].away);
            }
    } else if (r < 12) {
        const auto pairs = round_robin_pairings(schedule_.teams4, r - 6);
        for (size_t i = 0; i < 2 && i < pairs.size(); i++) {
            save_.state.at(0x8EB1 + i * 2) = uint8_t(euro_final_.teams[size_t(pairs[i].home)]);
            save_.state.at(0x8EB1 + i * 2 + 1) = uint8_t(euro_final_.teams[size_t(pairs[i].away)]);
        }
        for (size_t s = 4; s < 8; s++) save_.state.at(0x8EB1 + s) = 0xFF;
    }
    if (!human_euro && !minute_path) {
        // a EuroCup day without the human club rests the 64 clubs like a cup day without him: energy 90, shifts 0, the
        // human squad's fitness 90 and match load, DS:A88D = FF (REAL-CODE period 13)
        for (int team = 0; team < 64; team++) {
            HokTeam& t = save_.teams[size_t(team)];
            const int mode = manager_team(team) ? (t.flags & 0x07) : 0;
            prepare_match_energy(t, nullptr);
            t.flags |= mode;
        }
        for (int m = 0; m < managers_; m++) {
            auto& own_squad = save_.squad_of(m);
            const size_t own_block = squad_block(m);
            for (size_t slot = 0; slot < own_squad.size() && slot < size_t(kHokSquadSlots); slot++) {
                if (save_.state.at(own_block + slot * kSquadRecord + 0x0F) == 0) continue;
                const uint8_t f = rested_fitness(save_.state.at(own_block + slot * kSquadRecord + 0x13));
                save_.state.at(own_block + slot * kSquadRecord + 0x1B) = f;
                own_squad[slot].fitness = f;
            }
            const ManagerState& mg = mgr_[size_t(m)];
            if (mg.team >= 0 && mg.team < kHokTeams)
                save_.state.at(manager_block(m) + 0xF0) = uint8_t(squad_match_load(save_.teams[size_t(mg.team)], mg.squad));
        }
        save_.state.at(0x784) = 0xFF;
    }
    save_.state.at(kEuroRound) = uint8_t(euro_round_);
}

// ovl06_161d quick path, scan loop: 162c:02a5 sets up home, then away (which already sees the home team's new effort)
// directly in the team records, so the effort and tactics stay until the next set-up (REAL-CODE period 28: Landshut plays
// its next set-up with effort 60). The strengths are computed with rested lines (energy 90); the record keeps its energies.
// `h` and `a` receive the teams as they enter the quick result.
void Game::quick_setup(int home, int away, HokTeam& h, HokTeam& a) {
    HokTeam& H = save_.teams[size_t(home)];
    HokTeam& A = save_.teams[size_t(away)];
    h = H; a = A;
    prepare_match_energy(h, nullptr); prepare_match_energy(a, nullptr);
    setup_computer_team(h, a);
    setup_computer_team(a, h);
    h.penalty_code = a.penalty_code = 0;  // running penalties end with the set-up (REAL-CODE period 28: +0x33 of the relegation clubs)
    for (auto [rec, run] : {std::pair<HokTeam*, const HokTeam*>{&H, &h}, {&A, &a}}) {
        int energy[2][4];
        std::memcpy(energy, rec->energy, sizeof energy);
        *rec = *run;
        std::memcpy(rec->energy, energy, sizeof energy);
    }
}

void Game::play_playoff_day(bool setup_only) {
    // The first round of the phase (quarter-final pairs, relegation groups, play-off grids) is set up at the end of the
    // last league day: with the human club in the play-offs the menu before the first play-off day already shows it
    // (REAL-CODE Köln period 33); without it the day loop plays on and the timing does not show (Mannheim period 28).
    if (setup_only && (playoff_day_ != 0 || playoffs_set_up_)) return;
    const int day = setup_only ? 0 : playoff_day_++;
    const bool already_set_up = !setup_only && day == 0 && playoffs_set_up_;
    if (!setup_only) playoffs_set_up_ = false;
    const int stage = day / 5;  // five days per best-of-five round
    MatchStats stats;
    std::array<MatchStats, kMaxManagers> other_stats{};  // the matches of managers 1..
    const HumanSide side = human_sides(&stats, other_stats);
    const League& first = leagues_[0];
    auto position = [&](int team) { return first.rows[size_t(team - first.first_team)].position; };
    if (day % 5 == 0 && !already_set_up) {
        std::vector<int> teams;
        if (stage == 0) {  // DS:5CCB: the first eight of the final table, 1-8, 2-7, 3-6, 4-5
            std::vector<int> order(kHokLeagueSize);
            for (int s = 0; s < kHokLeagueSize; s++) order[size_t(first.rows[size_t(s)].position)] = first.first_team + s;
            for (int i = 0; i < 4; i++) teams.insert(teams.end(), {order[size_t(i)], order[size_t(7 - i)]});
            // ovl05_0ef7(1): relegation groups of the 11th and 12th of a division with the 1st and
            // 2nd of the one below (DS:5CD5 + group * 12)
            for (size_t l = 0; l < 2; l++) {
                std::array<int, kHokLeagueSize> up{}, down{};
                for (int s = 0; s < kHokLeagueSize; s++) {
                    up[size_t(leagues_[l].rows[size_t(s)].position)] = leagues_[l].first_team + s;
                    down[size_t(leagues_[l + 1].rows[size_t(s)].position)] = leagues_[l + 1].first_team + s;
                }
                relegation_[l] = GroupTable{};
                relegation_[l].teams = {up[10], up[11], down[0], down[1]};
                store_group_schedule(save_.state, relegation_[l], 1, int(l), schedule_);
            }
            // sub_3E007(1) builds every round in the league pair table DS:5D7B (4 letter bytes converted to team ids) and
            // copies all 6 bytes to the schedule: bytes 4/5 of each round are the stale league pair bytes; the table keeps
            // the last round of the second group. Order slots 4/5 keep 4/5 (the sort only touches 0..3), DS:5CF5/5CF6 = 3,
            // team-grid row 3 starts with FF (REAL-CODE period 28).
            Bytes& st = save_.state;
            for (size_t g = 0; g < 2; g++) {
                for (int round = 0; round < 6; round++) {
                    const size_t at = kGroupSchedule + 0x48 + g * 0x24 + size_t(round) * 6;
                    for (size_t k = 0; k < 4; k++) st.at(0x8DFD + k) = st.at(at + k);
                    st.at(at + 4) = st.at(0x8DFD + 4);
                    st.at(at + 5) = st.at(0x8DFD + 5);
                }
                st.at(0x05F8 + 12 + g * 6 + 4) = 4;
                st.at(0x05F8 + 12 + g * 6 + 5) = 5;
            }
            st.at(0x8ED3) = st.at(0x8ED4) = 3;
            save_.pairings[0][0] = {st.at(0x8DFD), st.at(0x8DFE)};  // the saved pair table keeps these bytes
            save_.pairings[0][1] = {st.at(0x8DFF), st.at(0x8E00)};
            std::fill(st.begin() + long(kPlayoffTeams), st.begin() + long(kPlayoffTeams) + 32, uint8_t(0xFF));  // sub_3F514(0): row 2 starts with FF
            std::fill(st.begin() + long(kPlayoffLive), st.begin() + long(kPlayoffLive) + 32, uint8_t(0));
            std::fill(st.begin() + 0x8EB1, st.begin() + 0x8ED1, uint8_t(0xFF));
        } else {  // bracket (1-8 / 3-6) and (2-7 / 4-5), then the final
            std::vector<int> w;
            for (const Series& s : series_) w.push_back(s.winner());
            // DS:0144 maps quarter winners to slots 9,11,10,12 (one-based).
            if (stage == 1 && w.size() == 4) teams = {w[0], w[2], w[1], w[3]};
            if (stage == 2 && w.size() == 2) teams = {w[0], w[1]};
        }
        series_.clear();
        for (size_t i = 0; i + 1 < teams.size(); i += 2) {
            Series s;
            // quarter finals by the final table; later rounds keep the bracket slot: the winner inherits the seed of its slot
            // (REAL-CODE period 28: Kaufbeuren, 6th, beat Krefeld, 2nd, and is the home team of the first semi-final against the 3rd)
            s.higher = stage > 0 || position(teams[i]) < position(teams[i + 1]) ? teams[i] : teams[i + 1];
            s.lower = s.higher == teams[i] ? teams[i + 1] : teams[i];
            series_.push_back(s);
        }
        const size_t base = size_t(kPlayoffBase[stage]);
        for (size_t i = 0; i < series_.size(); i++) {
            const size_t slot = base + i * 2;
            save_.state.at(kPlayoffTeams + slot) = uint8_t(series_[i].higher);
            save_.state.at(kPlayoffTeams + slot + 1) = uint8_t(series_[i].lower);
            save_.state.at(kPlayoffWins + slot) = save_.state.at(kPlayoffWins + slot + 1) = 0;
            for (size_t game = 0; game < 5; game++) {
                save_.state.at(kPlayoffScores + game * 14 + slot) = 0;
                save_.state.at(kPlayoffScores + game * 14 + slot + 1) = 0;
            }
        }
    }
    if (setup_only) {
        // the set-up starts the phase cursor at the quarter finals and clears the pending flag (REAL-CODE Köln period 33;
        // on a play-off day store_playoff_state overwrites both)
        save_.state.at(kPlayoffCursor) = uint8_t(kPlayoffBase[0]);
        save_.state.at(kPlayoffPending) = 0;
        playoffs_set_up_ = true;
        return;
    }
    // ovl06_161d quick path (no human club in any series of the day): the games are played in slot order into the
    // result grid DS:5C4B; after each game sub_40683 scans every open pair of the day for H == A - the pairs not played
    // yet are still 0:0 and count (they are marked 30:0) - and any such pair gives the current game a shoot-out of
    // Borland rand(0,2) pairs, +30 on its home byte (a real draw was marked as well: +60). REAL-CODE period 28.
    std::vector<size_t> open_today;
    bool human_today = false;
    for (size_t i = 0; i < series_.size(); i++) {
        const Series& s = series_[i];
        if (s.wins_higher == 3 || s.wins_lower == 3) continue;
        open_today.push_back(i);
        human_today |= manager_team(s.higher) || manager_team(s.lower);
    }
    if (!human_today) {  // like a live cup day without the human club, the play-off day passes the menu routine (REAL-CODE period 28),
        // without a sort (REAL-CODE period 91: no swap on any of the play-off days without the human club)
        if (std::getenv("EM_OLD_POSORT")) native_sort_messages(save_.state);
        native_message_step(save_.state);
    }
    std::vector<std::array<int, 2>> grid(series_.size(), {0, 0});
    auto quick_playoff = [&](size_t index, int home, int away) {
        HokTeam h, a;
        quick_setup(home, away, h, a);
        if (std::getenv("EM_DEBUG_PQ"))
            for (const HokTeam* t : {&save_.teams[size_t(home)], &save_.teams[size_t(away)]})
                std::fprintf(stderr, "pq %s: bonus %d s1 %d/%d s2 %d/%d form %d/%d en %d %d %d %d %d %d %d %d eff %d fl %02x lines %d/%d sk %d pen %02x\n", t->name.c_str(), t->bonus,
                             t->skill1[0], t->skill1[1], t->skill2[0], t->skill2[1], t->form[0], t->form[1], t->energy[0][0], t->energy[0][1], t->energy[0][2], t->energy[0][3],
                             t->energy[1][0], t->energy[1][1], t->energy[1][2], t->energy[1][3], t->effort_current, t->flags, t->current_line[0], t->current_line[1], t->skaters, t->penalty_code);
        MatchResult r = quick_competition_match(h, a, home < 64, away < 64, rng_, 0, false, true);  // German clubs +6
        grid[index] = {r.home, r.away};
        bool tie = false;
        for (size_t j : open_today)
            if (grid[j][0] == grid[j][1]) { tie = true; grid[j][0] += 30; }
        if (tie) {
            int c = 0, e = 0;
            do {
                c = brng_.range(0, 2);
                e = brng_.range(0, 2);
            } while (grid[index][0] + c == grid[index][1] + e);
            grid[index][0] += c + 30;
            grid[index][1] += e;
        }
        r.decision_marker = (grid[index][0] / 30) * 30;
        r.home = grid[index][0] - r.decision_marker;
        r.away = grid[index][1];
        r.shootout = tie;
        return r;
    };
    // A play-off day with a human club runs like a live cup day (REAL-CODE Köln period 34): per pair in slot order the
    // gate of a manager's home game and the squad bookkeeping (sub_60D60), one stream spin, then every open series of the
    // day together in the minute loop (ovl06_2522) with the knock-out rules.
    std::vector<MatchResult> live_result(series_.size());
    std::array<bool, kMaxManagers> gate_before{};
    const bool live_round = human_today && !std::getenv("EM_OLD_POLIVE");
    size_t knockout_pairs = 0;
    int po_loop_minutes = 60;  // the last minute of the live loop (overtime breaks, see play_cup_day)
    std::vector<MatchResult> relegation_live;  // results of the relegation games played in the live loop (group 1, then 2)
    if (live_round) {
        std::vector<HokPairing> draw;
        std::vector<size_t> draw_index;
        for (size_t index : open_today) {
            const Series& s = series_[index];
            const bool higher_home = s.games.size() % 2 == 0;
            draw.push_back({higher_home ? s.higher : s.lower, higher_home ? s.lower : s.higher});
            draw_index.push_back(index);
        }
        knockout_pairs = draw.size();
        // the relegation round of the day runs in the same minute loop behind the play-off pairs (REAL-CODE Köln period 34:
        // FUN_162c_16bf is called for 8-2, 18-12, 22-15 right after the quarter finals)
        if (day % 5 == 0 || day % 5 == 2) {
            const int round = stage * 2 + (day % 5 == 2 ? 1 : 0);
            const auto pairs = round_robin_pairings(schedule_.teams4, round);
            for (size_t g = 0; g < 2; g++)
                for (const Pairing& p : pairs) draw.push_back({relegation_[g].teams[size_t(p.home)], relegation_[g].teams[size_t(p.away)]});
        }
        for (const HokPairing& p : draw)
            for (int m = 0; m < managers_; m++) {
                ManagerState& mg = mgr_[size_t(m)];
                if (p.home != mg.team && p.away != mg.team) continue;
                if (mg.squad.minimum_players > 0 && selected_match_players(mg.squad) < mg.squad.minimum_players) continue;
                if (p.home == mg.team) {
                    g_draw_scope = "gate";
                    home_game(p.away, 1, 0, 0, position(p.home), position(p.away), false, m);
                    gate_before[size_t(m)] = true;
                }
                manager_day(gate_before[size_t(m)], m);
            }
        spin_match_stream();
        for (int m = 0; m < managers_; m++) {
            HumanSquad& sq = mgr_[size_t(m)].squad;
            sq.on_minute = [this, m](int, int load, const HokTeam& team) { injury_minute(load, &team, m); };
            sq.on_chance = [this, m](bool goal, const HokTeam& team, const std::vector<HokSquadPlayer>& working) { credit_human_goal(0, false, goal, &team, &working, m); };
            sq.on_opponent_chance = [this](bool goal, const HokTeam& team) { credit_opponent_goal(0, goal, team); };
        }
        g_draw_scope = "playoff-live";
        const auto ties = play_cup_round_live(save_.teams, draw, rng_, side, &brng_, match_observer_ ? &match_observer_ : nullptr, &po_loop_minutes, true, knockout_pairs);
        for (size_t k = knockout_pairs; k < ties.size(); k++) relegation_live.push_back(ties[k].result);
        injuries_in_minutes_ = true;
        for (int m = 0; m < managers_; m++) {
            HumanSquad& sq = mgr_[size_t(m)].squad;
            sq.on_minute = nullptr;
            sq.on_chance = nullptr;
            sq.on_opponent_chance = nullptr;
        }
        for (size_t k = 0; k < ties.size() && k < draw_index.size(); k++) live_result[draw_index[k]] = ties[k].result;
    }
    for (size_t index = 0; index < series_.size(); index++) {
        Series& s = series_[index];
        if (s.wins_higher == 3 || s.wins_lower == 3) continue;
        const bool higher_home = s.games.size() % 2 == 0;  // games 1, 3, 5 at the higher seed
        const int home = higher_home ? s.higher : s.lower, away = higher_home ? s.lower : s.higher;
        const MatchResult r = live_round ? live_result[index] : human_today ? playoff_game(save_.teams, home, away, rng_, side) : quick_playoff(index, home, away);
        s.games.push_back(r);
        if ((r.home > r.away) == higher_home)  // a level result after the quick shoot-out goes to the away team (REAL-CODE period 28: both streams equal)
            s.wins_higher++;
        else
            s.wins_lower++;
        // every manager of the game in index order, the games in series order (the goals are credited here: assumption,
        // docs/MEHRSPIELER_PLAN.md step 7)
        MatchStats* game_stats = side.match_stats(home, away);
        for (int m = 0; m < managers_; m++) {
            const int own_team = mgr_[size_t(m)].team;
            if (home != own_team && away != own_team) continue;
            const int own_side = home == own_team ? 0 : 1;
            const MatchStats& st = game_stats ? *game_stats : stats;
            const bool forfeited = st.forfeit_side != 0;
            if (forfeited && st.forfeit_side == own_side + 1) book_forfeit(st, m);
            if (!forfeited) {
                book_match_condition(st, own_side, m);
                book_special_teams(st, own_side, m);
                book_club_match(own_side == 0 ? r.home : r.away,
                                own_side == 0 ? r.away : r.home, 0, false,
                                r.shootout_goals[own_side], !live_round, m);  // live: the goals were credited in the minutes
                mgr_[size_t(m)].played = true;
            }
            record_head_to_head(home, away, r.home, r.away, m);
            char line[160];
            std::snprintf(line, sizeof line, "play-offs: %s - %s %d:%d (series %d:%d)", save_.teams[size_t(home)].name.c_str(),
                          save_.teams[size_t(away)].name.c_str(), r.home, r.away, s.wins_higher, s.wins_lower);
            log_.push_back(line);
            if (!forfeited && own_side == 0 && !gate_before[size_t(m)]) home_game(away, 1, 0, 0, position(home), position(away), false, m);
            if (live_round && !forfeited) {
                // the overtime breaks of the live loop rest the managed pair after its own game too, as on a cup day
                // (play_cup_day; REAL-CODE Köln period 34: Köln - Kaufbeuren decided in minute 61, +19 for both and the squad)
                int played = 0;
                for (int line = 0; line < 4; line++) played += st.line_minutes[0][line];
                const int loop_breaks = po_loop_minutes > 60 ? 2 : 0;
                const int own_breaks = played >= 66 ? 2 : played >= 61 ? 1 : 0;
                const int recover = 19 * std::max(0, loop_breaks - own_breaks);
                if (recover > 0) {
                    for (int team : {home, away})
                        for (auto& kind : save_.teams[size_t(team)].energy)
                            for (auto& e : kind) e = uint8_t(std::min(90, e + recover));
                    auto& own_squad = save_.squad_of(m);
                    for (size_t slot = 0; slot < own_squad.size() && slot < size_t(kHokSquadSlots); slot++) {
                        uint8_t& f = save_.state.at(squad_block(m) + slot * kSquadRecord + 0x1B);
                        f = uint8_t(std::min(90, int(f) + recover));
                        own_squad[slot].fitness = f;
                    }
                }
            }
        }
    }
    // relegation rounds on the first and third day of each play-off round (round index =
    // DS:07B0 * 2 + [DS:CCA3 = 2]): a double round robin of four, letter table DS:0474
    if (day % 5 == 0 || day % 5 == 2) {
        const int round = stage * 2 + (day % 5 == 2 ? 1 : 0);
        for (size_t g = 0; g < 2; g++) {
            GroupTable& t = relegation_[g];
            const auto pairs = round_robin_pairings(schedule_.teams4, round);
            // ovr029:250C..2585: the round's pairs go to team-grid row 3 from the stage cursor (0/8/12), four per group
            for (size_t pair = 0; pair < pairs.size() && pair < 2; pair++) {
                const size_t at = 0x8EB1 + size_t(kPlayoffBase[stage]) + g * 4 + pair * 2;
                save_.state.at(at) = uint8_t(t.teams[size_t(pairs[pair].home)]);
                save_.state.at(at + 1) = uint8_t(t.teams[size_t(pairs[pair].away)]);
            }
            for (size_t pair = 0; pair < pairs.size(); pair++) {
                const Pairing& p = pairs[pair];
                const int home = t.teams[size_t(p.home)], away = t.teams[size_t(p.away)];
                MatchResult r;
                MatchStats* game_stats = &stats;
                const size_t live_at = g * pairs.size() + pair;
                if (live_round && live_at < relegation_live.size()) {
                    r = relegation_live[live_at];
                } else if (!manager_team(home) && !manager_team(away)) {  // without a human club a quick result (REAL-CODE period 28)
                    HokTeam h, a;
                    quick_setup(home, away, h, a);  // same quick path as the play-offs
                    r = quick_competition_match(h, a, true, true, rng_, 0, false, true);
                } else {
                    // a game of manager 0 books into `stats`, one of another manager into his own
                    const int owner = manager_of_team(home) == 0 || manager_of_team(away) == 0 ? 0 : std::max(manager_of_team(home), manager_of_team(away));
                    game_stats = owner == 0 ? &stats : &other_stats[size_t(owner)];
                    r = league_match(save_.teams[size_t(home)], save_.teams[size_t(away)], rng_, 0, manager_squad(home),
                                     manager_squad(away), game_stats);
                }
                t.book(home, away, r);
                const size_t o = kGroupResults + 0x48 + g * 0x24 + size_t(round) * 6 + pair * 2;
                save_.state.at(o) = uint8_t(r.home);
                save_.state.at(o + 1) = uint8_t(r.away);
                for (int m = 0; m < managers_; m++) {  // every manager of the game in index order (see above)
                    const int own_team = mgr_[size_t(m)].team;
                    if (home != own_team && away != own_team) continue;
                    const int own_side = home == own_team ? 0 : 1;
                    const MatchStats& st = *game_stats;
                    const bool forfeited = st.forfeit_side != 0;
                    if (forfeited && st.forfeit_side == own_side + 1) book_forfeit(st, m);
                    if (!forfeited) {
                        book_match_condition(st, own_side, m);
                        book_special_teams(st, own_side, m);
                        book_club_match(own_side == 0 ? r.home : r.away,
                                        own_side == 0 ? r.away : r.home, 0, false, 0, true, m);
                        mgr_[size_t(m)].played = true;
                    }
                    record_head_to_head(home, away, r.home, r.away, m);
                    char line[160];
                    std::snprintf(line, sizeof line, "relegation round %d: %s - %s %d:%d", round + 1,
                                  save_.teams[size_t(home)].name.c_str(), save_.teams[size_t(away)].name.c_str(), r.home, r.away);
                    log_.push_back(line);
                    if (!forfeited && own_side == 0) home_game(away, 1, team_class(finance_, home), team_class(finance_, away), 5, 5, false, m);
                }
            }
            t.finish_round();
            if (round == 5) {
                const auto o = t.order();
                std::string up = "relegation group " + std::to_string(int(g) + 1) + ": higher division", down = "; lower:";
                for (size_t i = 0; i < 4; i++) {
                    const int team = t.teams[size_t(o[i])];
                    relegation_upper_[g][i] = team;
                    (i < 2 ? up : down) += " " + save_.teams[size_t(team)].name;
                }
                log_.push_back(up + down);
            }
        }
    }
    // after the games a play-off day without the human club rests like a EuroCup day without him: all 64 club records
    // energy 90 (the managed clubs keep their mode, the teams of today's quick games too), the squad fitness from the load
    // (rested_fitness), DS:A88D = FF (REAL-CODE Köln period 35: after the cup final Köln and Berlin, finalist of the
    // play-offs, stand at 90 on 30.03.; in the Mannheim chain everything was rested long before, so it did not show)
    if (!human_today && !std::getenv("EM_OLD_PODAYREST")) {
        for (int team = 0; team < 64; team++) {
            HokTeam& t = save_.teams[size_t(team)];
            const int mode = manager_team(team) ? (t.flags & 0x07) : 0;
            prepare_match_energy(t, nullptr);
            t.flags |= mode;
        }
        for (int m = 0; m < managers_; m++) {
            const ManagerState& mg = mgr_[size_t(m)];
            if (mg.team < 0 || mg.team >= kHokTeams) continue;
            auto& own_squad = save_.squad_of(m);
            for (size_t slot = 0; slot < own_squad.size() && slot < size_t(kHokSquadSlots); slot++) {
                if (save_.state.at(squad_block(m) + slot * kSquadRecord + 0x0F) == 0) continue;
                const uint8_t f = rested_fitness(save_.state.at(squad_block(m) + slot * kSquadRecord + 0x13));
                save_.state.at(squad_block(m) + slot * kSquadRecord + 0x1B) = f;
                own_squad[slot].fitness = f;
            }
        }
        save_.state.at(0x784) = 0xFF;
    }
    if (stage == 2 && series_.size() == 1 && champion_ < 0 && (series_[0].wins_higher == 3 || series_[0].wins_lower == 3)) {
        champion_ = series_[0].winner();
        log_.push_back("German champion: " + save_.teams[size_t(champion_)].name);
    }
    store_playoff_state(stage, day % 5 + 1);
}

void Game::store_playoff_state(int stage, int completed_days) {
    Bytes& state = save_.state;
    const size_t base = size_t(kPlayoffBase[stage]);
    bool open = false;
    for (size_t pair = 0; pair < series_.size(); pair++) {
        const Series& s = series_[pair];
        const size_t slot = base + pair * 2;
        const bool finished = s.wins_higher == 3 || s.wins_lower == 3;
        open |= !finished;
        const int first = completed_days == 5 ? s.winner() : completed_days % 2 == 0 ? s.higher : s.lower;
        const int second = first == s.higher ? s.lower : s.higher;
        const int previous_games = std::min(int(state.at(kPlayoffWins + slot)), 3) +
                                   std::min(int(state.at(kPlayoffWins + slot + 1)), 3);
        // All native history columns follow the current participant order. Swap the old
        // bytes intact, preserving imported extra-time/penalty markers of 30 and 60.
        if (state.at(kPlayoffTeams + slot) != first)
            for (size_t game = 0; game < 5; game++)
                std::swap(state.at(kPlayoffScores + game * 14 + slot), state.at(kPlayoffScores + game * 14 + slot + 1));
        if (int(s.games.size()) > previous_games) {
            const size_t game = s.games.size() - 1;
            const MatchResult& result = s.games[game];
            const bool first_home = (game % 2 == 0) == (first == s.higher);
            const size_t o = kPlayoffScores + game * 14 + slot;
            state.at(o) = uint8_t((first_home ? result.home : result.away) + result.decision_marker);
            state.at(o + 1) = uint8_t(first_home ? result.away : result.home);
        }
        for (size_t game = s.games.size(); game < 5; game++) {
            state.at(kPlayoffScores + game * 14 + slot) = uint8_t(finished ? 100 : 0);
            state.at(kPlayoffScores + game * 14 + slot + 1) = uint8_t(finished ? 100 : 0);
        }
        state.at(kPlayoffTeams + slot) = uint8_t(first);
        state.at(kPlayoffTeams + slot + 1) = uint8_t(second);
        state.at(kPlayoffWins + slot) = uint8_t(first == s.higher ? s.wins_higher : s.wins_lower);
        state.at(kPlayoffWins + slot + 1) = uint8_t(first == s.higher ? s.wins_lower : s.wins_higher);
        // sub_3F1C9: a series decided before the last day marks only the first grid byte with 100
        state.at(kPlayoffLive + slot) = uint8_t(finished && completed_days < 5 ? 100 : 0);
        state.at(kPlayoffLive + slot + 1) = 0;
        if (completed_days == 5 && s.winner() == s.lower) {
            if (stage < 2) {  // the round end swaps a lower-seed winner back: (higher, lower), wins (wH, 0x14) (sub_3F514/sub_3F0E5)
                for (size_t game = 0; game < 5; game++)
                    std::swap(state.at(kPlayoffScores + game * 14 + slot), state.at(kPlayoffScores + game * 14 + slot + 1));
                state.at(kPlayoffTeams + slot) = uint8_t(s.higher);
                state.at(kPlayoffTeams + slot + 1) = uint8_t(s.lower);
                state.at(kPlayoffWins + slot) = uint8_t(s.wins_higher);
                state.at(kPlayoffWins + slot + 1) = 0x14;
            } else {
                state.at(kPlayoffWins + slot) = 0x14;  // the final keeps the winner first, marked 0x14 (sub_3F1C9 at game 4)
            }
        }
    }
    int next_stage = stage, next_game = completed_days;
    if (completed_days == 5 && stage < 2) {
        std::vector<int> winners;
        for (const Series& s : series_) winners.push_back(s.winner());
        if (stage == 0) std::swap(winners[1], winners[2]);
        const size_t next = size_t(kPlayoffBase[stage + 1]);
        for (size_t i = 0; i + 1 < winners.size(); i += 2) {
            if (std::getenv("EM_OLD_PLAYOFF_SEED") && team_rank(winners[i]) > team_rank(winners[i + 1])) std::swap(winners[i], winners[i + 1]);  // the slot decides (REAL-CODE period 28)
            for (size_t side = 0; side < 2; side++) {
                state.at(kPlayoffTeams + next + i + side) = uint8_t(winners[i + side]);
                state.at(kPlayoffWins + next + i + side) = 0;
                state.at(kPlayoffLive + next + i + side) = 0;
            }
        }
        state.at(kPlayoffTeams + 23) = uint8_t(series_.back().lower);  // the odd slots all go to slot 23 (DS:0144): the last lower seed stays
        state.at(kPlayoffLive + 23) = 0;
        next_stage++;
        next_game = 0;
        open = true;
    }
    const size_t end = size_t(kPlayoffBase[next_stage] + (8 >> next_stage));
    state.at(kPlayoffTeams + end) = 0xFF;
    state.at(kPlayoffStage) = uint8_t(next_stage);
    state.at(kPlayoffGame) = uint8_t(next_game);
    state.at(kPlayoffCursor) = uint8_t(kPlayoffBase[next_stage]);
    state.at(kPlayoffPending) = uint8_t(open || completed_days == 5);  // game index 4 always sets it, the final included (sub_3F1C9)
}

// ovl06_0c29: two clubs change places (players' team byte +0x1D, the managers' team, records).
void Game::reset_wm() {
    for (size_t g = 0; g < 2; g++) {
        wm_groups_[g] = RoundRobinTable<6>{};
        for (size_t t = 0; t < 6; t++) wm_groups_[g].teams[t] = kFirstNationalTeam + int(g * 6 + t);
    }
    wm_slots_.clear();
    wm_ranking_.clear();
    wm_round_ = 0;
    wm_champion_ = -1;
}

// ovl05_194b(1), one world championship day. Rounds 0-4 (DS:07AE) play the save's group pairings
// (DS:CED8, team indices); after round 4 the group orders (DS:CD88/CD8E) give the quarter-finals
// first of group 1 - fourth of group 2, second - third, third - second, fourth - first. Knock-out
// games cannot end in a draw; the winner moves to the even slot. The slot table DS:012C sends
// winners and losers of each round on: quarter-final losers play for places 5-8, semi-final
// losers for place 3, so round 7 holds the final and the games for places 3, 5 and 7. Pair k of
// round 7 decides places 2k+1 and 2k+2 (the per-nation history DS:828B, sixteen years; nations
// out in the groups get 11 there). The placement also determines the next trainer grant.
void Game::begin_world_championship() {
    if (national_coach_ >= 0 || wm_round_ > 0 || save_.state.at(0x025A) == 2) return;
    // ovl14_2e0f clears last season's achievement flags with a new WM appointment (below); its 0x40 achievement then survives
    // the following summer. Without an appointment the flags are cleared only when 13.04. is left (REAL-CODE period 57).
    if (std::getenv("EM_OLD_WMBEGIN")) save_.state.at(kManagerBlock + 0xF3) = 0;
    if (mgr_[0].team < 0 || mgr_[0].team >= 12 || team_rank(mgr_[0].team) >= 5) return;
    // ovl05_5239 appoints the first human manager in the top five, rather than
    // every qualifying manager. Other human managers in an imported save retain their claim.
    bool appointed = false;
    for (int place = 0; place < 5; place++) {
        int owner = 0;
        for (int team = 0; team < 12; team++)
            if (team_rank(team) == place) owner = save_.teams[size_t(team)].raw[0x16];
        if (owner && owner != 1) return;
        if (owner == 1) { appointed = true; break; }
    }
    if (!appointed) return;
    const int nation = save_.state.at(0x068C);
    if (nation < kFirstNationalTeam || nation >= kFirstNationalTeam + 12) return;
    national_coach_ = 0;
    const size_t block = kManagerBlock + 5 * 0x1AF;
    std::copy_n(save_.state.begin() + long(kManagerBlock), 20, save_.state.begin() + long(block));
    save_.state.at(block + 0x14) = uint8_t(nation);
    save_.state.at(block + 0xE4) = 0;
    save_.state.at(block + 0xEB) = 0;
    const long budget = int32_t(le32(save_.state, block + 0x18F)) + national_team_grant(save_);
    for (int byte = 0; byte < 4; byte++) save_.state.at(block + 0x18F + size_t(byte)) = uint8_t(uint32_t(budget) >> (byte * 8));
    save_.state.at(kManagerBlock + 0xF3) = 0x40;  // last season's flags cleared, the appointment mark set
    const size_t team = size_t(save_.teams[size_t(nation)].raw - save_.state.data());
    save_.state.at(team + 0x16) = 6;
    save_.state.at(team + 0x34) |= 0x80;
    save_.state.at(0x0788) = save_.state.at(0x0789) = 5;
    save_.state.at(0xA528) = 1;  // DS:0229: native WM match display
    auto_select_national_squad();
    log_.push_back("National team manager: " + save_.teams[size_t(nation)].name);
}

void Game::finish_world_championship() {
    // ovl14_2e0f returns the borrowed players before the season review. The
    // national account is retained; its placement grant is paid at the next appointment.
    release_national_squad(save_);
    if (national_coach_ >= 0) {
        const size_t at = size_t(save_.teams[size_t(national_team())].raw - save_.state.data());
        save_.state.at(at + 0x16) = 0;
    }
    national_coach_ = -1;
    save_.state.at(kManagerBlock + 5 * 0x1AF + 0x14) = 0xFD;
    save_.state.at(0x0788) = save_.state.at(0x0789) = 0;
}

bool Game::auto_select_national_squad() {
    if (national_coach_ < 0 || wm_round_ != 0) return false;
    auto_national_squad(save_, national_tables_, contracts_, brng_);
    refresh_national_team();
    return true;
}

bool Game::select_national_player(size_t player, bool selected) {
    if (national_coach_ < 0 || wm_round_ != 0) return false;
    const bool changed = selected ? call_up_national_player(save_, player, contracts_, brng_)
                                  : release_national_player(save_, player);
    if (changed) { auto_national_lineup(save_); refresh_national_team(); }
    return changed;
}

void Game::refresh_national_team() {
    if (national_coach_ < 0) return;
    const auto squad = national_squad();
    HumanSquad human{&squad, &save_.players};
    for(int kind=0;kind<2;kind++)human.per_line[kind]=line_counts_[5][size_t(kind)];
    human.level = finance_.level;
    HokTeam& team = save_.teams[size_t(national_team())];
    for (int kind = 0; kind < 2; kind++) {
        int skill1 = 0, skill2 = 0, form = 0;
        for (int line = 0; line < 3; line++) {
            const LineValues v = line_strength(team, human, line, kind, false, 0);
            skill1 += v.skill1; skill2 += v.skill2; form += v.form;
        }
        team.skill1[kind] = skill1 / 3;
        team.skill2[kind] = skill2 / 3;
        team.form[kind] = form / 3;
    }
}

// ovl14_2349 (ovr038:2349) for a goal of the human club's opponent: it books the goal for the first of the six managers
// whose club is the scorer. Unused manager blocks carry club 0, so a goal of team record 0 lands in manager 1's empty
// squad: without players no pick is drawn, the slot of the last scorer pick (byte_33277) gets the goal counters and the
// player record of that slot (index 0 = record 0) its +0x1B league goals (REAL-CODE period 7).
// With N managers the first unused block is N (blocks N..3 carry club 0); a manager club 0 is found first and picks for
// itself (its own on_chance). With four managers no manager block is unused: the pool party 4 is not treated (assumption).
void Game::credit_opponent_goal(int competition, bool goal, const HokTeam& scorer) {
    if (!goal || competition < 0 || competition > 2 || scorer.name != save_.teams[0].name || manager_team(0) || managers_ >= kMaxManagers) return;
    const size_t block = squad_block(managers_);  // the first unused manager block
    for (int s = 0; s < kHokSquadSlots; s++)
        if (save_.state.at(block + size_t(s) * kSquadRecord + 0x0F) != 0) return;  // a used block would draw a real pick
    const size_t rec = block + size_t(last_scorer_slot_) * kSquadRecord;
    if (competition == 0) {
        uint8_t& g = save_.state.at(0x4FA5 + size_t(save_.state.at(rec + 0x0F)) * 0x21 + 0x1B);
        if (g < 0xFF) g++;
    }
    const size_t at = rec + 0x22 + size_t(competition) * 2;
    const int value = le16(save_.state, at);
    if (value < 0xFF) { save_.state.at(at) = uint8_t(value + 1); save_.state.at(at + 1) = uint8_t((value + 1) >> 8); }
    uint8_t& b = save_.state.at(rec + 3 + size_t(competition));
    if (b < 0xFF) b++;
}

void Game::credit_human_goal(int competition, bool penalty, bool goal, const HokTeam* on_ice,
                             const std::vector<HokSquadPlayer>* working, int m) {
    auto add_assists = [&](const HokPlayer& player, int count) {
        const size_t at = size_t(player.raw - save_.state.data()) + 0x1F;
        const int value = le16(save_.state, at) + count;
        save_.state.at(at) = uint8_t(value);
        save_.state.at(at + 1) = uint8_t(value >> 8);
    };
    auto& squad = save_.squad_of(m);
    GoalCredit credit;
    if (working && working->size() == squad.size()) {
        // the pick sees the squad of the running match: a player in the penalty box has no role there (REAL-CODE period 8)
        std::vector<HokSquadPlayer> view = squad;
        for (size_t i = 0; i < view.size(); i++) view[i].role = (*working)[i].role;
        credit = credit_goal(view, save_.players, brng_, competition, penalty, goal, on_ice);
        for (size_t i = 0; i < view.size(); i++) {  // the goal counters go to the stored squad
            squad[i].league_goals = view[i].league_goals; squad[i].league_goals_byte = view[i].league_goals_byte;
            squad[i].cup_goals = view[i].cup_goals; squad[i].cup_goals_byte = view[i].cup_goals_byte;
            squad[i].other_goals = view[i].other_goals; squad[i].other_goals_byte = view[i].other_goals_byte;
        }
    } else {
        credit = credit_goal(squad, save_.players, brng_, competition, penalty, goal, on_ice);
    }
    if (credit.scorer >= 0) last_scorer_slot_ = credit.scorer;
    if (!goal || credit.scorer < 0) return;
    auto& scorer = save_.players[size_t(squad[size_t(credit.scorer)].player)];
    if (competition == 0 && !penalty) scorer.league_goals = std::min(255, scorer.league_goals + 1);
    // ovl14_2349 also credits this assist for a successful penalty shot.
    if (credit.assist >= 0) add_assists(save_.players[size_t(squad[size_t(credit.assist)].player)], 1);
}

void Game::book_club_match(int own_goals, int goals_against, int competition, bool league_statistics, int shootout_goals,
                           bool credit_goals, int m) {
    interactive_played_ = true;
    const bool cup = competition == 1;
    auto add_assists = [&](const HokPlayer& player, int count) {
        const size_t at = size_t(player.raw - save_.state.data()) + 0x1F;
        const int value = le16(save_.state, at) + count;
        save_.state.at(at) = uint8_t(value);
        save_.state.at(at + 1) = uint8_t(value >> 8);
    };
    for (int goal = 0; goal < own_goals; goal++) {
        const bool penalty = goal >= own_goals - shootout_goals;
        if (credit_goals || penalty) credit_human_goal(competition, penalty, true, nullptr, nullptr, m);  // minute-wise credited goals are not booked twice
    }
    bool goalkeeper_booked = false;
    for (size_t slot = 0; slot < save_.squad_of(m).size(); slot++) {
        auto& sp = save_.squad_of(m)[slot];
        if (sp.player >= 0) {
            save_.state.at(squad_block(m) + slot * kSquadRecord + 0x1A) &= 0x7F;
            sp.status = sp.role == 0 ? SquadStatus::None : SquadStatus::Reserve;
        }
        const bool hurt = slot < mgr_[m].hurt_in_match.size() && mgr_[m].hurt_in_match[slot];  // a player injured in the match started it
        if (slot < mgr_[m].hurt_in_match.size()) mgr_[m].hurt_in_match[slot] = false;
        if (sp.player < 0 || (sp.role == 0 && !hurt) || sp.line == 0) continue;
        auto& player = save_.players[size_t(sp.player)];
        if (competition == 2) {
            const size_t at = squad_block(m) + slot * kSquadRecord;
            save_.state.at(at + 8)++;
            const int games = le16(save_.state, at + 0x20) + 1;
            save_.state.at(at + 0x20) = uint8_t(games);
            save_.state.at(at + 0x21) = uint8_t(games >> 8);
        } else {
            int& season = cup ? sp.cup_games : sp.league_games;
            season++;
            // the season byte and the career total (+0x1C league / +0x1E cup) go up at once: the contract wish of the next
            // squad day reads the total (REAL-CODE period 36)
            const size_t at = squad_block(m) + slot * kSquadRecord;
            save_.state.at(at + (cup ? 7 : 6)) = uint8_t(season);
            const size_t total_at = at + (cup ? 0x1E : 0x1C);
            const int games = le16(save_.state, total_at) + 1;
            save_.state.at(total_at) = uint8_t(games);
            save_.state.at(total_at + 1) = uint8_t(games >> 8);
        }
        sp.status = SquadStatus::Team;
        if (!league_statistics) continue;
        player.league_games = uint8_t(player.league_games + 1);
        if (player.position == 0 && !goalkeeper_booked) {
            sp.goals_against = sp.league_goals_byte = std::min(255, sp.league_goals_byte + goals_against);
            add_assists(player, 60);  // a goalkeeper's +0x1F stores minutes, not assists
            goalkeeper_booked = true;
        }
    }
}

bool Game::book_forfeit(const MatchStats& stats, int manager) {
    if (stats.forfeit_side == 0) return false;
    interactive_played_ = true;
    const long fine = long(int64_t(200000) * finance_.money_num / finance_.money_den);
    if (manager < kMaxManagers) mgr_[size_t(manager)].budget.balance -= fine;  // the national coach (5) has no ManagerBudget
    else {
        const size_t account = kManagerBlock + size_t(manager) * 0x1AF + 0x18F;
        const uint32_t balance = le32(save_.state, account) - uint32_t(fine);
        for (int byte = 0; byte < 4; byte++) save_.state.at(account + size_t(byte)) = uint8_t(balance >> (8 * byte));
    }
    save_.state.at(kManagerBlock + size_t(manager) * 0x1AF + 0xF0) = 100;
    log_.push_back("forfeit: fewer than nine selected players, 0:2, fine " + std::to_string(fine / 3) + " DM");
    return true;
}

void Game::book_match_condition(const MatchStats& stats, int side, int manager) {
    if (stats.initial_load[side] >= 0)
        save_.state.at(kManagerBlock + size_t(manager) * 0x1AF + 0xF0) = uint8_t(stats.initial_load[side]);
    const int team = manager < kMaxManagers ? mgr_[size_t(manager)].team : national_team();
    const HokTeam& played_team = stats.final_teams[size_t(side)];
    if (played_team.raw && team >= 0 && team < kHokTeams) {
        HokTeam& stored = save_.teams[size_t(team)];
        stored.flags = played_team.flags;
        stored.effort_current = played_team.effort_current;
        stored.effort_base = played_team.effort_base;
        stored.offence_base = played_team.offence_base;
        stored.skaters = played_team.skaters;
        for (int kind = 0; kind < 2; kind++) {
            stored.current_line[kind] = played_team.current_line[kind];
            stored.shift[kind] = played_team.shift[kind];
            for (int line = 0; line < 4; line++) stored.energy[kind][line] = played_team.energy[kind][line];
        }
        for (int situation = 0; situation < kTacticSituations; situation++) {
            stored.effort[situation] = played_team.effort[situation];
            stored.offence[situation] = played_team.offence[situation];
        }
    }
    const auto& squad = stats.final_squads[size_t(side)];
    for (size_t slot = 0; slot < squad.size(); slot++) {
        const auto& played = squad[slot];
        if (played.player < 0) continue;
        const size_t at = kSquad + size_t(manager) * 0x618 + slot * kSquadRecord;
        save_.state.at(at + 0x13) = uint8_t(played.raw_exhaustion);
        save_.state.at(at + 0x1B) = uint8_t(played.fitness);
        save_.state.at(at + 0x00) = uint8_t(played.major_penalties);
        save_.state.at(at + 0x01) = uint8_t(played.minor_penalties);
        if (manager < kMaxManagers) {
            auto& stored = save_.squad_of(manager).at(slot);
            stored.raw_exhaustion = played.raw_exhaustion;
            stored.exhaustion = played.exhaustion;
            stored.fitness = played.fitness;
            stored.major_penalties = played.major_penalties;
            stored.minor_penalties = played.minor_penalties;
        }
    }
}

void Game::book_national_match(int own_goals, const MatchStats& stats, int side, int shootout_goals) {
    interactive_played_ = true;
    if (book_forfeit(stats, 5)) return;
    book_match_condition(stats, side, 5);
    book_special_teams(stats, side, 5);
    auto squad = national_squad();
    constexpr size_t block = kSquad + 5 * 0x618;
    auto increment16 = [&](size_t at) {
        const int value = le16(save_.state, at) + 1;
        save_.state.at(at) = uint8_t(value);
        save_.state.at(at + 1) = uint8_t(value >> 8);
    };
    for (int goal = 0; goal < own_goals; goal++) {
        const bool penalty = goal >= own_goals - shootout_goals;
        const GoalCredit credit = credit_goal(squad, save_.players, brng_, 0, penalty);
        if (credit.scorer < 0) continue;
        HokPlayer& scorer = save_.players[size_t(squad[size_t(credit.scorer)].player)];
        if (!penalty) scorer.league_goals = std::min(scorer.league_goals + 1, 255);
        const size_t at = block + size_t(credit.scorer) * kSquadRecord;
        save_.state.at(at + 3) = uint8_t(squad[size_t(credit.scorer)].league_goals_byte);
        if (!penalty && le16(save_.state, at + 0x22) < 255) increment16(at + 0x22);
        if (credit.assist >= 0) {
            const HokPlayer& assist = save_.players[size_t(squad[size_t(credit.assist)].player)];
            increment16(size_t(assist.raw - save_.state.data()) + 0x1F);
        }
    }
    for (size_t slot = 0; slot < squad.size(); slot++) {
        const auto& sp = squad[slot];
        if (sp.player < 0) continue;
        const size_t at = block + slot * kSquadRecord;
        save_.state.at(at + 0x1A) &= 0x7F;
        if (sp.role == 0 || sp.line == 0) continue;
        save_.state.at(at + 8)++;
        increment16(at + 0x20);
        save_.state.at(at + 0x1A) |= 0x80;
    }
}

void Game::play_wm_day() {
    if (save_.state.at(0x025A) == 2) return;  // native league-only mode has no WM
    if (wm_round_ >= 8) return;
    if (wm_round_ == 0) begin_world_championship();
    if (std::getenv("EM_DEBUG_DAYFLOW")) std::fprintf(stderr, "dayflow: WM round %d, national coach %d\n", wm_round_, national_coach_);
    // a WM day rests the squads of the managers who are not national coach like a EuroCup day without them: fitness from
    // the load; the squad routine of the WM branch trains afterwards (REAL-CODE Köln period 37: after the last WM day the
    // fitness stands one load step higher than the formula of the final load)
    if (!std::getenv("EM_OLD_WMREST"))
        for (int m = 0; m < managers_; m++) {
            if (m == national_coach_) continue;
            auto& squad = save_.squad_of(m);
            for (size_t slot = 0; slot < squad.size() && slot < size_t(kHokSquadSlots); slot++) {
                const size_t at = squad_block(m) + slot * kSquadRecord;
                if (save_.state.at(at + 0x0F) == 0) continue;
                const uint8_t f = rested_fitness(save_.state.at(at + 0x13));
                save_.state.at(at + 0x1B) = f;
                squad[slot].fitness = f;
            }
        }
    // ovr038:3649: without a human national coach (manager 5 club 0xFD) the first WM day rolls the strengths of the own
    // nation (DS:06AC): per kind skill1 rand(84,94), skill2 rand(79,89), form rand(45,55); the manager index DS:A70E/A70D
    // becomes 5 (REAL-CODE period 30)
    const bool quick = national_coach_ < 0;
    if (wm_round_ == 0 && quick) {
        const int own = save_.state.at(0x068C);
        if (own >= kFirstNationalTeam && own < kFirstNationalTeam + 12) {
            HokTeam& t = save_.teams[size_t(own)];
            for (int kind = 0; kind < 2; kind++) {
                t.skill1[kind] = brng_.range(84, 94);
                t.skill2[kind] = brng_.range(79, 89);
                t.form[kind] = brng_.range(45, 55);
            }
        }
        save_.state.at(0x0788) = 5;  // DS:A70E (DS:A70D is not saved)
    }
    refresh_national_team();
    const auto national = national_squad();
    HumanSquad squad{&national, &save_.players};
    for(int kind=0;kind<2;kind++)squad.per_line[kind]=line_counts_[5][size_t(kind)];
    squad.minimum_players = 9;
    squad.level = finance_.level;
    squad.selection_rng = &brng_;
    squad.rotation = &national_rotation_;
    squad.timers = &national_timers_;
    configure_human_tactics(squad, save_, 5);
    if (national_coach_ >= 0) squad.penalty_effort_adjustment = penalty_effort_adjustment(save_, national_team());
    const int nation = national_team();
    // FUN_162c_0417(0) counts every nominee once per WM day, even when the
    // nation has already gone out in the groups and only other teams are playing.
    if (national_coach_ >= 0)
        for (const auto& sp : national) {
            if (sp.player < 0) continue;
            const size_t player = size_t(save_.players[size_t(sp.player)].raw - save_.state.data());
            if (save_.state.at(player + 0x19) != 255) save_.state.at(player + 0x19)++;
        }
    const int r = wm_round_++;
    struct QuickResult { int home, away, diff; };
    std::vector<QuickResult> quick_results;
    // the table booking of the quick games: morale +0x17 by rand(0, 2|diff|), clamped 47..53, home then away (as in the league)
    auto book_morale = [&] {
        for (const QuickResult& q : quick_results)
            for (auto [team, dd] : {std::pair<int, int>{q.home, q.diff}, {q.away, -q.diff}}) {
                const int step = int(brng_.range(0, 2 * std::abs(dd)));
                save_.teams[size_t(team)].bonus = std::clamp(save_.teams[size_t(team)].bonus + (dd > 0 ? step : -step), 47, 53);
            }
    };
    if (r < 5) {
        for (size_t g = 0; g < 2; g++)
            for (size_t pair = 0; pair < 3; pair++) {
                const HokPairing& p = save_.wm_rounds[g][size_t(r)][pair];
                MatchStats stats;
                MatchResult res;
                if (quick) {  // no human nation: ovl06_161d quick path for every pair (REAL-CODE period 30)
                    HokTeam h, a;
                    quick_setup(p.home, p.away, h, a);
                    res = quick_competition_match(h, a, false, false, rng_, 1, false, false, true);  // level 1: the away roll (1,2) is drawn (REAL-CODE)
                    quick_results.push_back({p.home, p.away, res.home - res.away});
                    update_series(p.home, 0, res.home, res.away);  // series and records of the national teams (REAL-CODE period 30)
                    update_series(p.away, 1, res.away, res.home);
                    update_records(p.home, p.away, res.home, res.away);
                    // the head-to-head table of manager 0 is booked from the own nation's view (DS:06AC, REAL-CODE period 30)
                    book_head_to_head(save_.state, p.home, p.away, res.home, res.away, save_.state.at(0x068C));
                } else
                    res = league_match(save_.teams[size_t(p.home)], save_.teams[size_t(p.away)], rng_, 0,
                                       p.home == nation ? &squad : nullptr, p.away == nation ? &squad : nullptr, &stats);
                if (p.home == nation || p.away == nation)
                    book_national_match(p.home == nation ? res.home : res.away, stats, p.home == nation ? 0 : 1);
                wm_groups_[g].book(p.home, p.away, res);
                const size_t o = kGroupResults + g * 0x24 + size_t(r) * 6 + pair * 2;
                save_.state.at(o) = uint8_t(res.home);
                save_.state.at(o + 1) = uint8_t(res.away);
            }
        book_morale();
        for (auto& group : wm_groups_) group.finish_round();
        if (r == 4) {
            const auto a = wm_groups_[0].order(), b = wm_groups_[1].order();
            wm_slots_.assign(24, -1);
            std::string line = "World championship quarter-finals:";
            for (size_t i = 0; i < 4; i++) {
                wm_slots_[2 * i] = wm_groups_[0].teams[size_t(a[i])];
                wm_slots_[2 * i + 1] = wm_groups_[1].teams[size_t(b[3 - i])];
                line += " " + save_.teams[size_t(wm_slots_[2 * i])].name + " - " +
                        save_.teams[size_t(wm_slots_[2 * i + 1])].name + ";";
            }
            log_.push_back(line);
        }
        return;
    }
    if (r > 7 || wm_slots_.empty()) return;
    // DS:012C: destination slot (1-based) of the team in slot i of rounds 5 and 6
    static constexpr std::array<int, 16> kNextSlot = {9,  13, 10, 14, 11, 15, 12, 16,
                                                      17, 19, 18, 20, 21, 23, 22, 24};
    const size_t base = size_t(r - 5) * 8;
    std::vector<HokPairing> draw;
    for (size_t i = base; i < base + 8; i += 2) draw.push_back({wm_slots_[i], wm_slots_[i + 1]});
    MatchStats stats;
    std::vector<Tie> ties;
    if (quick) {  // no human nation: the quick path for the knock-out games as well (REAL-CODE period 30)
        // as in the play-offs (sub_40683): after each game every pair of the day is scanned for a level score in the result
        // grid; the pairs not played yet are 0:0 and get the mark 30, and any level pair gives the current game a Borland
        // shoot-out rand(0,2) pairs (+30 on its home byte); no morale booking
        std::vector<std::array<int, 2>> grid(draw.size(), {0, 0});
        for (size_t index = 0; index < draw.size(); index++) {
            const HokPairing& p = draw[index];
            HokTeam h, a;
            quick_setup(p.home, p.away, h, a);
            MatchResult res = quick_competition_match(h, a, false, false, rng_, 1, false, false, true);
            grid[index] = {res.home, res.away};
            bool tie = false;
            for (auto& g : grid)
                if (g[0] == g[1]) { tie = true; g[0] += 30; }
            if (tie) {
                int c = 0, e = 0;
                do {
                    c = brng_.range(0, 2);
                    e = brng_.range(0, 2);
                } while (grid[index][0] + c == grid[index][1] + e);
                grid[index][0] += c + 30;
                grid[index][1] += e;
            }
            res.decision_marker = (grid[index][0] / 30) * 30;
            res.home = grid[index][0] - res.decision_marker;
            res.away = grid[index][1];
            res.shootout = tie;
            ties.push_back({p.home, p.away, res});
        }
    } else
        ties = play_cup_round(save_.teams, draw, rng_, HumanSide{nation, &squad, &stats});
    bool nation_played = false;
    for (size_t k = 0; k < ties.size(); k++) {
        const Tie& t = ties[k];
        if (t.home == nation || t.away == nation) {
            nation_played = true;
            book_national_match(t.home == nation ? t.result.home : t.result.away, stats, t.home == nation ? 0 : 1,
                                t.result.shootout_goals[t.home == nation ? 0 : 1]);
        }
        const int loser = t.winner() == t.home ? t.away : t.home;
        wm_slots_[base + 2 * k] = t.winner();
        wm_slots_[base + 2 * k + 1] = loser;
        // Native knock-out slots are winner first; the scores follow that same order.
        const size_t o = kWorldResults + base + 2 * k;
        save_.state.at(o) = uint8_t(std::max(t.result.home, t.result.away) + t.result.decision_marker);
        save_.state.at(o + 1) = uint8_t(std::min(t.result.home, t.result.away));
    }
    if (national_coach_ >= 0 && !nation_played)
        for (size_t slot = 0; slot < national.size(); slot++)
            // FUN_162c_0417 also prepares nominees of an eliminated nation. No
            // minute loop runs for them: neither the raw_E clamp nor interval rest.
            save_.state.at(kSquad + 5 * 0x618 + slot * kSquadRecord + 0x1B) =
                uint8_t(std::min(90, 100 - national[slot].raw_exhaustion / 8));
    if (r < 7) {
        for (size_t i = base; i < base + 8; i++) wm_slots_[size_t(kNextSlot[i] - 1)] = wm_slots_[i];
        return;
    }
    wm_ranking_.assign(wm_slots_.begin() + 16, wm_slots_.end());
    wm_champion_ = wm_ranking_[0];
    // ovl05_194b: the champion's zero-terminated history determines the shared year
    // column. Keep the last sixteen tournaments; nations out in the groups get 11.
    const auto first = save_.state.begin() + long(kWorldHistory + size_t(wm_champion_) * 0x50);
    size_t year = size_t(std::find(first, first + 16, uint8_t(0)) - first);
    if (year == 16) {
        for (size_t team = kFirstNationalTeam; team < kFirstNationalTeam + 12; team++) {
            auto history = save_.state.begin() + long(kWorldHistory + team * 0x50);
            std::move(history + 1, history + 16, history);
        }
        year = 15;
    }
    for (size_t team = kFirstNationalTeam; team < kFirstNationalTeam + 12; team++)
        save_.state.at(kWorldHistory + team * 0x50 + year) = 11;
    for (size_t place = 0; place < wm_ranking_.size(); place++)
        save_.state.at(kWorldHistory + size_t(wm_ranking_[place]) * 0x50 + year) = uint8_t(place + 1);
    char line[160];
    std::snprintf(line, sizeof line, "World championship: %s beats %s %d:%d", save_.teams[size_t(ties[0].home)].name.c_str(),
                  save_.teams[size_t(ties[0].away)].name.c_str(), ties[0].result.home, ties[0].result.away);
    log_.push_back(line);
    log_.push_back("World champion: " + save_.teams[size_t(wm_champion_)].name);
    finish_world_championship();
}

// ovl16_0480(mode), the end of the season change: every free player (owner +0x1A = 6) of a range
// whose team is a computer club gets a random team and a new name from that team's roster in
// KB/23.vga. Ranges (DS:55F0 + mode * 6; player indices here are one lower than in EM.EXE):
//   mode 0: players 178-198 to the national teams 52-63
//   mode 1: players 0-178 to the clubs 0-23 (first and second division)
// The roster slot comes from DS:55F0[c] .. DS:55F1[c] - 1, c counting the range markers (mode 0:
// 183 and 190, mode 1: 20 and 96): slots 0-1 (goalkeepers), 2-11 (defence), 12-24 (attack);
// one draw in 16 takes slot 0-1 instead. A vacant slot or a name already held by a player that
// keeps it (not free, or handled earlier in this pass) is drawn again, team and slot. The 20
// copied bytes also set +0x13 (foreigner) from the byte after the name. All draws use Borland
// rand. The original checks DS:06AC (a manager's nation) as well; only club managers exist here.
void Game::redistribute_players(int mode) {
    if (db_.clubs.empty()) return;
    struct Range {
        int first, end, marker1, marker2, team_lo, team_hi;
    };
    static constexpr Range kRanges[2] = {{178, 199, 183, 190, 52, 63}, {0, 179, 20, 96, 0, 23}};
    static constexpr int kSlotFrom[3] = {0, 2, 12}, kSlotTo[3] = {1, 11, 24};  // DS:55F0..55F3
    const Range& r = kRanges[mode];
    // record number of a player (decode_hok skips vacant records, so it is not the vector index)
    auto record = [&](const HokPlayer& pl) { return int((pl.raw - save_.state.data() - kPlayers) / kPlayerRecord); };
    auto raw = [&](const HokPlayer& pl) { return save_.state.data() + (pl.raw - save_.state.data()); };
    auto db_club = [&](int team) -> const DbClub* {  // ovl16_0214: by name
        for (const DbClub& c : db_.clubs)
            if (c.name == save_.teams[size_t(team)].name) return &c;
        return nullptr;
    };
    int c = 0, renamed = 0;
    for (HokPlayer& pl : save_.players) {
        const int p = record(pl);
        if (p < r.first || p >= r.end) continue;
        c = p >= r.marker2 ? 2 : p >= r.marker1 ? 1 : 0;
        if (raw(pl)[0x1A] != 6) continue;
        if (pl.team >= 0 && pl.team == mgr_[0].team) continue;  // a manager's club keeps its players
        for (;;) {
            const int team = brng_.range(r.team_lo, r.team_hi);
            // ovr040:0585: the own nation (DS:06AC) gets no new players while no manager holds it (+0x16 = 0): redraw
            if (team == save_.state.at(0x068C) && save_.teams[size_t(team)].raw && save_.teams[size_t(team)].raw[0x16] == 0) continue;
            int slot = brng_.range(kSlotFrom[c], kSlotTo[c]);
            if (brng_.range(0, 15) == 0) slot = brng_.range(0, 1);
            const DbClub* club = db_club(team);
            if (!club || size_t(slot) >= club->players.size() || club->players[size_t(slot)].name.empty()) continue;
            const DbPlayer& name = club->players[size_t(slot)];
            bool taken = false;
            for (const HokPlayer& other : save_.players) {
                const int q = record(other);
                if (q >= r.first && q < r.end && other.name == name.name && (raw(other)[0x1A] != 6 || q < p)) taken = true;
            }
            if (taken) continue;
            std::copy(name.copy.begin(), name.copy.end(), raw(pl));
            pl.name = name.name;
            pl.foreigner = name.copy[19] != 0;
            pl.team = team;
            renamed++;
            break;
        }
    }
    log_.push_back(std::to_string(renamed) + (mode ? " club players" : " national players") + " redistributed");
}

// ovl11_34ef, the transfer list (squad block 4, slots 0-9 of DS:5DC3 + 4 * 0x618).
//   1. Every slot is offered to ovl09_25d6(slot, 0): a listed player (+0x1A = 4) leaves the list
//      (+0x1A = 6) and the slots behind him move up one place; the loop still goes on with the
//      next slot number, so of a full list every second player goes. A slot with an unlisted
//      player stays and counts as kept.
//   2. 10 - kept times a slot rand(0, 9) is drawn: a human-owned pool record is protected;
//      an empty or ordinary listed slot is replaced (no candidate in 1500
//      tries ends the refresh). He gets form rand(45, 55) and is inserted by ovl09_2cd1 behind the
//      last listed player of his class (goalkeeper, defence, forward; 1000:0180): record zeroed,
//      +0x0F player + 1, +0x13 = rand(80, 120), ovl11_307a (+0x0E = rand(35, 65), +0x14 =
//      rand(1, 13) | rand(1, 3) << 4 | rand(0, 1) << 7), games and goals from the player record,
//      skills and form. Then +0x1A = 4, value +0x2C and salary +0x28 from ovl11_3d36 and the
//      nibbles +0x19 = rand(3, 12) << 4 | rand(3, 12).
// A candidate of a human club would first move to a computer club of the same class
// (ovl11_381e); club players of managers are never free (+0x1A = 0..3), so that is left out.
void Game::refresh_transfer_list() {
    constexpr size_t kBlock = kSquad + 4 * 0x618;
    constexpr int kSlots = kTransferRefreshSlots;
    uint8_t* st = save_.state.data();
    auto rec = [&](int slot) { return st + kBlock + size_t(slot) * kSquadRecord; };
    auto player = [&](int exe_index) -> HokPlayer* {  // EXE index = HOK record + 1
        for (HokPlayer& pl : save_.players)
            if (pl.raw == st + kPlayers + size_t(exe_index - 1) * kPlayerRecord) return &pl;
        return nullptr;
    };
    auto owner = [&](int exe_index) -> uint8_t& { return st[kPlayers + size_t(exe_index - 1) * kPlayerRecord + 0x1A]; };
    auto remove = [&](int slot) {  // ovl09_25d6: shift through slot 10, clear only its player ID
        std::memmove(rec(slot), rec(slot + 1), size_t(kSlots - slot) * kSquadRecord);
        rec(kSlots)[0x0F] = 0;
    };
    auto pclass = [&](int exe_index) {  // 1000:0180
        const int pos = st[kPlayers + size_t(exe_index - 1) * kPlayerRecord + 0x18];
        return pos == 0 ? 0 : pos < 60 ? 1 : 2;
    };
    int kept = 0, dropped = 0, added = 0;
    for (int slot = 0; slot < kSlots; slot++) {
        const int p = rec(slot)[0x0F];
        if (p == 0) continue;
        if (owner(p) == 4) {
            remove(slot);
            owner(p) = 6;
            dropped++;
        } else {
            kept++;
        }
    }
    for (int k = 0; k < kSlots - kept; k++) {
        const int s = brng_.range(0, kSlots - 1);
        const int old = rec(s)[0x0F];
        if (old && owner(old) != 4) continue;  // preserve human-owned pool players
        if (old) {
            remove(s);
            owner(old) = 6;
        }
        const int p = pick_transfer_candidate(brng_, [&](int q) { return owner(q) == 6; });
        if (p < 0) break;
        HokPlayer* pl = player(p);
        if (!pl) break;
        pl->form = brng_.range(45, 55);
        uint8_t* pr = st + kPlayers + size_t(p - 1) * kPlayerRecord;
        pr[0x17] = uint8_t(pl->form);
        {  // ovr035:381E (sub_5678E): the skills are derived anew from the attack strengths of the player's club; a club of the
           // national teams narrow the random part to rand(10,14), the others rand(0,14) (REAL-CODE)
            const int club = pr[0x1D];
            if (club >= 0 && club < kHokTeams && save_.teams[size_t(club)].raw[0x16] == 0) {
                const HokTeam& tm = save_.teams[size_t(club)];
                const int league_first = kFirstNationalTeam;  // DS:06AD (byte_2567D) = 52: the national teams
                const int lo = club >= league_first && club < league_first + 12 ? 10 : 0;
                if (std::getenv("EM_DEBUG_REFRESH")) std::fprintf(stderr, "refresh: player %d club %d league_first %d lo %d\n", p, club, league_first, lo);
                int a = tm.skill1[1] + int(brng_.range(lo, 14)) - 10;
                a = std::clamp(a, 10, 99);
                int b = tm.skill2[1] + int(brng_.range(lo, 14)) - 10;
                if (brng_.range(0, 4) == 0) b += int(brng_.range(12, 20));
                b = std::clamp(b, 10, 99);
                pr[0x15] = uint8_t(a);
                pr[0x16] = uint8_t(b);
                pl->skill_a = a;
                pl->skill_b = b;
            }
        }
        // ovl09_2cd1: behind the last entry of the same class
        int at = 0;
        while (at < kSlots && rec(at)[0x0F] && pclass(rec(at)[0x0F]) <= pclass(p)) at++;
        if (rec(kSlots)[0x0F]) continue;
        if (rec(at)[0x0F] && at < kSlots)
            std::memmove(rec(at + 1), rec(at), size_t(kSlots - at) * kSquadRecord);
        uint8_t* r = rec(at);
        std::memset(r, 0, kSquadRecord);
        r[0x0F] = uint8_t(p);
        r[0x13] = uint8_t(brng_.range(80, 120));
        r[0x0E] = uint8_t(brng_.range(35, 65));  // ovl11_307a
        int n = brng_.range(1, 13);
        n |= brng_.range(1, 3) << 4;
        n |= brng_.range(0, 1) << 7;
        r[0x14] = uint8_t(n);
        r[0x03] = uint8_t(pl->league_goals);  // +0x1B / +0x1C of the player: the model holds the counts of the day (the raw bytes follow on save)
        r[0x06] = uint8_t(pl->league_games);
        r[0x10] = pr[0x15];
        r[0x11] = pr[0x16];
        r[0x12] = pr[0x17];
        owner(p) = 4;
        const ContractPlayer c = contract_player(save_, 4, at);
        const long value = player_worth(contracts_, c, Worth::Value, brng_);
        const long salary = player_worth(contracts_, c, Worth::Salary, brng_);
        for (int b = 0; b < 4; b++) {
            r[0x2C + b] = uint8_t(value >> (8 * b));
            r[0x28 + b] = uint8_t(salary >> (8 * b));
        }
        const int hi = brng_.range(3, 12);
        r[0x19] = uint8_t(hi << 4 | brng_.range(3, 12));
        added++;
    }
    log_.push_back("transfer list: " + std::to_string(dropped) + " players gone, " + std::to_string(added) + " new");
}

// ovl13_2f93 -> ovl13_22b8 for every game of manager 0's club: the result goes into his head-to-head
// block (state 0x8F88 = DS:AB7B, 4 managers x 0x280 bytes, 10 per opponent team index): five pairs
// (home game, away game), each byte goals against << 4 | own goals, 0xFF = empty. The first free
// slot of the column is taken; with all five used the column moves up one slot and the new result
// goes to the last. Opponents from DS:06AD (52) on use both columns as one list of ten. The
// opponent info screen (ovl13_0095) shows these results and its records (highest win and so on).
void Game::record_head_to_head(int home, int away, int home_goals, int away_goals, int m) {
    book_head_to_head(save_.state, home, away, home_goals, away_goals, mgr_[m].team, m);
}

// ovl13_3e43, the club records of the opponent info (ovl13_12e5), from ovl13_2f93 after league and
// cup games (DS:D0C2 = 7 / 8) for every club below 64. Per team (state 0xA11C = DS:BD0F, 8 x u16;
// opponents at 0x9F1C = DS:BB0F, 8 bytes):
//   0 highest home win, 1 highest home defeat, 2 home goals scored, 3 home goals conceded,
//   4 highest away win, 5 highest away defeat, 6 away goals scored, 7 away goals conceded.
// A record keeps (a * wa) << 8 | b * wb of the game (a = home goals, b = away goals) and is replaced
// when a * wa - b * wb beats the stored value, compared as unsigned 16-bit numbers (records 1 and 4
// look for the smallest difference and so never replace an empty 0:0 - in the original saves they
// stay 0:0, the screen says "Noch keine Eintragung"). wb < 0 is stored as 1.
void Game::update_records(int home, int away, int home_goals, int away_goals) {
    book_records(save_.state, home, away, home_goals, away_goals);
}

void Game::swap_teams(int a, int b) {
    if (std::getenv("EM_DEBUG_SWAP")) std::fprintf(stderr, "swap %d %d\n", a, b);
    HokTeam& ta = save_.teams[size_t(a)];
    HokTeam& tb = save_.teams[size_t(b)];
    // the raw 61-byte records change places too (unparsed bytes such as the manager slot +0x16
    // travel with the club); the raw pointers stay with the index position
    uint8_t* ra = save_.state.data() + (ta.raw - save_.state.data());
    uint8_t* rb = save_.state.data() + (tb.raw - save_.state.data());
    std::swap_ranges(ra, ra + kTeamRecord, rb);
    std::swap(ta, tb);
    std::swap(ta.raw, tb.raw);
    if (a < kSeriesTeams && b < kSeriesTeams) {  // the club records too
        uint8_t* s = save_.state.data();
        std::swap_ranges(s + kRecords + size_t(a) * 16, s + kRecords + size_t(a) * 16 + 16, s + kRecords + size_t(b) * 16);
        std::swap_ranges(s + kRecordOpponents + size_t(a) * 8, s + kRecordOpponents + size_t(a) * 8 + 8,
                         s + kRecordOpponents + size_t(b) * 8);
        // the opponents of all records are slot numbers and follow the swap as well (REAL-CODE period 30)
        for (size_t i = 0; i < size_t(kSeriesTeams) * 8; i++) {
            uint8_t& v = s[kRecordOpponents + i];
            if (v == a) v = uint8_t(b);
            else if (v == b) v = uint8_t(a);
        }
    }
    if (a < kSeriesTeams && b < kSeriesTeams)  // the head-to-head rows travel with the club too
        for (size_t m = 0; m < 4; m++) {  // the four human managers; the series follow at 0x9988
            uint8_t* base = save_.state.data() + kHeadToHead + m * 0x280;
            std::swap_ranges(base + size_t(a) * 10, base + size_t(a) * 10 + 10, base + size_t(b) * 10);
        }
    if (a < kSeriesTeams && b < kSeriesTeams) {  // the series travel with the club
        uint8_t* sa = save_.state.data() + kSeries + size_t(a) * 0x15;
        uint8_t* sb = save_.state.data() + kSeries + size_t(b) * 0x15;
        std::swap_ranges(sa, sa + 0x15, sb);
    }
    for (HokPlayer& p : save_.players) {
        // ovr030:0C29 (sub_43AB9) reads and writes the native byte +0x1D: other routines (the annual redistribution) change
        // it directly, so the decoded team may be stale (REAL-CODE Köln period 37: player 13 ended in the wrong club)
        uint8_t& native = save_.state.at(size_t(p.raw - save_.state.data()) + 0x1D);
        int team = std::getenv("EM_OLD_SWAPTEAM") ? p.team : (native == 255 ? -1 : native);
        if (team == a)
            team = b;
        else if (team == b)
            team = a;
        p.team = team;
        // Contract renewal and player redistribution read the native byte
        // before the next export, just as ovl06_0c29 updates it immediately.
        native = uint8_t(team);
    }
    if (a >= kFirstNationalTeam && a < kFirstNationalTeam + 12 && b >= kFirstNationalTeam && b < kFirstNationalTeam + 12) {
        // sub_4382D also swaps the u32 per nation at DS:026C (state 0x658, REAL-CODE period 30)
        uint8_t* n = save_.state.data() + 0x658;
        std::swap_ranges(n + size_t(a - kFirstNationalTeam) * 4, n + size_t(a - kFirstNationalTeam) * 4 + 4, n + size_t(b - kFirstNationalTeam) * 4);
    }
    if (a < 64 && b < 64) {
        // the whole 0x50-byte record DS:8257 travels with the club: titles, table row with its copies, last position,
        // curve / WM history and the eternal table (REAL-CODE period 30: the curves, the copies of the relegated clubs
        // in the regional rows and +0x4C of the national teams all follow the swaps)
        uint8_t* s = save_.state.data();
        std::swap_ranges(s + kTitles + size_t(a) * 0x50, s + kTitles + size_t(a) * 0x50 + 0x50, s + kTitles + size_t(b) * 0x50);
    }
    if (save_.state.at(0x068C) == a) save_.state.at(0x068C) = uint8_t(b);
    else if (save_.state.at(0x068C) == b) save_.state.at(0x068C) = uint8_t(a);
    for (int* t : {&mgr_[0].team, &champion_, &euro_champion_, &cup_winner_}) {  // titles stay with the club
        if (*t == a)
            *t = b;
        else if (*t == b)
            *t = a;
    }
    save_.state.at(kManagerBlock + 0x14) = uint8_t(mgr_[0].team);
}

void Game::new_season() {
    const int previous_division = mgr_[0].team >= 0 ? mgr_[0].team / kHokLeagueSize : 3;
    CareerSeason history;
    // overview page "Saisonende" (ovl12_1657): titles by name before the clubs change places
    SeasonReview review;
    {
        const std::array<int, 4> titles{wm_champion_, cup_winner_, champion_, euro_champion_};
        for (size_t k = 0; k < 4; k++) {
            const int t = titles[k];
            if (t >= 0 && t < int(save_.teams.size())) review.winners[k] = save_.teams[size_t(t)].name;
            review.human_winner[k] = t >= 0 && t == mgr_[0].team;
        }
    }
    auto team_name = [&](int t) { return t >= 0 && t < int(save_.teams.size()) ? save_.teams[size_t(t)].name : std::string(); };
    history.league = std::clamp(mgr_[0].budget.league, 0, 2);
    history.place = mgr_[0].team < 36
        ? leagues_[size_t(mgr_[0].team / 12)].rows[size_t(mgr_[0].team % 12)].position + 1
        : save_.table.at(size_t(mgr_[0].team)).position + 1;
    if (history.league == 0 && history.place < 9) {
        history.place = 53;
        int slot = 13;
        for (; slot > 7; slot--) {
            if (slot == 11) history.place--;
            if (save_.state.at(kPlayoffTeams + size_t(slot)) == mgr_[0].team) break;
        }
        if (slot == 7) history.place--;
    }
    history.champion = champion_ == mgr_[0].team;
    history.cup_round = save_.state.at(kManagerBlock + 0xE6);
    history.cup_winner = cup_winner_ == mgr_[0].team;
    history.euro_phase = save_.state.at(kManagerBlock + 0xE7);
    history.euro_winner = euro_champion_ == mgr_[0].team && history.euro_phase != 0 && history.euro_phase != 30;
    append_career_season(save_.state, 0, le16(save_.state, kCareerYear), history);
    // ovl14 clears the annual attendance totals before the new season's games.
    mgr_[0].budget.home_games = 0;
    mgr_[0].budget.attendance_total = mgr_[0].budget.attendance_record = 0;
    mgr_[0].budget.attendance_min = 99999;
    save_.state.at(kManagerBlock + 0xED) = 0;
    for (size_t at = kManagerBlock + 0x183; at < kManagerBlock + 0x18F; at++) save_.state.at(at) = 0;
    for (int byte = 0; byte < 4; byte++)
        save_.state.at(kManagerBlock + 0x18B + size_t(byte)) = uint8_t(99999u >> (byte * 8));
    // ovl01_0000(1) runs before the club indices and player statistics change.
    std::vector<SeasonScorer> scorers;
    for (size_t i = 0; i < save_.players.size(); i++) {
        const auto& pl = save_.players[i];
        int goals = pl.league_goals;
        for (const auto& sp : save_.squad)
            if (sp.player == int(i)) { goals = sp.league_goals_byte; break; }
        const size_t raw = size_t(pl.raw - save_.state.data());
        scorers.push_back({pl.team, pl.position, goals, int(le16(save_.state, raw + 0x1F)), pl.league_games});
    }
    const auto scoring_winners = season_scoring_winners(scorers, previous_division * 12,
                                                         previous_division * 12 + 11);
    const bool scorer_award = scoring_winners[0] == mgr_[0].team;
    const bool goalkeeper_award = scoring_winners[1] == mgr_[0].team;
    // Native annual flags are rebuilt before licensing and promotion. The two
    // title flags exempt a financially weak club from license withdrawal.
    uint8_t season_flags = uint8_t((save_.state.at(kManagerBlock + 0xF3) & 0x40) |
                                   (champion_ == mgr_[0].team ? 0x08 : 0) |
                                   (cup_winner_ == mgr_[0].team ? 0x10 : 0) |
                                   (history.euro_winner ? 0x20 : 0));
    save_.state.at(kManagerBlock + 0xF3) = season_flags;
    if (champion_ == mgr_[0].team) save_.state.at(kManagerBlock + 0x15)++;
    if (history.euro_winner) save_.state.at(kManagerBlock + 0x17)++;
    if (cup_winner_ == mgr_[0].team) {
        // ovl14:3e43: booked before the strict -2,000,000 license threshold.
        mgr_[0].budget.balance += kCupPrize;
        save_.state.at(kManagerBlock + 0x16)++;
        log_.push_back("cup prize money " + std::to_string(kCupPrize / 3) + " DM");
    }
    // Return temporarily called players before any club/nation index is shuffled.
    release_national_squad(save_);
    if (national_coach_ >= 0) {
        const size_t nation = size_t(save_.state.at(0x068C));
        const size_t at = size_t(save_.teams[nation].raw - save_.state.data());
        save_.state.at(at + 0x16) = 0;
        save_.state.at(at + 0x34) &= 0x7F;
    }
    national_coach_ = -1;
    save_.state.at(0x0788) = save_.state.at(0x0789) = 0;
    save_.state.at(kManagerBlock + 5 * 0x1AF + 0x14) = 0xFD;
    // the manager's eternal balance (manager block +0x13B, state 0x0F08): u16 pairs (home, away) of
    // points won, goals scored, goals conceded, points lost, games, wins, losses and draws; his club's
    // league season is added at the season change (NEUSAIS: 37/16, 86/61, 40/95, 7/28, 22/22, 17/6,
    // 2/12, 3/4 = the 1993/94 season of Köln)
    if (mgr_[0].team >= 0 && mgr_[0].team < 3 * kHokLeagueSize) {
        const HokTableRow& r = leagues_[size_t(mgr_[0].team / kHokLeagueSize)].rows[size_t(mgr_[0].team % kHokLeagueSize)];
        for (int side = 0; side < 2; side++) {
            const int d = r.draws(side);
            const int add[8] = {2 * r.wins[side] + d, r.goals_for[side], r.goals_against[side], 2 * r.losses[side] + d,
                                r.games[side], r.wins[side], r.losses[side], d};
            for (int k = 0; k < 8; k++) {
                uint8_t* p = save_.state.data() + kManagerBalance + size_t(k) * 4 + size_t(side) * 2;
                const int v = (p[0] | p[1] << 8) + add[k];
                p[0] = uint8_t(v);
                p[1] = uint8_t(v >> 8);
            }
        }
    }
    // final positions of all 48 table rows (the regional rows keep what the save had)
    std::array<int, kHokLeagueTeams> final_pos{};
    for (int t = 0; t < kHokLeagueTeams; t++)
        final_pos[size_t(t)] = t < 3 * kHokLeagueSize ? leagues_[size_t(t / kHokLeagueSize)].rows[size_t(t % kHokLeagueSize)].position
                                                       : save_.table[size_t(t)].position;
    std::array<int, kHokLeagueTeams> origin{};  // which old index each index holds after the swaps below
    for (int t = 0; t < kHokLeagueTeams; t++) origin[size_t(t)] = t;
    // the table rows of the last season (DS:8257 records): they travel with their club through every swap below
    std::array<HokTableRow, kHokLeagueTeams> old_rows{};
    for (int t = 0; t < kHokLeagueTeams; t++)
        old_rows[size_t(t)] = t < 3 * kHokLeagueSize ? leagues_[size_t(t / kHokLeagueSize)].rows[size_t(t % kHokLeagueSize)] : save_.table[size_t(t)];
    auto swap_clubs = [&](int a, int b) {
        swap_teams(a, b);
        std::swap(origin[size_t(a)], origin[size_t(b)]);
    };
    if (financial_license_due(mgr_[0].budget.balance, previous_division, season_flags, 0)) {
        const size_t group = size_t(previous_division);
        auto work = relegation_[group].teams;
        const auto order = relegation_[group].order();
        insert_financial_relegation(work, order, previous_division, mgr_[0].team, 0);
        for (size_t slot = 0; slot < work.size(); slot++) {
            // DS:CF20 is the first working schedule row, not DS:CFFC's
            // immutable list of the original relegation participants.
            save_.state.at(kGroupSchedule + 0x48 + group * 0x24 + slot) = uint8_t(work[slot]);
            relegation_upper_[group][slot] = work[size_t(order[slot])];
        }
        season_flags |= 0x04;
        log_.push_back("financial license withdrawn: " + save_.teams[size_t(mgr_[0].team)].name);
    }
    // promotion and relegation: the first two of each relegation group play in the upper division
    for (int g = 0; g < 2; g++) {
        const int upper_first = g * kHokLeagueSize;
        std::vector<int> up, down;  // lower teams going up, upper teams going down
        for (int i = 0; i < 4; i++) {
            const int team = relegation_upper_[size_t(g)][size_t(i)];
            const bool upper = team >= upper_first && team < upper_first + kHokLeagueSize;
            if (i < 2 && !upper) up.push_back(team);
            if (i >= 2 && upper) down.push_back(team);
        }
        for (size_t k = 0; k < up.size() && k < down.size(); k++) {
            log_.push_back("promoted: " + save_.teams[size_t(up[k])].name + ", relegated: " + save_.teams[size_t(down[k])].name);
            review.promoted_into[size_t(g)].push_back(team_name(up[k]));
            review.relegated_into[size_t(g + 1)].push_back(team_name(down[k]));
            swap_clubs(up[k], down[k]);
        }
    }
    // Oberliga and regional league (ovl14_2e0f, league 2): the last two of the Oberliga (DS:07B9
    // = 2; 12th first, from the table order DS:5CCB) go down without a relegation round, each
    // changing places with a regional club rand(36, 41) (Borland rand, DS:07C2 + DS:07B3 .. +5;
    // the club of the previous draw is drawn again). Regional clubs 42-47 never come up.
    {
        const League& ol = leagues_[2];
        std::array<int, kHokLeagueSize> by_place{};
        for (int s = 0; s < kHokLeagueSize; s++) by_place[size_t(ol.rows[size_t(s)].position)] = ol.first_team + s;
        int previous = -1;
        for (int k = 0; k < 2; k++) {
            const int down = by_place[size_t(kHokLeagueSize - 1 - k)];
            int up;
            do up = brng_.range(kFirstRegionalTeam, kFirstRegionalTeam + 5);
            while (up == previous);
            previous = up;
            review.promoted_into[2].push_back(team_name(up));
            if (mgr_[0].team == down && (season_flags & 0x04) == 0) {
                // ovl14:440d..4467: a human Oberliga loser receives a
                // replacement balance and an extra full club swap. The
                // common swap below cancels it; both season flags remain.
                season_flags |= 0x02;
                mgr_[0].budget.balance = 1200000;
                swap_clubs(down, up);
                log_.push_back("Oberliga relegation review: account reset to 400000 DM");
            } else {
                log_.push_back("promoted from the regional league: " + save_.teams[size_t(up)].name +
                               ", relegated: " + save_.teams[size_t(down)].name);
            }
            if (mgr_[0].team == up && (season_flags & 0x04) == 0) season_flags |= 0x01;
            swap_clubs(down, up);
        }
    }
    if ((season_flags & 0x04) == 0) {
        const int division = mgr_[0].team / kHokLeagueSize;
        if (division < previous_division) season_flags |= 0x01;
        if (division > previous_division) season_flags |= 0x02;
    }
    save_.state.at(kManagerBlock + 0xF3) = season_flags;
    // ovl14_47ea, the eternal table (u32 per table row, travels with the club): after promotion and
    // relegation every club 0-41 gets 12 - final position (0 = first) + 20 in the first division
    // + 20 in the first or second division of its NEW class (nothing above team 35, DS:07CB),
    // + 15 for the champion (DS:D0E6), + 25 for the cup winner (DS:0387), + 40 for the European cup
    // winner (DS:0388), + 10 for the champion in the first division or + 3 for the first of a lower
    // division. SAISEND -> NEUSAIS: 48 of 48 values.
    for (int u = 0; u < 42; u++) {
        const int pos = final_pos[size_t(origin[size_t(u)])];
        int v = 12 - pos + (u < kHokLeagueSize ? 20 : 0) + (u < 2 * kHokLeagueSize ? 20 : 0);
        if (champion_ == u) v += 15;
        if (u > 35) v = 0;
        if (cup_winner_ == u) v += 25;
        if (euro_champion_ == u) v += 40;
        if (u < kHokLeagueSize) {
            if (champion_ == u) v += 10;
        } else if (pos == 0 && u <= 35) {
            v += 3;
        }
        // the title columns of the eternal table: champion, DEB cup, European cup (row bytes 0-2)
        uint8_t* titles = save_.state.data() + kTitles + size_t(u) * 0x50;
        if (champion_ == u) titles[0]++;
        if (cup_winner_ == u) titles[1]++;
        if (euro_champion_ == u) titles[2]++;
        uint8_t* e = save_.state.data() + kEternal + size_t(u) * 0x50;
        const uint32_t x = uint32_t(e[0] | e[1] << 8 | e[2] << 16 | uint32_t(e[3]) << 24) + uint32_t(v);
        for (int k = 0; k < 4; k++) e[k] = uint8_t(x >> (8 * k));
    }
    // 162c:1c5b: new season values of the German clubs in their new class, before the clubs change places (REAL-CODE
    // period 30: the 52 rerates follow the regional swaps directly)
    for (int t = 0; t < kFirstNationalTeam; t++) {
        const HokTeam before = save_.teams[size_t(t)];
        rerate_team(save_.teams[size_t(t)], team_class(finance_, t), rating_, rng_);
        if (std::getenv("EM_DEBUG_RERATE")) {
            const HokTeam& x = save_.teams[size_t(t)];
            std::fprintf(stderr, "rerate %d%s class %d: %d %d %d %d -> %d %d %d %d\n", t, t == mgr_[0].team ? " (human)" : "", team_class(finance_, t),
                         before.skill1[0], before.skill1[1], before.skill2[0], before.skill2[1], x.skill1[0], x.skill1[1], x.skill2[0], x.skill2[1]);
        }
    }
    for (int t = kFirstNationalTeam; t < 64; t++) {  // the national teams only re-roll their form (as in the weekly drift)
        save_.teams[size_t(t)].form[0] = int(rng_.range(45, 55));
        save_.teams[size_t(t)].form[1] = int(rng_.range(45, 55));
        save_.teams[size_t(t)].energy[0][0] = int(rng_.range(45, 55));
    }
    // ovl06_099d: the clubs of each division change places at random (55 swaps per division,
    // which reshuffles the schedule), the national teams 20 times
    // (REAL-CODE period 30: the national teams first)
    for (int i = 0; i < 20; i++) {
        const int a = int(rng_.range(kFirstNationalTeam, kFirstNationalTeam + 11));
        const int b = int(rng_.range(kFirstNationalTeam, kFirstNationalTeam + 11));
        if (a != b) swap_teams(a, b);
    }
    for (int l = 0; l < 3; l++)
        for (int i = 0; i < 55; i++) {
            const int a = int(rng_.range(l * kHokLeagueSize, l * kHokLeagueSize + kHokLeagueSize - 1));
            const int b = int(rng_.range(l * kHokLeagueSize, l * kHokLeagueSize + kHokLeagueSize - 1));
            if (a != b) {
                swap_clubs(a, b);
                // the table order DS:5CCB holds slot numbers; only these swaps rewrite them (REAL-CODE period 30)
                for (int& slot : leagues_[size_t(l)].ranking)
                    if (slot == a - l * kHokLeagueSize) slot = b - l * kHokLeagueSize;
                    else if (slot == b - l * kHokLeagueSize) slot = a - l * kHokLeagueSize;
            }
        }
    // European cup entrants and groups (ovl05_51a5, ovl05_4c7b(1), ovl05_0ef7(2)): the champion
    // and a German European cup winner join six or seven drawn European clubs; the group
    // pairings follow the letter table for four teams like the saved ones
    std::vector<int> german;
    if (champion_ >= 0) german.push_back(champion_);
    if (euro_champion_ >= 0 && euro_champion_ < kSeriesTeams && euro_champion_ != champion_)
        german.push_back(euro_champion_);
    uint8_t* st = save_.state.data();
    st[kStateChampion] = uint8_t(std::max(champion_, 0));
    if (cup_winner_ >= 0) st[kStateCupWinner] = uint8_t(cup_winner_);
    if (euro_champion_ >= 0) st[kStateEuroWinner] = uint8_t(euro_champion_);
    std::fill(st + kStateEntrants, st + kStateEntrants + 10, uint8_t(0));
    std::fill(st + kStateEntrantSlots, st + kStateEntrantSlots + 18, uint8_t(0x80));
    for (size_t i = 0; i < german.size(); i++) st[kStateEntrants + i] = st[kStateEntrantSlots + i] = uint8_t(german[i]);
    // prizes of the season review (ovl02_0000)
    const int league = team_class(finance_, mgr_[0].team);
    // ovl04_0000(0) checks the fewest total league penalty minutes after club
    // swaps. The first club index wins an exact tie; tables have not reset yet.
    int fair_play_winner = league * 12;
    auto penalty_minutes = [&](int team) {
        const int old = origin.at(size_t(team));
        return old < 36 ? leagues_[size_t(old / 12)].rows[size_t(old % 12)].penalty_minutes
                        : save_.table.at(size_t(old)).penalty_minutes;
    };
    for (int team = league * 12 + 1; team < (league + 1) * 12; team++)
        if (penalty_minutes(team) < penalty_minutes(fair_play_winner)) fair_play_winner = team;
    const bool promoted = (season_flags & 3) == 1;
    const long prize = season_prizes(promoted, league, scorer_award, goalkeeper_award,
                                     fair_play_winner == mgr_[0].team);
    // The manager's page of the season review (ovl02_0000): history line, symbols and prizes; the header shows the
    // balance before the review prizes (ovl01_0f6a runs first).
    review.date = date_of_season_day(int(calendar_.events.size()), save_.year);  // the closing day, 30.04.
    review.balance = mgr_[0].budget.balance;
    const size_t review_row = kManagerBlock + 0x18 + 4 * size_t(std::min<int>(le16(save_.state, kCareerYear), 49));
    review.place = save_.state.at(review_row);
    review.league = save_.state.at(review_row + 1);
    for (int bit = 0; bit < 7; bit++)
        if (season_flags & (1 << bit)) review.icons.push_back(kSeasonFlagIcons[bit]);
    if (scorer_award) review.icons.push_back(kIconScorer);
    if (goalkeeper_award) review.icons.push_back(kIconGoalkeeper);
    if (fair_play_winner == mgr_[0].team) review.icons.push_back(kIconFairPlay);
    if (std::find(german.begin(), german.end(), mgr_[0].team) != german.end()) review.icons.push_back(kIconEuroEntrant);
    review.prizes = (cup_winner_ == mgr_[0].team ? kCupPrize : 0) + prize;
    if (promoted)
        for (auto& advert : mgr_[0].budget.adverts) advert.flags |= 0x80;
    if (prize) {
        mgr_[0].budget.balance += prize;
        log_.push_back("prize money " + std::to_string(prize / 3) + " DM");
    }
    // 5396:076B precedes the review: intake uses the mixed junior quality.
    youth_new_season(save_.state,0);
    if(save_.state.at(0x025a)!=1){
        const ContractRandomDraw draw=[&](int low,int high){return brng_.range(low,high);};
        const int attempts=youth_review_attempts(date_of_season_day(day_+1,save_.year).year,draw);
        for(int attempt=0;attempt<attempts;++attempt){
            const auto intake=youth_intake(save_,contracts_,0,save_.state.at(0x06ba),draw);
            if(!intake)continue;
            review.icons.push_back(kIconYouth);
            HokPlayer* player=player_by_exe_index(intake->player);
            if(!player)throw FormatError("Season: youth player ID missing");
            player->age=player->raw[0x14];player->skill_a=player->raw[0x15];
            player->skill_b=player->raw[0x16];player->form=player->raw[0x17];
            player->team=player->raw[0x1d];
            if(intake->shifted)
                for(size_t slot=mgr_[0].injured_role.size()-1;slot>size_t(intake->slot);--slot)
                    mgr_[0].injured_role[slot]=mgr_[0].injured_role[slot-1];
            mgr_[0].injured_role[size_t(intake->slot)]=0;
            save_.squad=hok_squad(save_,0);
            log_.push_back(player->name+" joins from the youth team");
        }
        refresh_budget_roster();
    }
    if (mgr_[0].team >= 0) season_review_ = std::move(review);
    // 4184 first pass: byte birthdays and contract decrement precede the
    // second-pass retirement gates. Zero-year contracts wrap to255.
    for(int id=1;id<=kHokPlayers;++id){
        HokPlayer* player=player_by_exe_index(id);
        if(!player)continue;
        const size_t global=size_t(player->raw-save_.state.data());
        player->age=save_.state.at(global+0x14)=uint8_t(player->age+1);
        native_clear_player_notification(save_,uint8_t(id));
        if(const auto location=native_find_roster_player(save_,uint8_t(id))){
            const size_t row=kSquad+size_t(location->manager)*0x618+size_t(location->slot)*kSquadRecord;
            save_.state.at(row+9)&=0x3f;
            save_.state.at(row+0x0b)=uint8_t(save_.state.at(row+0x0b)-1);
        }
    }
    save_.squad=hok_squad(save_,0);
    // ovl02_0000 visits global player IDs in order and returns human-owned
    // records from the pool before retirement/expiry review. They retain the
    // full contract/insurance record and must not enter the CPU aging branch.
    // The ordinary listing block and a temporary loan marker do not survive
    // this annual return. Their already shortened contract is copied intact.
    for (int id = 1; id <= kHokPlayers; id++) {
        const HokPlayer* owned = player_by_exe_index(id);
        if (!owned || owned->raw[0x1A] != 0) continue;
        for (int slot = 0; slot < kTransferListSlots; slot++) {
            const size_t record = kSquad + 4 * 0x618 + size_t(slot) * kSquadRecord;
            if (save_.state.at(record + 15) != id) continue;
            save_.state.at(record + 9) &= 0x3F;
            save_.state.at(record + 0x18) &= 0x7F;
            save_.state.at(record + 0x0C) = 0;
            if (move_owned_player(size_t(slot), false, true) != TransferMoveResult::Moved)
                throw FormatError("Season: unable to return own pool player");
            break;
        }
    }
    // 4184 second pass is global-ID ordered, with a fresh storage lookup after
    // each removal. Every healthy own row consumes the age-threshold draw.
    int retired = 0;
    const ContractRandomDraw annual_draw=[&](int low,int high){return brng_.range(low,high);};
    for (size_t p = 0; p < save_.players.size(); p++) {
        HokPlayer& pl = save_.players[p];
        const int id=int((pl.raw-save_.state.data()-kPlayers)/kPlayerRecord)+1;
        pl.league_goals = pl.league_games = 0;
        uint8_t* raw=save_.state.data()+(pl.raw-save_.state.data());
        raw[0x1b]=raw[0x1c]=raw[0x1f]=raw[0x20]=0;
        std::string native_name;for(size_t k=0;k<19&&raw[k];k++)native_name+=char(raw[k]);
        const int age_before=raw[0x14];
        const auto review=annual_owned_player(save_,uint8_t(id),0,annual_draw);
        if(review.status==AnnualPlayerStatus::Retired||review.status==AnnualPlayerStatus::Disabled){
            // ovr026:07F7..09F3: shown at once in the message field and deleted again (src/game/messages.cpp)
            // the review is dated with the next day (REAL-CODE Köln period 37: "27. April 1994" on season day 216 + summer)
            const GameDate d=date_of_season_day(std::getenv("EM_OLD_FLASHMSG")?day_:day_+1,save_.year);
            const MessageDate md{d.day,d.month,d.year,day_+1};
            if(review.status==AnnualPlayerStatus::Retired)
                immediate_messages_.push_back(native_message_text(review.notice_back,md,MessageKind::Retirement,{MessageArg::str(native_name),MessageArg::num(age_before)}));
            else if(review.insurance_payment==0)
                immediate_messages_.push_back(native_message_text(review.notice_back,md,MessageKind::Invalid,{MessageArg::str(native_name)}));
            else
                immediate_messages_.push_back(native_message_text(review.notice_back,md,MessageKind::TwoLines,{MessageArg::str(native_name+" ist invalide."),
                    MessageArg::str("Ein Fall f]r die Versicherung: Sie zahlt Ihnen "+std::to_string(review.insurance_payment/3)+" DM")}));
            // the text stays in the first free message entry (REAL-CODE Köln period 37: LUPZIG ist Sportinvalide)
            if(!std::getenv("EM_OLD_FLASHMSG"))native_flash_message(save_.state,"%%%"+immediate_messages_.back());  // "%%%": the message field's marker
            mgr_[0].budget.balance+=review.insurance_payment;
            if(review.insurance_payment)
                log_.push_back(pl.name+" disability insurance payment "+std::to_string(review.insurance_payment/3)+" DM");
            log_.push_back(pl.name+(review.status==AnnualPlayerStatus::Disabled?" leaves after permanent injury":" retires from ice hockey"));
            std::move(mgr_[0].injured_role.begin()+review.slot+1,mgr_[0].injured_role.end(),mgr_[0].injured_role.begin()+review.slot);
            mgr_[0].injured_role.back()=0;
            ++retired;
        }else if(review.status==AnnualPlayerStatus::Missing&&pl.team>=0&&player_retires(pl.age,brng_)){
            // The no-row CPU/BIOS edge is still an approximation; keep its
            // existing age-based replacement separate from proven own rows.
            const NewPlayer n = new_player(brng_);
            raw[0x14]=uint8_t(n.age);raw[0x15]=uint8_t(n.skill1);raw[0x16]=uint8_t(n.skill2);
            raw[0x19]=0;
            retired++;
        }
        pl.age=raw[0x14];pl.skill_a=raw[0x15];pl.skill_b=raw[0x16];
    }
    save_.squad=hok_squad(save_,0);
    for (size_t i = 0; i < save_.squad.size(); i++) {
        HokSquadPlayer& sp = save_.squad[i];
        uint8_t* r = save_.state.data() + kSquad + i * kSquadRecord;
        r[0x09] &= 0x3F;  // offer flags
        // the season bytes start again; the u16 totals of games (+0x1C/+0x1E) and goals (+0x22/+0x24/+0x26) stay
        // (REAL-CODE period 30)
        sp.league_games = sp.cup_games = 0;
        r[0x06] = r[0x07] = 0;  // so that the export leaves the u16 copies alone
        sp.goals_against = sp.league_goals_byte = sp.cup_goals_byte = sp.other_goals_byte = 0;
        sp.major_penalties = sp.minor_penalties = 0;
        r[0x00] = r[0x01] = 0;
        // ovl02_0000 clears +0x00, +0x01 and +0x03..+0x08 in the raw records before the contract dialogs, whose salary
        // formula reads the goals and penalties (REAL-CODE period 58)
        if (!std::getenv("EM_OLD_CONTRACTS")) r[0x03] = r[0x04] = r[0x05] = r[0x08] = 0;
    }
    log_.push_back(std::to_string(retired) + " players retired");
    mgr_[0].transfer_offers.clear();  // the offer flags are cleared above (ovl02_0000)
    // power play counters (manager block +0x177) start from 0 again (NEUSAIS); the series records stay
    std::fill(save_.state.begin() + long(kSpecialTeams), save_.state.begin() + long(kSpecialTeams + 12), uint8_t(0));
    // the season change sorts the messages once before the contract talks (REAL-CODE period 92: the injury of 13.04. stands
    // first, the two contract wishes behind it are deleted by the talks; the exact call site in ovr026 is not located)
    if (!std::getenv("EM_OLD_WMEXIT")) native_sort_messages(save_.state);
    debug_pclub(save_.state, "before-expire");
    expire_contracts();
    redistribute_players(0);
    redistribute_players(1);
    debug_pclub(save_.state, "redistribute");
    mgr_[0].budget.league = team_class(finance_, mgr_[0].team);
    mgr_[0].budget.team = mgr_[0].team;
    refresh_budget_roster();
    // ovr032:16B7 reads skill1 + skill2 of the club record at each booking. Open (REAL-CODE period 30): on 31.08. the
    // original's record gives 256, the rerated record here 253 (6 bytes of finances, docs/DAYDIFF.md Stand 51).
    const auto& summer_team = save_.teams.at(size_t(mgr_[0].team));
    mgr_[0].budget.team_strength = summer_team.skill1[0] + summer_team.skill1[1] + summer_team.skill2[0] + summer_team.skill2[1];
    // ovl14:462c..470b: May 1 (234) through September 9 (365).
    // There are no matches, transfer offers or normal training in this loop.
    for (int summer_day = 234; summer_day <= 365; summer_day++) {
        const GameDate summer_date = date_of_season_day(summer_day, save_.year);
        if (summer_day % 7 == 0) stadium_week(finance_, mgr_[0].budget, save_.state, 0, brng_, [&](const std::string& a, const std::string& b) {
            native_add_message(save_.state, brng_, {summer_date.day, summer_date.month, summer_date.year, summer_day}, MessageKind::TwoLines, {MessageArg::str(a), MessageArg::str(b)}, 0, true);
        });
        const int month_days = kMonthDays[size_t(summer_date.month - 1)];
        stadium_daily_refusal_reset(stadium_refusals_,0,brng_);
        // the share market runs on every summer day as well, directly after the refusal reset (REAL-CODE period 30)
        market_day(save_.state, brng_);
        for (int c = 0; c < kHokShares; c++) {
            save_.finance.shares[c] = le32(save_.state, 0x30 + size_t(c) * 8 + 4);
            for (int e = 0; e < kHokShareHistory; e++) save_.finance.share_price[c][e] = le16(save_.state, 0xB0 + size_t(c) * 72 + size_t(e) * 2) / 3;
        }
        if (summer_date.day == month_days / 2 || summer_date.day == month_days) {
            // ovl08_1d61 calls 2ba7 with the stored image. Unlike the
            // playable daily preparation, summer does not recompute it.
            update_sponsors();
            // the talent scout price coin of ovr032:1F0B follows in the summer too (REAL-CODE period 30)
            if (brng_.range(0, 1) != 0) {
                const uint32_t price = uint32_t(60000 + 10000 * brng_.range(0, 10)) * 3;
                for (int n = 0; n < 4; n++) save_.state.at(0x18 + size_t(n)) = uint8_t(price >> (8 * n));
            }
        }
        if (summer_date.day == month_days) clear_insurance_locks();
        if (std::getenv("EM_DEBUG_SUMMER") && summer_date.day == month_days) {
            long ads = 0;
            for (const AdvertContract& a : mgr_[0].budget.adverts) ads += a.amount;
            std::fprintf(stderr, "summer %02d.%02d.: strength %d players %d adverts %ld balance %ld accrued %ld human %d\n", summer_date.day, summer_date.month,
                         int(mgr_[0].budget.team_strength), int(mgr_[0].budget.players), ads, mgr_[0].budget.balance, mgr_[0].budget.accrued, mgr_[0].team);
            for (int t = 0; t < 36; t++) {
                const HokTeam& x = save_.teams[size_t(t)];
                const int sum = x.skill1[0] + x.skill1[1] + x.skill2[0] + x.skill2[1];
                if (sum >= 250 && sum <= 262) std::fprintf(stderr, "  team %d (%s) sum %d\n", t, x.name.c_str(), sum);
            }
        }
        finance_day(finance_, mgr_[0].budget, summer_date.day, kMonthDays[size_t(summer_date.month - 1)]);
        heal_squad(summer_day % 7 == 0);
    }
    // a fresh season: tables, schedule position, cup draw, competitions
    for (int l = 0; l < 3; l++) {
        League& lg = leagues_[size_t(l)];
        lg.first_team = l * kHokLeagueSize;
        for (int s = 0; s < kHokLeagueSize; s++) {
            // REAL-CODE period 30: the last position (+0x31) and the curve of the last season stay with the club, and the
            // table order keeps the old (shuffled) order
            const HokTableRow& old = old_rows[size_t(origin[size_t(lg.first_team + s)])];
            lg.rows[size_t(s)] = HokTableRow{};
            lg.rows[size_t(s)].form = "--------";
            lg.rows[size_t(s)].position = old.position;
            std::copy(std::begin(old.position_history), std::end(old.position_history), std::begin(lg.rows[size_t(s)].position_history));
        }
        matchday_[size_t(l)] = 1;
    }
    // The regional rows are reset the same way. The current fields of all 48 rows are reset in the records too, so the
    // export leaves the copies (+0x05/06, +0x1B..+0x20, +0x2B, +0x2F) that travelled with the club; the form copy of the
    // records 36..63 becomes "--------" (REAL-CODE period 30).
    for (int t = 3 * kHokLeagueSize; t < kHokLeagueTeams; t++) {
        const HokTableRow& old = old_rows[size_t(origin[size_t(t)])];
        HokTableRow& row = save_.table[size_t(t)];
        row = HokTableRow{};
        row.form = "--------";
        row.position = old.position;
        std::copy(std::begin(old.position_history), std::end(old.position_history), std::begin(row.position_history));
    }
    for (int t = 0; t < 64; t++) {
        uint8_t* r = save_.state.data() + kTitles + size_t(t) * 0x50 + 7;  // the table row (hok.cpp kTable)
        if (t < kHokLeagueTeams) {
            r[-4] = r[-3] = 0;
            std::fill(r, r + 8, uint8_t('-'));
            for (int side = 0; side < 2; side++) r[0x12 + side] = r[0x16 + side] = r[0x1A + side] = r[0x22 + side] = r[0x26 + side] = 0;
            r[0x43] = r[0x44] = 0;
        }
        if (t >= 3 * kHokLeagueSize) std::fill(r + 9, r + 17, uint8_t('-'));
    }
    // (the initial cup draw follows the EuroCup draw at the end of the season change, see below)
    // Native manager +0xE6: 1 = first-round participant, 30 = not in the draw.
    for (size_t manager = 0; manager < 6; manager++)
        save_.state.at(kManagerBlock + manager * 0x1AF + 0xE6) = manager == 0 ? 1 : 30;
    cup_day_ = 0;
    cup_round_ = 1;
    store_cup_participants(save_, cup_draw_, cup_round_);
    cup_game_ = 0;
    cup_series_.clear();
    std::fill(save_.state.begin() + long(kCupGame), save_.state.begin() + long(kCupWins + 32), uint8_t(0));
    save_.state.at(kCupPending) = 1;
    save_.state.at(0x8F84) = 0;  // DS:07AD: 0 after the season change (REAL-CODE period 30)
    cup_winner_ = -1;
    euro_round_ = 0;
    save_.state.at(kEuroRound) = 0;
    euro_champion_ = -1;
    playoff_day_ = 0;
    champion_ = -1;
    reset_wm();
    std::fill(save_.state.begin() + long(kGroupResults), save_.state.begin() + long(kGroupResults + 0x48), uint8_t(0));
    std::fill(save_.state.begin() + long(kWorldResults), save_.state.begin() + long(kWorldResults + 24), uint8_t(0));
    std::fill(save_.state.begin() + long(kWorldSlots), save_.state.begin() + long(kWorldSlots + 24), uint8_t(0xFF));
    save_.state.at(kWorldCursor) = 0;
    series_.clear();
    relegation_ = {};
    relegation_upper_ = {};
    // REAL-CODE period 30: the season change clears the play-off scores and the game index only; the wins, the play-off
    // teams, the pending flag DS:D0E5 and the relegation results stay from the last season
    std::fill(save_.state.begin() + long(kPlayoffScores), save_.state.begin() + long(kPlayoffWins), uint8_t(0));
    save_.state.at(kPlayoffGame) = 0;
    save_.state.at(kPlayoffStage) = 0;
    save_.state.at(kPlayoffCursor) = 0xFF;
    std::fill(save_.state.begin() + long(kLeagueResults), save_.state.begin() + long(kLeagueResults + 0x630), uint8_t(0));
    mgr_[0].budget.league = team_class(finance_, mgr_[0].team);
    mgr_[0].budget.team = mgr_[0].team;
    save_.year++;
    const uint16_t career_year = uint16_t(le16(save_.state, kCareerYear) + 1);
    save_.state.at(kCareerYear) = uint8_t(career_year);
    save_.state.at(kCareerYear + 1) = uint8_t(career_year >> 8);
    save_.season_year = save_.year;  // DS:03BB follows at the season change (NEUSAIS)
    // REAL-CODE period 30: the EuroCup groups and then the first DEB cup round are drawn after the summer, before the
    // annual skill and distribution draws (the original asks whether the cup draw is shown live in between)
    save_.euro_groups = draw_euro_groups(save_.teams, german, brng_);
    // ovl05_4c7b(1,0) resets all six manager phase bytes before drawing.
    for (size_t manager = 0; manager < 6; manager++)
        save_.state.at(kManagerBlock + manager * 0x1AF + 0xE7) = 30;
    euro_final_ = GroupTable{};
    std::fill(save_.state.begin() + long(kGroupResults + 2 * 0x48),
              save_.state.begin() + long(kGroupResults + 3 * 0x48), uint8_t(0));
    for (size_t g = 0; g < 2; g++) {
        euro_groups_[g] = GroupTable{save_.euro_groups[g]};
        for (int r = 0; r < 6; r++) {
            const std::vector<Pairing> pairs = round_robin_pairings(schedule_.teams4, r);
            for (size_t p = 0; p < 2 && p < pairs.size(); p++)
                save_.euro_rounds[g][size_t(r)][p] = {save_.euro_groups[g][size_t(pairs[p].home)],
                                                      save_.euro_groups[g][size_t(pairs[p].away)]};
        }
        store_group_schedule(save_.state, euro_groups_[g], 2, int(g), schedule_);
    }
    std::string draw = "European cup groups:";
    for (const auto& grp : save_.euro_groups) {
        for (int t : grp) draw += " " + save_.teams[size_t(t)].name + ";";
        draw += " /";
    }
    log_.push_back(draw);
    cup_draw_ = draw_initial_cup({mgr_[0].team}, brng_);
    store_cup_participants(save_, cup_draw_, cup_round_);
    // 6AF8:47A1 ->146E:179E ->1238:13A1: all398 global skill draws
    // happen after review, redistribution, summer and competition resets.
    annual_global_skills(save_.state,annual_draw);
    for(auto& player:save_.players){player.skill_a=player.raw[0x15];player.skill_b=player.raw[0x16];}
    // The export copy supplies current modeled CPU ratings and manager leagues
    // without flushing unrelated working bytes. The native allocator changes
    // only global club IDs; names, owner, squad and salary remain untouched.
    auto distribution=decode_hok(save_bytes());
    annual_distribute_players(distribution.state,annual_distribution_cursors_,annual_draw);
    for(auto& player:save_.players){
        const size_t global=size_t(player.raw-save_.state.data());
        const uint8_t club=distribution.state.at(global+0x1d);
        player.team=club==255?-1:int(club);save_.state.at(global+0x1d)=club;
    }
    seasons_++;
    day_ = 0;
    // REAL-CODE period 30: after the distribution the original computes the club image and renews the sponsor offers,
    // then books September 10 like a playable evening (refusal reset, share market, finances, image).
    debug_pclub(save_.state, "after-distribution");
    mgr_[0].image = current_club_image();
    update_sponsors();
    // The first playable menu already includes September 10's booking.
    stadium_daily_refusal_reset(stadium_refusals_,0,brng_);
    market_day(save_.state, brng_);
    for (int c = 0; c < kHokShares; c++) {
        save_.finance.shares[c] = le32(save_.state, 0x30 + size_t(c) * 8 + 4);
        for (int e = 0; e < kHokShareHistory; e++) save_.finance.share_price[c][e] = le16(save_.state, 0xB0 + size_t(c) * 72 + size_t(e) * 2) / 3;
    }
    refresh_budget_roster();  // the roster with the new skills before the booking
    finance_day(finance_, mgr_[0].budget, 10, 30);
    mgr_[0].image = current_club_image();
    log_.push_back("new season " + std::to_string(save_.year) + "/" + std::to_string(save_.year + 1));
}

std::vector<HokTableRow> Game::table_rows() const {
    std::vector<HokTableRow> rows = save_.table;
    for (const League& lg : leagues_)
        for (int s = 0; s < kHokLeagueSize; s++) {
            HokTableRow row = lg.rows[size_t(s)];
            if (row.form.size() < 8) row.form.insert(0, 8 - row.form.size(), '-');
            if (size_t(lg.first_team + s) < rows.size()) rows[size_t(lg.first_team + s)] = row;
        }
    return rows;
}

Bytes Game::save_bytes() const {
    HokFile h = clone_hok(save_);
    // date and matchdays
    const GameDate next = date_of_season_day(day_ + 1, h.year);  // the day to play after loading
    h.date.day = next.day;
    h.date.month = next.month;
    h.date.year = next.year;
    h.date.day_index = day_ + 1;
    const std::vector<HokTableRow> rows = table_rows();
    for (size_t t = 0; t < rows.size() && t < h.table.size(); t++)
        if (t < size_t(3 * kHokLeagueSize)) h.table[t] = rows[t];
    h.keep_table_copies = table_copies_frozen_;
    for (int l = 0; l < 3; l++) {
        h.matchday[l] = matchday_[size_t(l)];
        const League& lg = leagues_[size_t(l)];
        for (size_t place = 0; place < lg.ranking.size(); place++)
            h.table_order[size_t(l)][place] = lg.first_team + lg.ranking[place];
        const int md = matchday_[size_t(l)];
        if (md >= 1 && md <= 4 * (kHokLeagueSize - 1)) {
            const auto pairs = round_robin_pairings(schedule_.teams12, md - 1);
            for (size_t i = 0; i < pairs.size() && i < h.pairings[size_t(l)].size(); i++)
                h.pairings[size_t(l)][i] = {lg.first_team + pairs[i].home, lg.first_team + pairs[i].away};
        }
    }
    h.cup_draw = cup_draw_;
    h.cup_round = cup_round_;
    // Since step 7 the day loop keeps the decoded squads of managers 1..3 like manager 0's (lineup, training, games, goals,
    // injuries): store_hok writes them back like `squad` (an unused block was not decoded and keeps its bytes).
    store_hok(h);
    // manager block of manager 0 (raw money)
    Bytes& s = h.state;
    // The original keeps an event cursor independently of the displayed date/day counter.
    // Keeping START's cursor here makes a later save resume the September calendar again.
    s.at(kCalendarCursor) = uint8_t(day_ + 1);
    s.at(kCalendarCursor + 1) = uint8_t((day_ + 1) >> 8);
    s.at(kEuroRound) = uint8_t(euro_round_);
    for (size_t g = 0; g < 2; g++) store_native_group(s, euro_groups_[g], 2, int(g));
    if (euro_round_ >= 6) {
        store_native_group(s, euro_final_, 3, 0);
        for (size_t i = 0; i < euro_final_.teams.size(); i++) s.at(kEuroFinalTeams + i) = uint8_t(euro_final_.teams[i]);
        store_group_schedule(s, euro_final_, 3, 0, schedule_);
    }
    if (euro_champion_ >= 0) s.at(kStateEuroWinner) = uint8_t(euro_champion_);
    if (cup_winner_ >= 0) s.at(kStateCupWinner) = uint8_t(cup_winner_);
    if (champion_ >= 0) s.at(kStateChampion) = uint8_t(champion_);
    if (playoff_day_ > 0 || playoffs_set_up_)
        for (size_t group = 0; group < relegation_.size(); group++) {
            const GroupTable& table = relegation_[group];
            store_native_group(s, table, 1, int(group));
            for (size_t i = 0; i < table.teams.size(); i++)
                s.at(kRelegationTeams + group * 4 + i) = uint8_t(table.teams[i]);
            store_group_schedule(s, table, 1, int(group), schedule_);
        }
    // ovl05_194b leaves the native counter at 7 after the final (early return); the
    // final score records distinguish the completed tournament from its pending last date.
    s.at(kWorldRound) = uint8_t(std::min(wm_round_, 7));
    for (size_t g = 0; g < wm_groups_.size(); g++) store_native_group(s, wm_groups_[g], 0, int(g));
    if (wm_round_ > 0) {
        s.at(kWorldCursor) = uint8_t(std::clamp(wm_round_ - 5, 0, 2) * 8);
        for (size_t i = 0; i < wm_slots_.size(); i++) s.at(kWorldSlots + i) = uint8_t(wm_slots_[i]);
    }
    store_budget(h, mgr_[0].budget, 0);
    store_sponsor_offers(s, mgr_[0].offers, 0);
    s[kManagerBlock + 0x173] = uint8_t(mgr_[0].image);
    s[kManagerBlock + 0x174] = uint8_t(mgr_[0].image >> 8);
    for (int m = 1; m < managers_; m++) {  // managers 1..: what their UI actions (step 5) changed
        store_budget(h, mgr_[size_t(m)].budget, m);
        store_sponsor_offers(s, mgr_[size_t(m)].offers, m);
        s[manager_block(m) + 0x173] = uint8_t(mgr_[size_t(m)].image);
        s[manager_block(m) + 0x174] = uint8_t(mgr_[size_t(m)].image >> 8);
    }
    // plain header: calendar year of the next day (MONAT4: 1994), next matchday of the manager's league
    h.header[0x0A] = uint8_t(next.year);
    h.header[0x0B] = uint8_t(next.year >> 8);
    h.header[0x0F] = uint8_t(matchday_[size_t(std::clamp(mgr_[0].budget.league, 0, 2))]);
    return encode_hok(h);
}

long Game::quote_player_value(int manager,int slot) {
    if(manager<0||manager>=kHokManagers||slot<0||slot>=30)
        throw FormatError("HOK: invalid player quote slot");
    const auto player=contract_player(save_,manager,slot);
    if(player.player<0)throw FormatError("HOK: empty player quote slot");
    return player_worth(contracts_,player,Worth::Value,brng_);
}

Bytes Game::native_save_bytes() {
    HokFile h = decode_hok(save_bytes());
    const int managers = h.state.at(0x0DC6);
    if (managers < 1 || managers > 4) throw FormatError("HOK: invalid preview manager count");
    Bytes header(kHokHeaderSize, 0);
    const char version[] = "KRON_0.99";
    std::copy_n(version, 10, header.begin());
    std::copy_n(h.state.begin() + 0x8E29, 4, header.begin() + 0x0A);
    header[0x0E] = uint8_t(managers);
    header[0x0F] = h.state.at(0x8F80); // first league, independent of manager league
    for (int manager = 0; manager < managers; ++manager) {
        const size_t block = 0x0DCD + size_t(manager) * 0x1AF;
        const size_t record = 0x10 + size_t(manager) * 40;
        const bool world = h.state.at(0x025A) == 1;
        const int team = world ? h.state.at(0x068C) : h.state.at(block + 0x14);
        if (team >= kHokTeams) throw FormatError("HOK: invalid preview team");
        header[record] = h.state.at(block + 0xEB);
        header[record + 1] = uint8_t(world);
        std::copy_n(h.state.begin() + block, 19, header.begin() + record + 2);
        std::copy_n(h.state.begin() + 0x17E7 + size_t(team) * 61, 19, header.begin() + record + 21);
    }
    // The native writer leaks unused stack records. Define them as zero instead.
    std::copy_n("EH0", 4, header.begin() + 0xB0);
    BorlandRandom next = brng_;
    (void)next.range(-28673, 0); // first signed16 draw discarded by native SHL AX,16
    const uint32_t nonce = uint32_t(int32_t(next.range(-28673, 0) + 1));
    const uint8_t key = uint8_t(next.range(0, 255) + 0x6E);
    header[0xB8] = uint8_t(next.range(0, 255));
    for (int byte = 0; byte < 4; ++byte) header[0xB4 + size_t(byte)] = uint8_t(nonce >> (8 * byte));
    h.header = header; h.key0 = key;
    Bytes result = encode_hok(h);
    save_.header = std::move(header); save_.key0 = key; brng_ = next;
    save_history_.remember(nonce);
    return result;
}

// ovl08_0376 (ovr032:0376), the stadium week of manager m, with its wear message ovr032:0000
void Game::manager_stadium_week(int m) {
    g_draw_scope = "stadium-week";
    const GameDate wear_date = date_of_season_day(day_ + 1, save_.year);
    stadium_week(finance_, mgr_[size_t(m)].budget, save_.state, m, brng_, [&](const std::string& a, const std::string& b) {
        native_add_message(save_.state, brng_, {wear_date.day, wear_date.month, wear_date.year, day_ + 1}, MessageKind::TwoLines, {MessageArg::str(a), MessageArg::str(b)}, m, true);
    });
}

// One iteration of the finance loop ovr038:3332..335F (Amiga 17743): the daily finance ovl08_1d61 (sub_4C221) and then the
// image ovl08_2893 of manager m, both with DS:A70E = DS:A70D = m; the day loop runs it for 0..N-1 in index order.
// sub_4C221 draws its own refusal reset and calls the share market (sub_24EFF = ovl39_03ca at ovr032:1DAA) without a manager
// check: with N managers the market moves N times a day (from the disassembly; no recording with N > 1 yet). The WM branch
// (ovr038:3770..3803) books the stadium week (day % 7 == 0) and the healing 146e:1732 inside the same manager iteration.
void Game::manager_finance_day(int m, const GameDate& finance_date, int finance_month_days, bool world_phase) {
    ManagerState& mg = mgr_[size_t(m)];
    refresh_budget_roster(m);
    const long before = mg.budget.balance;
g_draw_scope = "refusal-reset";
    stadium_daily_refusal_reset(stadium_refusals_,m,brng_);
    if (finance_date.day == finance_month_days) clear_insurance_locks(m);
    // ovl08_1d61 (daily finance of the manager): its 11 draws (0,10) are the stadium refusal reset above, then the share
    // market (ovl39_03ca; REAL-CODE test realcode_market); the finance view mirrors the state.
g_draw_scope = "market"; if (std::getenv("EM_DEBUG_DAYFLOW")) std::fprintf(stderr, "dayflow: market at day %d md %d,%d,%d\n", day_, matchday_[0], matchday_[1], matchday_[2]);
    market_day(save_.state, brng_);
    for (int c = 0; c < kHokShares; c++) {
        save_.finance.shares[c] = le32(save_.state, 0x30 + size_t(c) * 8 + 4);
        for (int e = 0; e < kHokShareHistory; e++) save_.finance.share_price[c][e] = le16(save_.state, 0xB0 + size_t(c) * 72 + size_t(e) * 2) / 3;
    }
    if (finance_date.day == finance_month_days / 2 || finance_date.day == finance_month_days) {  // after the share market (whole-program test)
        update_sponsors(m);
        // ovr032:1F0B..1FAE: a coin flip; on heads the per-manager value DS:020D + 4 * manager (state 0x18) is set to 60000 +
        // 10000 * rand(0,10) stored raw (x3); it is the talent scout price (REAL-CODE after the emulator cache fix: 0x6DDD0 = 3 * (60000 + 10000 * 9))
        if (brng_.range(0, 1) != 0) {
            const uint32_t price = uint32_t(60000 + 10000 * brng_.range(0, 10)) * 3;
            for (int n = 0; n < 4; n++) save_.state.at(0x18 + 4 * size_t(m) + size_t(n)) = uint8_t(price >> (8 * n));
        }
    }
g_draw_scope = "finance";
    // ovr032:16B7 reads skill1 + skill2 of the club record itself: a weekly drift of the record on this day counts before the
    // next 146c refresh (REAL-CODE period 49: drift to 230 on the training day, booked at the month end, then refreshed to 228)
    if (finance_date.day == finance_month_days && mg.team >= 0 && !std::getenv("EM_OLD_INCOMESTRENGTH")) {
        const HokTeam& own = save_.teams[size_t(mg.team)];
        mg.budget.team_strength = own.skill1[0] + own.skill1[1] + own.skill2[0] + own.skill2[1];
        if (std::getenv("EM_DEBUG_STRENGTH")) std::fprintf(stderr, "month end day %d (%d.%d.): strength %d at %zu\n", day_, finance_date.day, finance_date.month, mg.budget.team_strength, (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0));
    }
    finance_day(finance_, mg.budget, finance_date.day, finance_month_days);
    if (finance_date.day == finance_month_days) {
        char line[128];
        std::snprintf(line, sizeof line, "month end %02d.%02d.: balance %ld DM (%+ld DM)", finance_date.day, finance_date.month,
                      mg.budget.balance / 3, (mg.budget.balance - before) / 3);
        log_.push_back(line);
    }
g_draw_scope = "image";
    mg.image = current_club_image(m);  // ovl08_2893, after each daily finance booking
    if (world_phase) {
        if ((day_ + 1) % 7 == 0) manager_stadium_week(m);
        heal_squad((day_ + 1) % 7 == 0, m);
    }
}

// The menu pass of the original (ovr038:2FAF..3064, Amiga 17631..17658): every manager 0..N-1 in index order draws the
// transfer-list roll rand(0, N + 2) (a zero refreshes the list, ovl11_34ef) and, when the coming day has a menu (league
// games or a game of any manager, var_1A), opens his menu ovl10_1cd2 with the daily valuation factor rand(85, 115): roll(0),
// factor(0), roll(1), factor(1), ... The factor DS:E1A4 is one value: after the pass it holds that of manager N-1.
// Assumption (docs/MEHRSPIELER_PLAN.md step 7): OpenEM draws the whole pass at the end of the step, before the hot-seat
// menus; random draws of a menu action of manager m < N-1 therefore come after the whole pass here, in the original
// between factor(m) and roll(m + 1).
void Game::menu_pass(bool menu) {
    for (int m = 0; m < managers_; m++) {
        if (brng_.range(0, managers_ + 2) == 0) refresh_transfer_list();  // rand(0, managers + 2) (asm 170773)
        if (menu) daily_luck_ = brng_.range(85, 115);
    }
}

void Game::advance_day() {
    g_goal_scenes = save_.state.at(0xA525);  // DS:0226 goal scenes (docs/logic/TORSZENEN_HOOK.md)
    if (std::getenv("EM_DEBUG_MSGS")) { std::fprintf(stderr, "msgs day %d:", day_); for (int i = 0; i < 4; i++) std::fprintf(stderr, " %02x/%02x", save_.state.at(0xAEB7 + size_t(i) * 0x71), save_.state.at(0xAEB8 + size_t(i) * 0x71)); std::fprintf(stderr, "\n"); }
    if (std::getenv("EM_DEBUG_MSGSTEP")) std::fprintf(stderr, "msgstep day %d pending %d\n", day_, int(message_step_pending_));
    // leaving the club calendar's last day (13.04., "Saisonende") clears the season flags of a manager without WM appointment
    // (DOS sub_6153F asm 171379, Amiga 17802; REAL-CODE period 57: on 13.04. the licence mark 0x04 of the last season end is still set)
    // (the loop clears +0xF3 of every manager 0..N-1 and sets 0x40 again for the appointed one, DOS LAB_6af8_3509)
    if (!std::getenv("EM_OLD_WMBEGIN") && calendar_.event(day_) == kEventSectionEnd && calendar_.event(day_ + 1) == kEventWorldChampionship)
        for (int m = 0; m < managers_; m++)
            if (national_coach_ != m) save_.state.at(manager_block(m) + 0xF3) = 0;
    // the first play step of a fresh game (START.HOK, day 0): the original leaves all 150 teams with line energy 90 and the
    // upper bytes of DS:A88D at FF (the new-game routine writes 0 there; REAL-CODE Köln period 1; the Mannheim chain only
    // started from a state the original had already played, mid1.HOK)
    if (day_ == 0 && !std::getenv("EM_OLD_FIRSTINIT")) {
        for (HokTeam& team : save_.teams)
            for (auto& kind : team.energy)
                for (auto& e : kind) e = 90;
        for (size_t i = 0x785; i < 0x788; i++) save_.state.at(i) = 0xFF;
    }
    const bool step_start = message_step_pending_;  // this day leaves the menu (a loaded game stands in the menu)
    // The day loop sub_6153F (ovr038:2E5E..3067) passes the manager block and the count-down before every day with a calendar
    // event (byte not 0, 9 or 0xFF), also a cup / EuroCup / playoff day without a manager inside a play step; the WM branch
    // (byte_32089) bypasses it (REAL-CODE period 44: two EuroCup days without the club count a general message down).
    const uint8_t day_event = calendar_.event(day_ + 1);
    const bool event_day = day_event != 0 && day_event != 9 && day_event != 0xFF && day_event != kEventWorldChampionship && !std::getenv("EM_OLD_MSGSTEP");
    if (message_step_pending_ || event_day) {  // ovr038:3019 / 3067
        // ovr038:3006..3019: the sort only runs when the day has league games (calendar byte & 7) or a manager plays on
        // it (sub_24C41, var_1A); the count-down always (REAL-CODE period 30: no sort before the first WM day)
        const uint8_t ev = calendar_.event(day_ + 1);
        const bool sort = calendar_.league_day(day_ + 1) || (ev == kEventCup && human_in_next_cup()) ||
                          (ev == kEventEuroCup && human_in_next_euro()) || (ev == kEventPlayoff && human_in_next_playoff()) ||
                          (ev == kEventWorldChampionship && national_coach_ >= 0) || std::getenv("EM_OLD_MSGSORT");
        // after a stop the sort already ran before the menu (see the step end): only days inside a step sort here
        // leaving the menu into a WM day without a national coach takes the WM branch: no count-down either (REAL-CODE
        // period 92: the injury message of 13.04. keeps its count 3 until the summer)
        const bool wm_exit = message_step_pending_ && ev == kEventWorldChampionship && national_coach_ < 0 && !std::getenv("EM_OLD_WMEXIT");
        if (sort && (!message_step_pending_ || std::getenv("EM_OLD_MSGSORTPOS"))) native_sort_messages(save_.state);
        if (!wm_exit) native_message_step(save_.state);
        message_step_pending_ = false;
    }
    interactive_played_ = false;
    for (int m = 0; m < managers_; m++) expire_advert_options(m);
    // The closing section end of the calendar (29.04.) is not played as a day: after the evening finances of the WM final
    // the original goes straight into the season change, then 132 summer days (REAL-CODE period 30: 15 finance days after
    // the first WM day, then the season change).
    if (day_ + 1 >= int(calendar_.events.size()) ||
        (day_ + 2 == int(calendar_.events.size()) && calendar_.event(day_ + 1) == kEventSectionEnd && !std::getenv("EM_OLD_SEASONEND"))) {
        // Native club-menu saves can already show April 30 while WM rounds are still
        // pending. Complete one such event per call before rolling the season over;
        // do not charge that displayed day's finances again.
        if (wm_round_ > 0 && wm_round_ < 8) {
            play_wm_day();
            return;
        }
        new_season();
        if (!std::getenv("EM_OLD_SEASONEND")) {
            // The original stops in the menu "Vor dem ersten Spieltag" (September 10) before the first matchday, with the
            // usual end of a play step: transfer-list roll and the daily factor (REAL-CODE period 30). Day 1 is the next step.
            for (int m = 0; m < managers_; m++) prepare_human_lineup(true, m);
            menu_pass(true);
            if (national_coach_ < 0) save_.state.at(0x0788) = 0;
            write_table_copies(save_.state, table_rows());
            table_copies_frozen_ = league_since_refresh_ = false;
            message_step_pending_ = true;
            return;
        }
    }
    // 162c:146c runs in the event block of the day loop (ovr038:2F1A): days without a calendar event skip it (REAL-CODE period 49)
    // every manager 0..N-1 in index order (the loop of ovr038:2EDD..2F2A; 146c draws nothing)
    if (std::getenv("EM_OLD_DAYREFRESH") || (calendar_.event(day_ + 1) != 0 && calendar_.event(day_ + 1) != 0xFF))
        for (int m = 0; m < managers_; m++) refresh_human_team(m);  // 162c:146c (not in the WM branch without a national coach, see there)
    // Native6AF8:313E..3158 preserves the menu's latest selection for
    // the next menu, then disables automatic selection during this match.
    // It runs once per play step, when the step leaves the menu (REAL-CODE period 29, write watch on DS:0375/0376: one copy
    // at the start of a seven-day step; periods 13 and 23: a later match day of the same step does not copy again, or the
    // saved mode would be lost). A loaded save keeps the 0 of DS:0375, so its first step saves 0.
    if (step_start)
        for (int m = 0; m < managers_; m++) {  // DS:0375/0376 + 2 * manager (ovr038:3111..3171)
            save_.state.at(0x078B + 2 * size_t(m)) = save_.state.at(0x078A + 2 * size_t(m));
            save_.state.at(0x078A + 2 * size_t(m)) = 0;
        }
    for (ManagerState& mg : mgr_) mg.hurt_in_match.fill(false);  // only a match of this day can count its injured starters
    day_++;
    const uint8_t event = calendar_.event(day_);
    for (ManagerState& mg : mgr_) mg.played = false;
    if (calendar_.league_day(day_)) {
        //6AF8:3178..3198 updates every club manager before the league games (sub_24AAF for 0..N-1 in index order).
        //All clubs use first-league DS07A9.
g_draw_scope = "youth";
        for (int m = 0; m < managers_; m++)
            youth_fixture_day(save_.state,finance_,std::min(44, matchday_[0]),m,[&](int lo,int hi){return brng_.range(lo,hi);});
g_draw_scope = "league-day";
        play_league_day();
        league_since_refresh_ = true;
        // after the last league day the play-off phase is set up at once: the image of the next days already counts the
        // play-offs, and the menu before the first play-off day shows the pairs (REAL-CODE Köln period 33)
        if (matchday_[0] > 4 * (kHokLeagueSize - 1) && !std::getenv("EM_OLD_PLAYOFFSETUP")) play_playoff_day(true);
        for (int m = 0; m < managers_; m++) mgr_[size_t(m)].played = mgr_[size_t(m)].team >= 0 && mgr_[size_t(m)].team < 3 * kHokLeagueSize;
    }
g_draw_scope = "cup/euro/playoff/wm"; if (std::getenv("EM_DEBUG_DAYFLOW")) std::fprintf(stderr, "dayflow: events at day %d: event %d league %d at %zu\n", day_, int(event), int(calendar_.league_day(day_)), (g_game_log ? g_game_log->size() : 0) + (g_borland_log ? g_borland_log->size() : 0));
    if (event == kEventCup) play_cup_day();
    if (event == kEventEuroCup) play_euro_day();
    if (event == kEventPlayoff) play_playoff_day();
    if (event == kEventWorldChampionship) play_wm_day();
    // the WM appointment on 13.04. (DOSBox NATPRE1 -> NATAUTO: the national manager's menu on that day); a manager without
    // appointment keeps last season's flags until the day is left (see the top of advance_day)
    if (event == kEventSectionEnd && calendar_.event(day_ + 1) == kEventWorldChampionship)
        begin_world_championship();
g_draw_scope = "match-injuries";
    // After the matches of the day the original leaves every team of the 150 with fresh line energy 90 (whole-program test:
    // even clubs that did not play); the weekly drift below overwrites the byte +0x1E afterwards.
    if (std::getenv("EM_OLD_ENERGY90"))
    for (HokTeam& team : save_.teams)
        for (int kind = 0; kind < 2; kind++)
            for (int line = 0; line < 4; line++) team.energy[kind][line] = 90;
    // managers in index order (assumption: the per-manager checks after a match without minute loop follow the manager
    // loop of the day, docs/MEHRSPIELER_PLAN.md step 7)
    for (int m = 0; m < managers_; m++)
        if (mgr_[size_t(m)].played && !injuries_in_minutes_) match_injuries(m);
    injuries_in_minutes_ = false;
    const bool world_phase = wm_round_ > 0 || event == kEventWorldChampionship;
    // The WM branch selects manager 5 on a match date; 146e:0c89's
    // human-manager guard then excludes the club. Empty dates retain club
    // processing before the next date's finances (ovl14:3727..388f).
    // Without a human national coach manager 5 has no club (0xFD), so the guard excludes nothing: the WM branch
    // (ovr038:37CC) runs sub_15369 on match dates too, without the team drift, and the calendar byte of the day is the
    // no-match flag (REAL-CODE period 30).
    // The branch calls 146e:0c89 once with DS:A70E = 5 (not in a manager loop); which squads it reaches with N > 1 is open,
    // the call stays with manager 0 (docs/MEHRSPIELER_PLAN.md step 7).
    if (world_phase && (national_coach_ < 0 ? event == kEventWorldChampionship : event == 0) && (day_ % 7 == 0 || (day_ + 3) % 7 == 0)) {
        if (national_coach_ >= 0) weekly_team_drift();
        squad_injuries(false, event == 0);  // today's healing was already prepared
        make_offers();
        train_squad();
    }
    // A native menu save is before today's match, but today's finances are already booked.
    // After playing this day, prepare the finances of the next (saveable) date. Charging the
    // played day again duplicates its interest on import and exports month-start contracts late.
    const GameDate finance_date = date_of_season_day(day_ + 1, save_.year);
    const int finance_month_days = kMonthDays[size_t(finance_date.month - 1)];
    // the routines of the new month (ovr038:2EAB / 36E3 / 4645) copy the month counter DS:5A1C to DS:A8A0 (state 0xB965, REAL-CODE period 7)
    if (finance_date.day == finance_month_days) save_.state.at(0xB965) = uint8_t(finance_date.month % 12);
    // ovr038:3332..335F (Amiga 17743): for every manager 0..N-1 in index order the daily finance and the image
    for (int m = 0; m < managers_; m++) manager_finance_day(m, finance_date, finance_month_days, world_phase);
    // Club branch 6af8:3374..3443: advance date, finance, then the
    // twice-weekly training and finally stadium. The WM preparation branch
    // instead advances the stadium and healing without normal club training.
    // The talent search (once, it loops over the managers itself) and the team drift (once) come first, then per manager
    // 0..N-1 146e:0c89 (injuries, offers, training) and on day % 7 == 0 the stadium week ovl08_0376 in the same iteration
    // (ovr038:33FA..3443, Amiga 17752..17764).
    if (!world_phase && ((day_ + 1) % 7 == 0 || (day_ + 4) % 7 == 0)) {
g_draw_scope = "weekly-injuries-offers-training";
        talent_search_week();  // ovr038:33FA, before 162c:1c5b and 146e:0c89
        weekly_team_drift();
        if (std::getenv("EM_DEBUG_DAYFLOW")) std::fprintf(stderr, "dayflow: weekly at day %d event(today) %d event(next) %d league_day(next) %d\n", day_, int(calendar_.event(day_)), int(calendar_.event(day_ + 1)), int(calendar_.league_day(day_ + 1)));
        for (int m = 0; m < managers_; m++) {
            g_draw_scope = "weekly-injuries-offers-training";
            squad_injuries((day_ + 1) % 7 == 0, calendar_.event(day_) == 0, m);  // the flag is the calendar byte of the current day (146e:0f5a)
            make_offers(m);
            train_squad(m);
            if ((day_ + 1) % 7 == 0) manager_stadium_week(m);
        }
    }
    // The evening restores the saved selection mode (DS:0376 -> DS:0375) and runs its automatic lineup. The original does
    // this at the menu only and leaves DS:0375 at 0 within a step (docs/DEVIATIONS.md); the saved mode DS:0376 is the same
    // in both, as the copy runs once per step (above), so the lineups agree (REAL-CODE periods 2-29).
    // after a cup day without a game the evening lineup runs, but the club record is not refreshed (REAL-CODE period 49)
    // Every manager 0..N-1 (the loop head of the next day, ovr038:2EDD..2F2A); the lineup draws nothing.
    for (int m = 0; m < managers_; m++) {
        skip_refresh_once_ = (empty_cup_day_ && !std::getenv("EM_OLD_EMPTYCUP")) || idle_cup_day_[size_t(m)];
        prepare_human_lineup(true, m);
    }
    empty_cup_day_ = false;
    idle_cup_day_.fill(false);
    // The original plays on until the next day with a game and only then returns to the menu; the loop head of that day draws the
    // transfer-list roll rand(0, managers + 2) (one human manager: (0,3)) and the daily valuation factor rand(85,115)
    // (DS:E1A4, ovr034:1CD2), once per manager (menu_pass). REAL-CODE: both draws end every period of the original.
    const uint8_t next_event = calendar_.event(day_ + 1);
    if (calendar_.league_day(day_ + 1) || next_event == kEventCup || next_event == kEventEuroCup || next_event == kEventPlayoff ||
        next_event == kEventWorldChampionship) {
        g_draw_scope = "play-step-end"; if (std::getenv("EM_DEBUG_DAYFLOW")) std::fprintf(stderr, "dayflow: play step end, next event %d (day %d)\n", int(next_event), day_ + 1);
        // A cup day without the human club is played in the same step: the original only returns to the menu (factor,
        // news timer) before a day on which the human club plays (REAL-CODE period 7: (0,3), then the cup day's (100,300)).
        const bool no_menu = !calendar_.league_day(day_ + 1) && ((next_event == kEventCup && !human_in_next_cup()) || (next_event == kEventEuroCup && !human_in_next_euro()) ||
                                              (next_event == kEventPlayoff && !human_in_next_playoff()) ||
                                              (next_event == kEventWorldChampionship && wm_round_ > 0 && national_coach_ < 0));  // REAL-CODE periods 7, 13, 28 and 30
        // before a WM day the club menu loop is not entered: no transfer-list roll, only the factor (REAL-CODE period 29)
        if (next_event != kEventWorldChampionship) menu_pass(!no_menu);
        else if (!no_menu) daily_luck_ = brng_.range(85, 115);
        if (no_menu) {
            if (league_since_refresh_) table_copies_frozen_ = true;  // no menu after a league day: the copies of the table stay (REAL-CODE period 28)
            if (next_event == kEventCup && cup_game_ == 0 && cup_winner_ < 0)
                for (int t = 36; t < 64; t++)
                    for (auto& kind : save_.teams[size_t(t)].energy)
                        for (auto& e : kind) e = 90;
            return;
        }
        // DS:A70E is the manager loop index of the original; the menu loop leaves it at 0 (ovr037 376d3, REAL-CODE period 29)
        if (national_coach_ < 0) save_.state.at(0x0788) = 0;
        // the club menu refreshes the copies of the table (REAL-CODE period 28); before a WM day it is not entered (period 29);
        // after the last league day the copies keep the 43rd matchday for the rest of the season, also at the menus before
        // play-off and cup days (REAL-CODE Köln periods 33 and 35)
        const bool season_over = matchday_[0] > 4 * (kHokLeagueSize - 1) && !std::getenv("EM_OLD_PLAYOFFCOPY");
        if (next_event != kEventWorldChampionship && !season_over) {
            write_table_copies(save_.state, table_rows());
            table_copies_frozen_ = league_since_refresh_ = false;
        } else if (season_over && league_since_refresh_) {
            table_copies_frozen_ = true;
        }
        // messages (24 entries of 0x71 bytes at DS:D294, src/game/messages.cpp): ovr038:3019 sorts them (newest first),
        // ovr038:3067 counts the general ones down (0x38, 0x28, 0x18, gone); entries of a player stay
        // (REAL-CODE period 16: only when the next play step leaves the menu, so a message of this step keeps its count)
        // the manager block of the next day sorts before its menu (ovr038:3006..3019, then sub_24C46; REAL-CODE period 75: a
        // message of the last day stands first in the menu), under the condition of the day-start block below
        if (!std::getenv("EM_OLD_MSGSORTPOS") && (calendar_.league_day(day_ + 1) || (next_event == kEventCup && human_in_next_cup()) ||
                                                   (next_event == kEventEuroCup && human_in_next_euro()) || (next_event == kEventPlayoff && human_in_next_playoff()) ||
                                                   (next_event == kEventWorldChampionship && national_coach_ >= 0)))
            native_sort_messages(save_.state);
        message_step_pending_ = true;
        // before a DEB cup day the original prepares the energies of the 28 cup clubs outside the three leagues (REAL-CODE)
        if (next_event == kEventCup && cup_game_ == 0 && cup_winner_ < 0)
            for (int t = 36; t < 64; t++)
                for (auto& kind : save_.teams[size_t(t)].energy)
                    for (auto& e : kind) e = 90;
    }
}

}  // namespace em
