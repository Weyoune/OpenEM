// SPDX-License-Identifier: GPL-3.0-only
#include "game/game.hpp"
#include "game/market.hpp"
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
constexpr size_t kSquad = 0x696D, kSquadRecord = 0x34, kTeamRecord = 0x3D;
constexpr size_t kManagerBlock = 0x0DCD;  // manager 0, see finances.hpp
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
constexpr int kManagers = 1;                                 // DS:0386: only manager 0 plays
// state offsets of the season review values: champion DS:D0E6, European cup winner DS:0388, and
// the German European cup entrants of ovl05_51a5 (DS:A776 list, DS:A763 slots 3 x 6, 0x80 = free)
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
    std::fill(save.state.begin() + 0x8E51, save.state.begin() + 0x8E71, uint8_t(0xFF));
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
    human_team_ = save_.state.at(kManagerBlock + 0x14);
    if(human_team_<0||human_team_>=kHokTeams)throw FormatError("HOK: invalid manager club");
    // the header holds the calendar year of the save (1994 in MONAT4, February), DS:03BB the year
    // the season started; the day loop counts from the season start
    if (save_.season_year > 0) save_.year = save_.season_year;
    human_.squad = &save_.squad;
    human_.players = &save_.players;
    human_.level = finance_.level;
    human_.selection_rng = &brng_;
    rng_.borland = &brng_;
    human_.minimum_players = 9;
    human_.rotation = &human_rotation_;
    budget_ = budget_from_save(save_, 0);
    offers_ = sponsor_offers_from_save(save_.state, 0);
    for (int d = 0; d < kTrainingDays; d++)
        for (int u = 0; u < kTrainingUnits; u++) plan_[size_t(d)][size_t(u)] = save_.state[0x070C + size_t(d * 4 + u)];
    image_ = le16(save_.state, 0x0DCD + 0x173);
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
    if (cup_game_ > 0) {
        for (size_t pair = 0; pair < cup_draw_.size(); pair++) {
            const HokPairing& p = cup_draw_[pair];
            Series s;
            s.higher = cup_game_ % 2 == 0 ? p.home : p.away;
            s.lower = s.higher == p.home ? p.away : p.home;
            const int played = cup_winner_ >= 0 ? 3 : cup_game_;
            for (int game = 0; game < played; game++) {
                const size_t o = kCupScores + size_t(game) * 32 + pair * 2;
                const int a = save_.state.at(o), b = save_.state.at(o + 1);
                if (a == 100 || b == 100) continue;
                const bool a_home = (game % 2 == 0) == (s.higher == p.home);
                const int marker = (a / 30 + b / 30) * 30;
                const MatchResult r{(a_home ? a : b) % 30, (a_home ? b : a) % 30, marker >= 60, marker};
                s.games.push_back(r);
                if ((r.home > r.away) == (game % 2 == 0)) s.wins_higher++; else s.wins_lower++;
            }
            cup_series_.push_back(s);
        }
    }
    if (playoff_day_ > 0) {
        const int stage = std::min((playoff_day_ - 1) / 5, 2);
        const int base = kPlayoffBase[stage];
        for (int pair = 0; pair < (4 >> stage); pair++) {
            const size_t slot = size_t(base + pair * 2);
            const int a = save_.state.at(kPlayoffTeams + slot), b = save_.state.at(kPlayoffTeams + slot + 1);
            Series s;
            s.higher = team_rank(a) < team_rank(b) ? a : b;
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
            final_played &= save_.state.at(o) % 30 > save_.state.at(o + 1) % 30;
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
        for(int kind=0;kind<2;kind++)human_.per_line[kind]=line_counts_[0][size_t(kind)];
        human_rotation_ = checkpoint.club_rotation;
        national_rotation_ = checkpoint.national_rotation;
        injured_role_ = checkpoint.injured_roles;
        for (size_t slot = 0; slot < save_.squad.size(); slot++)
            save_.squad[slot].status = SquadStatus(checkpoint.squad_status.at(slot));
        seasons_ = checkpoint.seasons;
        budget_.team_strength = checkpoint.team_strength;
        budget_.players = checkpoint.players;
        budget_.squad_costs = checkpoint.squad_costs;
        budget_.pool_costs = checkpoint.pool_costs;
        for (const auto& offer : checkpoint.offers)
            manager_offers_.push_back({offer.player, offer.listed, offer.bidder, offer.amount});
        human_.penalty_effort_adjustment = penalty_effort_adjustment(save_, human_team_);
        configure_human_tactics(human_, save_, 0);
    } else {
        restore_offers();
        if(previous){
            //5CB4:27BF ->1D1D: live return keeps loaded A, count pairs and
            //cached team fields; only59B5:26DC automatic lineup is rerun.
            inherit_native_runtime(*previous);
            auto_lineup(save_,0);
            human_.penalty_effort_adjustment=penalty_effort_adjustment(save_,human_team_);
            configure_human_tactics(human_,save_,0);
        }else {
            prepare_human_lineup();
            // The cold club model already uses its active pair. Persist that
            // same pair; otherwise a checkpoint would replace it with BSS0/0.
            line_counts_[0]={uint8_t(human_.per_line[0]),uint8_t(human_.per_line[1])};
        }
    }
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
    checkpoint.club_rotation = human_rotation_;
    checkpoint.national_rotation = national_rotation_;
    checkpoint.injured_roles = injured_role_;
    for (size_t slot = 0; slot < save_.squad.size(); slot++)
        checkpoint.squad_status.at(slot) = uint8_t(save_.squad[slot].status);
    checkpoint.seasons = seasons_;
    checkpoint.team_strength = budget_.team_strength;
    checkpoint.players = budget_.players;
    checkpoint.squad_costs = budget_.squad_costs;
    checkpoint.pool_costs = budget_.pool_costs;
    for (const auto& offer : manager_offers_)
        checkpoint.offers.push_back({offer.player, offer.bidder, offer.listed, offer.amount});
    return encode_game_checkpoint(checkpoint);
}

int Game::team_rank(int team) const {
    for (int l = 0; l < 3; l++) {
        const League& lg = leagues_[size_t(l)];
        if (team >= lg.first_team && team < lg.first_team + kHokLeagueSize)
            return int(std::find(lg.ranking.begin(), lg.ranking.end(), team - lg.first_team) - lg.ranking.begin());
    }
    return 5;
}

void Game::home_game(int guest, int kind, int home_class, int guest_class, int home_rank, int guest_rank,
                     bool league_statistics) {
    AttendanceMatch m;
    m.home_class = home_class;
    m.away_class = guest_class;
    m.home_strength = attendance_strength(save_.teams[size_t(human_team_)]);
    m.away_strength = attendance_strength(save_.teams[size_t(guest)]);
    m.home_rank = home_rank;
    m.away_rank = guest_rank;
    m.guest = guest;
    m.kind = kind;
    m.league_statistics = league_statistics;
    const uint8_t previous_home_games = uint8_t(budget_.home_games);
    const long spectators = home_attendance(finance_, budget_, m, brng_);
    if (league_statistics)
        save_.state.at(kManagerBlock + 0xFE + previous_home_games) = uint8_t(spectators / 1000);
    char line[160];
    std::snprintf(line, sizeof line, "  %ld spectators against %s, %ld DM tickets", spectators,
                  save_.teams[size_t(guest)].name.c_str(), long(budget_.ticket_price) * spectators);
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

void Game::play_league_day() {
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
    // ovl14: the gate of the human home game is booked before the matches are played (REAL-CODE whole-program test)
    bool gate_booked = false;
    for (size_t d = 0; d < league_days.size() && !gate_booked; d++) {
        const auto pre_pairs = round_robin_pairings(schedule_.teams12, league_days[d].matchday);
        for (const auto& p : pre_pairs) {
            const int hi = league_days[d].league->first_team + p.home, ai = league_days[d].league->first_team + p.away;
            if (hi != human_team_) continue;
            if (human_.minimum_players > 0 && selected_match_players(human_) < human_.minimum_players) break;  // forfeit: no gate
            g_draw_scope = "gate";
            home_game(ai, 0, team_class(finance_, hi), team_class(finance_, ai), team_rank(hi), team_rank(ai), true);
            gate_booked = true;
            break;
        }
    }
    g_draw_scope = "league-day";
    MatchStats stats;
    const auto day_results = play_matchdays(league_days, save_.teams, schedule_, rng_, &human_, human_team_, &stats, &brng_);
    for (size_t d = 0; d < league_days.size(); d++) {
        const int l = day_league[d];
        const int md = league_days[d].matchday + 1;
        League& lg = *league_days[d].league;
        const auto pairs = round_robin_pairings(schedule_.teams12, md - 1);
        const auto& results = day_results[d];
        matchday_[size_t(l)]++;
        for (size_t k = 0; k < pairs.size(); k++) {
            const int hi = lg.first_team + pairs[k].home, ai = lg.first_team + pairs[k].away;
            const size_t result_offset = kLeagueResults + size_t(l) * 0x210 + size_t(md - 1) * 12 + k * 2;
            save_.state.at(result_offset) = uint8_t(results[k].home);
            save_.state.at(result_offset + 1) = uint8_t(results[k].away);
            update_series(hi, 0, results[k].home, results[k].away);
            update_series(ai, 1, results[k].away, results[k].home);
            update_records(hi, ai, results[k].home, results[k].away);
            // 1238:114f: scorers of the computer clubs (player records), home first
            if (hi != human_team_) credit_club_scorers(save_.players, save_.state, hi, results[k].home, results[k].away, true, brng_);
            if (ai != human_team_) credit_club_scorers(save_.players, save_.state, ai, results[k].away, results[k].home, true, brng_);
            if (hi != human_team_ && ai != human_team_) continue;
            record_head_to_head(hi, ai, results[k].home, results[k].away);
            const bool forfeited = book_forfeit(stats);
            if (!forfeited) {
                book_special_teams(stats, hi == human_team_ ? 0 : 1);
                book_match_condition(stats, hi == human_team_ ? 0 : 1, 0);
            }
            char line[160];
            std::snprintf(line, sizeof line, "league matchday %d: %s - %s %d:%d%s", md, save_.teams[size_t(hi)].name.c_str(),
                          save_.teams[size_t(ai)].name.c_str(), results[k].home, results[k].away,
                          results[k].shootout ? " (shoot-out)" : "");
            log_.push_back(line);
            const int own = hi == human_team_ ? results[k].home : results[k].away;
            if (!forfeited) book_club_match(own, hi == human_team_ ? results[k].away : results[k].home, 0, true);
        }
        // ovl13_3855 (after all games of the league day): the manager's series records follow his club's series
        update_series_records(save_.state, human_team_);
    }
}

void Game::play_cup_day() {
    cup_day_++;
    if (cup_draw_.empty() || cup_winner_ >= 0) return;
    if (cup_game_ == 0) {
        cup_series_.clear();
        for (const HokPairing& p : cup_draw_) {
            Series s;
            s.higher = p.home; s.lower = p.away;
            cup_series_.push_back(s);
        }
        std::fill(save_.state.begin() + long(kCupScores), save_.state.begin() + long(kCupWins + 32), uint8_t(0));
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
    HumanSide side{human_team_, &human_, &stats};
    const auto ties = play_cup_round(save_.teams, draw, rng_, side);
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
        update_series(t.home, 0, t.result.home, t.result.away);
        update_series(t.away, 1, t.result.away, t.result.home);
        update_records(t.home, t.away, t.result.home, t.result.away);  // DS:D0C2 = 8 counts too
        if (t.home != human_team_ && t.away != human_team_) continue;
        const bool forfeited = book_forfeit(stats);
        if (!forfeited) {
            book_match_condition(stats, t.home == human_team_ ? 0 : 1, 0);
            book_special_teams(stats, t.home == human_team_ ? 0 : 1);
            human_played_ = true;
        }
        record_head_to_head(t.home, t.away, t.result.home, t.result.away);
        if (!forfeited) book_club_match(t.home == human_team_ ? t.result.home : t.result.away,
                        t.home == human_team_ ? t.result.away : t.result.home, 1, false,
                        t.result.shootout_goals[t.home == human_team_ ? 0 : 1]);
        char line[160];
        std::snprintf(line, sizeof line, "DEB cup: %s - %s %d:%d%s", save_.teams[size_t(t.home)].name.c_str(),
                      save_.teams[size_t(t.away)].name.c_str(), t.result.home, t.result.away,
                      t.result.shootout ? " (shoot-out)" : "");
        log_.push_back(line);
        if (!forfeited && t.home == human_team_)
            home_game(t.away, 1, team_class(finance_, t.home), team_class(finance_, t.away), team_rank(t.home),
                      team_rank(t.away));
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
        const bool first = cup_draw_[pair].home == s.higher;
        save_.state.at(kCupWins + pair * 2) = uint8_t(first ? s.wins_higher : s.wins_lower);
        save_.state.at(kCupWins + pair * 2 + 1) = uint8_t(first ? s.wins_lower : s.wins_higher);
        save_.state.at(kCupLive + pair * 2) = save_.state.at(kCupLive + pair * 2 + 1) = uint8_t(finished ? 100 : 0);
        if (finished)
            for (int game = cup_game_; game < 3; game++)
                save_.state.at(kCupScores + size_t(game) * 32 + pair * 2) =
                    save_.state.at(kCupScores + size_t(game) * 32 + pair * 2 + 1) = 100;
    }
    if (cup_game_ == 3) {
        std::vector<int> winners;
        for (const Series& s : cup_series_) winners.push_back(s.winner());
        // ovl05_5646: remember the next round reached (6 denotes the cup winner).
        if (std::find(winners.begin(), winners.end(), human_team_) != winners.end())
            save_.state.at(kManagerBlock + 0xE6) = uint8_t(cup_round_ + 1);
        if (winners.size() == 1) {
            cup_winner_ = winners[0];
            cup_game_ = 2;  // ovl05_5646 leaves the final results visible at index 2
            log_.push_back("DEB cup winner: " + save_.teams[size_t(cup_winner_)].name);
        } else {
            cup_draw_ = draw_cup_round(winners, brng_);
            cup_series_.clear();
            cup_round_++;
            cup_game_ = 0;
            std::fill(save_.state.begin() + long(kCupScores), save_.state.begin() + long(kCupWins + 32), uint8_t(0));
        }
    }
    save_.state.at(kCupGame) = uint8_t(cup_game_);
    save_.state.at(kCupPending) = uint8_t(open || cup_game_ == 0);
    save_.state.at(0x8F84) = uint8_t(cup_round_);  // DS:07AD, native round mirror
    store_cup_participants(save_, cup_draw_, cup_round_);
}

// 1000:3d1a on squad slot `slot` (see injuries.hpp). Recovery leaves lineup
// selection to the saved manual/automatic mode, just like 146e:1732.
void Game::injure(size_t slot, int kind) {
    HokSquadPlayer& sp = save_.squad[slot];
    uint8_t* r = save_.state.data() + kSquad + slot * kSquadRecord;
    HokPlayer& pl = save_.players[size_t(sp.player)];
    if (pl.form > 20) pl.form -= brng_.range(10, 15);
    const Injury in = draw_injury(injuries_, kind, finance_.level, brng_);
    r[0x0D] = uint8_t(in.weeks);
    r[0x09] |= 2;
    r[0x17] = uint8_t(in.type);
    if (r[0x0A]) injured_role_[slot] = r[0x0A];
    r[0x0A] = 0;
    sp.role = 0;
    char line[160];
    if (in.weeks == kCareerOver)
        std::snprintf(line, sizeof line, "%s: %s, career over", pl.name.c_str(), injuries_.names[size_t(in.type)].c_str());
    else
        std::snprintf(line, sizeof line, "%s: %s, out for %d weeks", pl.name.c_str(),
                      injuries_.names[size_t(in.type)].c_str(), in.weeks);
    log_.push_back(line);
    const long premium = int32_t(le32(save_.state, kSquad + slot * kSquadRecord + 0x30));
    if (premium != 0) {
        // 146e:0ba9, used after both training and match injuries: native
        // CL=2 left shift pays FOUR premiums, despite the manual's six.
        const long payment = int32_t(uint32_t(premium) << 2);
        budget_.balance += payment;
        log_.push_back(pl.name + " injury insurance payment " + std::to_string(payment / 3) + " DM");
    }
}

// 146e:1732: the summer uses healing without training injuries or training.
void Game::heal_squad(bool weekly) {
    for (size_t i = 0; i < save_.squad.size() && i < size_t(kHokSquadSlots); i++) {
        uint8_t* r = save_.state.data() + kSquad + i * kSquadRecord;
        if (save_.squad[i].player < 0) continue;
        if (weekly && (r[0x09] & 2) && r[0x0D] != kCareerOver && r[0x0D] != 0) r[0x0D]--;
        if ((r[0x09] & 3) && r[0x0D] == 0) {
            r[0x09] &= 0xFC;
            if (injured_role_[i]) {
                // Do not put him back into a manual lineup. The normal
                // lineup preparation selects him if automatics are enabled.
                injured_role_[i] = 0;
                log_.push_back(save_.players[size_t(save_.squad[i].player)].name + " is fit again");
            }
        }
    }
}

// 146e:0c89 runs on season_day % 7 == 0 or (season_day + 3) % 7 == 0.
void Game::squad_injuries(bool weekly, bool no_match_today) {
    heal_squad(weekly);
    for (size_t i = 0; i < save_.squad.size() && i < size_t(kHokSquadSlots); i++) {
        if (save_.squad[i].player < 0) continue;
        const uint8_t* r = save_.state.data() + kSquad + i * kSquadRecord;
        if (r[0x0D] == 0 && r[0x09] == 0 && training_injury(r[0x13], no_match_today, finance_.level, brng_)) injure(i, 0);
    }
}

void Game::refresh_budget_roster() {
    const ManagerBudget current = budget_from_save(save_, 0);
    budget_.players = current.players;
    budget_.squad_costs = current.squad_costs;
    budget_.pool_costs = current.pool_costs;
}

void Game::clear_insurance_locks() {
    // ovl08_1d61 clears bit 31 of the stored value at each month-end.
    for (size_t slot = 0; slot < 29; slot++)
        if (save_.state.at(kSquad + slot * kSquadRecord + 0x0F))
            save_.state.at(kSquad + slot * kSquadRecord + 0x2F) &= 0x7F;
}

// ovl06_364f once per minute of a match of the human club (here after the match, so a player hurt
// in it misses the next match on), ovl06_2385 picks the player.
void Game::match_injuries() {
    if (selected_match_players(human_) < 9) return;  // no minutes were played in a forfeit
    const int load = save_.state[kManagerBlock + 0xF0];
    int with_role = 0;
    for (const HokSquadPlayer& sp : save_.squad) with_role += sp.player >= 0 && sp.role != 0;
    if (with_role < 5) return;
    for (int minute = 0; minute < 60; minute++) {
        if (!match_injury_minute(load, finance_.level, brng_)) continue;
        for (int tries = 0; tries < 150; tries++) {
            const size_t slot = size_t(brng_.range(0, int(save_.squad.size()) - 1));
            const HokSquadPlayer& sp = save_.squad[slot];
            const uint8_t* r = save_.state.data() + kSquad + slot * kSquadRecord;
            if (sp.player < 0 || sp.role == 0 || sp.line == 0 || (r[0x09] & 2)) continue;
            const bool weaker = r[0x11] < r[0x10];
            if (brng_.range(0, 10) > (weaker ? 3 : 7)) continue;
            if (save_.players[size_t(sp.player)].position == 0 && brng_.range(0, 2) == 0) continue;
            injure(slot, 1);
            break;
        }
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
void Game::make_offers() {
    uint8_t* st = save_.state.data();
    auto pending = [&](const HokPlayer* pl) {
        for (const ManagerOffer& o : manager_offers_)
            if (o.player == int(pl - save_.players.data())) return true;
        return false;
    };
    auto strength_of_record = [&](int exe_index) {  // DS:8253 + index * 0x21, +0x15..+0x17
        const uint8_t* p = st + kPlayers + size_t(exe_index - 1) * kPlayerRecord;
        return (p[0x15] + p[0x16] + p[0x17]) / 3;
    };
    auto offer = [&](int block, int slot, bool listed) {
        uint8_t* r = st + kSquad + size_t(block) * 0x618 + size_t(slot) * kSquadRecord;
        if (r[0x09] & 0xC0) return;  // a saved interest must not be overwritten by a new bidder
        HokPlayer* pl = player_by_exe_index(r[0x0F]);
        if (!pl || pending(pl)) return;
        const long value = player_worth(contracts_, contract_player(save_, block, slot), Worth::Value, brng_);
        const long v = value / 10000;
        if (listed) {
            if (brng_.range(0, 180) >= v || brng_.range(0, 2) == 0) return;
        } else {
            if (brng_.range(0, 200) >= v || brng_.range(0, 450) != 0) return;
        }
        r[0x09] |= listed ? 0x80 : 0x40;
        const bool foreign = !listed && v > 90 && brng_.range(0, 10) == 0;
        const int bidder = pick_bidder(save_.teams, strength_of_record(slot), foreign, brng_);
        r[0x16] = uint8_t(bidder);
        ManagerOffer o{int(pl - save_.players.data()), listed, bidder, 0};
        // Native interest creation has no sale-price draw; price begins on click.
        manager_offers_.push_back(o);
        char line[160];
        std::snprintf(line,sizeof line,"transfer interest: %s for %s",save_.teams[size_t(bidder)].name.c_str(),pl->name.c_str());
        log_.push_back(line);
    };
    for (int slot = 0; slot < kTransferListSlots; slot++) {
        const uint8_t* r = st + kSquad + 4 * 0x618 + size_t(slot) * kSquadRecord;
        if (!r[0x0F] || st[kPlayers + size_t(r[0x0F] - 1) * kPlayerRecord + 0x1A] != 0) continue;  // manager 0's
        if (r[0x18] & 0x80) continue;
        offer(4, slot, true);
    }
    if (day_ >= 233) return;
    for (size_t slot = 0; slot < save_.squad.size() && slot < size_t(kHokSquadSlots); slot++) {
        const uint8_t* r = st + kSquad + slot * kSquadRecord;
        if (!r[0x0F] || r[0x0C] != 0 || (r[0x18] & 0x80)) continue;
        offer(0, int(slot), false);
    }
}

void Game::restore_offers() {
    // HOK retains interest and bidder only. Import performs no price valuation
    // or random draws; the native price belongs to a later row-click modal.
    auto restore = [&](int block, int slots, uint8_t flag, bool listed) {
        for (int slot = 0; slot < slots; slot++) {
            const size_t at = kSquad + size_t(block) * 0x618 + size_t(slot) * kSquadRecord;
            if (!(save_.state.at(at + 9) & flag) || !save_.state.at(at + 15)) continue;
            HokPlayer* player = player_by_exe_index(save_.state.at(at + 15));
            const int bidder = save_.state.at(at + 0x16);
            if (!player || player->raw[0x1A] != 0 || bidder >= int(save_.teams.size()) || bidder == human_team_) continue;
            const int index = int(player - save_.players.data());
            if (std::any_of(manager_offers_.begin(), manager_offers_.end(), [&](const auto& o) { return o.player == index; })) continue;
            manager_offers_.push_back({index, listed, bidder, 0});
        }
    };
    restore(4, kTransferListSlots, 0x80, true);
    restore(0, kHokSquadSlots, 0x40, false);
}

// The contract dialog of ovl02_0000 for manager 0, after the contract years went down: every
// squad player at 0 years is asked for a new contract (ovl11_41db; demand as renewal_demand after
// season day 232). If the manager offers nothing or the player refuses (player_accepts, offer =
// demand + 10 %), he leaves: the club gets half his market value (ovl11_3d36(record, 0) / 2,
// added to DS:97EA), his record goes (ovl09_25d6) and he is a free player (+0x1A = 6, no team)
// for ovl16_0480. A player who accepts gets the years and the salary.
void Game::expire_contracts() {
    constexpr int kMatchdays = 44;
    for (size_t slot = 0; slot < save_.squad.size();) {
        uint8_t* r = save_.state.data() + kSquad + slot * kSquadRecord;
        if (r[0x0B] != 0 || !r[0x0F]) {
            slot++;
            continue;
        }
        const ContractPlayer cp = contract_player(save_, 0, int(slot));
        const long demand = renewal_demand(contracts_, cp, 2, 100, kMatchdays, kMatchdays, 233, brng_);
        const int years = contract_decider_ ? contract_decider_(cp, demand) : 2;
        const long offer = demand + demand / 10;
        HokSquadPlayer& sp = save_.squad[slot];
        HokPlayer& pl = save_.players[size_t(sp.player)];
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
        budget_.balance += fee;
        const int exe_index = r[0x0F];
        remove_squad_record(0, int(slot), kHokSquadSlots - 1);
        save_.squad.erase(save_.squad.begin() + long(slot));
        std::move(injured_role_.begin() + long(slot) + 1, injured_role_.end(), injured_role_.begin() + long(slot));
        injured_role_.back() = 0;
        save_.state[kPlayers + size_t(exe_index - 1) * kPlayerRecord + 0x1A] = 6;
        save_.state[kPlayers + size_t(exe_index - 1) * kPlayerRecord + 0x1D] = 0xFF;
        pl.team = -1;
        log_.push_back(pl.name + " leaves the club, " + std::to_string(fee / 3) + " DM transfer fee");
    }
}

bool Game::begin_sale_offer(int block,int slot,int16_t native_si,SaleQuote& quote){
    if(national_coach_>=0)return false;
    return begin_native_sale(save_,contracts_,brng_,block,slot,native_si,quote);
}
bool Game::finish_sale_offer(const SaleQuote& quote,bool sell){
    if(!current_native_sale(save_,quote))return false;
    if(sell)for(size_t byte=0;byte<4;++byte)save_.state.at(0x0dcd+0x18f+byte)=uint8_t(uint32_t(budget_.balance)>>(8*byte));
    int buyer=-1;
    if(!finish_native_sale(save_,brng_,quote,sell,&buyer))return false;
    if(sell){
        HokPlayer* player=player_by_exe_index(quote.id);
        if(!player)throw FormatError("Sale: missing global player");
        player->team=player->raw[0x1d]==255?-1:player->raw[0x1d];
        budget_.balance=int32_t(le32(save_.state,0x0dcd+0x18f));
        if(quote.block==0){
            std::move(injured_role_.begin()+quote.slot+1,injured_role_.end(),injured_role_.begin()+quote.slot);
            injured_role_.back()=0;
        }
        save_.squad=hok_squad(save_,0);
        if(buyer>=0&&buyer<int(save_.teams.size())){
            auto& team=save_.teams[size_t(buyer)];
            for(int kind=0;kind<2;++kind){team.skill1[kind]=team.raw[0x18+kind];team.skill2[kind]=team.raw[0x1a+kind];}
        }
        refresh_budget_roster();
        log_.push_back(player->name+" sold for "+std::to_string(quote.amounts.net/3)+" DM");
    }
    manager_offers_.clear();restore_offers();return true;
}
bool Game::accept_offer(size_t index) {
    if(index>=manager_offers_.size())return false;
    const auto offer=manager_offers_[index];const auto& player=save_.players.at(size_t(offer.player));
    const int id=int((player.raw-save_.state.data()-kPlayers)/kPlayerRecord)+1;
    const int block=offer.listed?4:0;
    for(int slot=0;slot<(offer.listed?11:29);++slot){
        if(save_.state.at(kSquad+size_t(block)*0x618+size_t(slot)*52+15)!=id)continue;
        SaleQuote quote;
        return begin_sale_offer(block,slot,0,quote)&&finish_sale_offer(quote,true);
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
    if (slot >= 29 || slot >= save_.squad.size() || national_coach_ >= 0 || human_team_ >= kFirstNationalTeam)
        return false;
    const size_t at = kSquad + slot * kSquadRecord;
    const int id = save_.state.at(at + 0x0F);
    const HokPlayer* player = id ? player_by_exe_index(id) : nullptr;
    if (!player || player->raw[0x1A] != 0) return false;
    if (save_.state.at(at + 0x0C) != 0 || (save_.state.at(at + 0x18) & 0x80)) return false;
    const bool insured = le32(save_.state, at + 0x30) != 0;
    if (insured == enabled) return true;
    // Preserve ovl11_41db's exact low-two-bit test, including its native
    // flags==3 exception, and the one-month lock in the stored value.
    if ((save_.state.at(at + 0x2F) & 0x80) || (save_.state.at(at + 0x09) & 3) == 2) return false;
    const long premium = enabled ? player_worth(contracts_, contract_player(save_, 0, int(slot)),
                                                Worth::Salary, brng_) / 2 : 0;
    for (int byte = 0; byte < 4; byte++) save_.state.at(at + 0x30 + size_t(byte)) = uint8_t(uint32_t(premium) >> (8 * byte));
    if (enabled && premium != 0) save_.state.at(at + 0x2F) |= 0x80;
    refresh_budget_roster();
    log_.push_back(player->name + (enabled ? " injury insurance enabled at " : " injury insurance cancelled at ") +
                   std::to_string(premium / 3) + " DM per month");
    return true;
}

TransferMoveResult Game::list_transfer_player(size_t slot,bool prepare_lineup) { return move_owned_player(slot, true,prepare_lineup); }
TransferMoveResult Game::recall_transfer_player(size_t slot,bool prepare_lineup) { return move_owned_player(slot, false,prepare_lineup); }
void Game::finish_transfer_menu(){prepare_human_lineup(false);refresh_budget_roster();}

bool Game::set_training_plan(const TrainingPlan& plan) {
    if (national_coach_ >= 0 || human_team_ >= kFirstNationalTeam) return false;
    for (const auto& day : plan) for (uint8_t kind : day) if (kind >= kTrainingKinds) return false;
    plan_ = plan;
    for (size_t day = 0; day < plan.size(); day++)
        for (size_t unit = 0; unit < plan[day].size(); unit++) save_.state.at(0x070C + day * 4 + unit) = plan[day][unit];
    budget_.open_97ee = training_recovery_cost(training_, plan_, budget_.league);
    for (int byte = 0; byte < 4; byte++)
        save_.state.at(kManagerBlock + 0x193 + size_t(byte)) = uint8_t(uint32_t(budget_.open_97ee) >> (8 * byte));
    log_.push_back("training plan saved, recovery " + std::to_string(budget_.open_97ee * 2 / 3) + " DM per week");
    return true;
}

StockResult Game::trade_shares(size_t company,uint32_t quantity,bool sell) {
    if(company>=kHokShares||quantity==0||quantity==UINT32_MAX)return StockResult::Invalid;
    if(national_coach_>=0||human_team_>=kFirstNationalTeam)return StockResult::Unavailable;
    const size_t holding=0x30+company*8;
    const uint32_t old_quantity=le32(save_.state,holding+4),old_cost=le32(save_.state,holding);
    const uint32_t price=le16(save_.state,0xB0+company*72+70);
    if(!price)return StockResult::Invalid;
    if(sell){if(quantity>old_quantity)return StockResult::InsufficientShares;}
    else if(budget_.balance<=0||quantity>uint32_t(budget_.balance)/price)return StockResult::InsufficientCash;
    const uint32_t gross=quantity*price;
    const uint32_t fee=(gross*15u)/1000u;
    uint32_t next_quantity,next_cost;
    if(sell){
        next_quantity=old_quantity-quantity;
        const uint32_t remaining_percent=(next_quantity*100u)/old_quantity;
        next_cost=(old_cost*remaining_percent)/100u;
        budget_.balance=int32_t(uint32_t(budget_.balance)+gross-fee);
    }else{
        next_quantity=old_quantity+quantity;next_cost=old_cost+gross+fee;
        // The native buy limit reserves no commission, so a maximum purchase
        // can overdraw the account by its fee. Preserve that behavior.
        budget_.balance=int32_t(uint32_t(budget_.balance)-gross-fee);
    }
    const auto store=[&](size_t at,uint32_t value){for(int byte=0;byte<4;byte++)save_.state.at(at+size_t(byte))=uint8_t(value>>(8*byte));};
    store(holding,next_cost);store(holding+4,next_quantity);
    store(kManagerBlock+0x18F,uint32_t(budget_.balance));
    save_.finance.shares[company]=next_quantity;save_.finance.share_cost[company]=int32_t(next_cost)/3;
    save_.finance.balance=budget_.balance/3;
    log_.push_back(std::string(sell?"sold ":"bought ")+std::to_string(quantity)+" shares of company "+std::to_string(company));
    return StockResult::Traded;
}

ManualLineupResult Game::change_lineup(size_t slot,int selected_line,ManualLineupAction action,const bool* six_override,
                                     const std::array<std::array<int,6>,5>* cells_override){
    if(national_coach_>=0||human_team_>=kFirstNationalTeam)throw FormatError("Aufstellung ist in dieser Phase nicht verfügbar.");
    const auto result=manual_lineup(save_,0,slot,selected_line,action,six_override,cells_override);
    if(result.changed){
        refresh_human_team();human_.penalty_effort_adjustment=penalty_effort_adjustment(save_,human_team_);
    }
    return result;
}

bool Game::set_tactics(const TacticsPlan& plan){
    if(national_coach_>=0||human_team_>=kFirstNationalTeam)return false;
    commit_tactics(save_,plan,0);
    configure_human_tactics(human_,save_,0);refresh_human_team();
    human_.penalty_effort_adjustment=penalty_effort_adjustment(save_,human_team_);
    return true;
}

BankResult Game::bank_transfer(long amount) {
    if (amount == 0) return BankResult::InvalidAmount;
    if (national_coach_ >= 0 || human_team_ >= kFirstNationalTeam) return BankResult::Unavailable;
    if (amount > 0 && amount > budget_.balance) return BankResult::InsufficientCash;
    const size_t bank_manager = kManagerBlock + 4 * 0x1AF;
    const int32_t bank_counter = int32_t(le32(save_.state, bank_manager + 0x19B));
    const int64_t available = std::max(int64_t(0), std::max(int64_t(budget_.bank),
                               int64_t(6000000) - std::max(int64_t(0), int64_t(bank_counter))));
    if (amount < 0 && -int64_t(amount) > available) return BankResult::CreditLimit;
    // Native four-sided posting, with defined 32-bit wrap just like the two
    // word ADD/ADC and SUB/SBB operations in ovl15_15d7.
    budget_.balance = int32_t(uint32_t(budget_.balance) - uint32_t(amount));
    budget_.bank = int32_t(uint32_t(budget_.bank) + uint32_t(amount));
    const uint32_t bank_cash = le32(save_.state, bank_manager + 0x18F) + uint32_t(amount);
    auto store = [&](size_t at, uint32_t value) {
        for (int byte = 0; byte < 4; byte++) save_.state.at(at + size_t(byte)) = uint8_t(value >> (8 * byte));
    };
    store(kManagerBlock + 0x18F, uint32_t(budget_.balance));
    store(kManagerBlock + 0x19B + 4 * 4, uint32_t(budget_.bank));
    store(bank_manager + 0x18F, bank_cash);
    store(bank_manager + 0x19B, uint32_t(bank_counter) - uint32_t(amount));
    log_.push_back("bank " + std::string(amount > 0 ? "deposit/repayment " : "withdrawal/loan ") +
                   std::to_string((amount > 0 ? int64_t(amount) : -int64_t(amount)) / 3) + " DM");
    return BankResult::Transferred;
}

TransferMoveResult Game::move_owned_player(size_t slot, bool to_market,bool prepare_lineup) {
    constexpr int club_end = 29, pool_end = kTransferListSlots - 1;
    if (slot >= size_t(to_market ? club_end : kTransferListSlots)) return TransferMoveResult::Invalid;
    if (national_coach_ >= 0 || human_team_ >= kFirstNationalTeam) return TransferMoveResult::Unavailable;
    const size_t source_block = kSquad + size_t(to_market ? 0 : 4) * 0x618;
    const size_t target_block = kSquad + size_t(to_market ? 4 : 0) * 0x618;
    const size_t source = source_block + slot * kSquadRecord;
    const int id = save_.state.at(source + 15);
    const HokPlayer* player = id ? player_by_exe_index(id) : nullptr;
    if (!player || player->raw[0x1A] != 0 || player->team != human_team_)
        return TransferMoveResult::Unavailable;
    if ((save_.state.at(source + 0x18) & 0x80) ||
        (save_.state.at(source + 9) & (to_market ? 0x40 : 0x80))) return TransferMoveResult::Blocked;
    if (to_market) {
        int own = 0;
        for (size_t at = 0; at < 12; at++) {
            const int other_id = save_.state.at(target_block + at * kSquadRecord + 15);
            const HokPlayer* other = other_id ? player_by_exe_index(other_id) : nullptr;
            own += other && other->raw[0x1A] == 0;
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
    std::copy_n(current.state.begin() + long(kSquad), 30 * kSquadRecord, save_.state.begin() + long(kSquad));
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
    remove_squad_record(to_market ? 0 : 4, int(slot), to_market ? club_end : pool_end);
    if (to_market) {
        std::move(injured_role_.begin() + long(slot) + 1, injured_role_.end(), injured_role_.begin() + long(slot));
        injured_role_.back() = 0;
    } else {
        std::move_backward(injured_role_.begin() + insert, injured_role_.end() - 1, injured_role_.end());
        injured_role_[size_t(insert)] = 0;
    }
    save_.squad = hok_squad(save_, 0);
    if(prepare_lineup)prepare_human_lineup(false);
    refresh_budget_roster();
    log_.push_back(player->name + (to_market ? " listed for transfer" : " returns from transfer list"));
    return TransferMoveResult::Moved;
}

ContractResult Game::negotiate_contract(size_t slot, int years, long salary, bool allow_shortening) {
    if (slot >= 29 || years < 1 || years > 4 || salary <= 0) return ContractResult::Invalid;
    if (slot >= save_.squad.size() || national_coach_ >= 0 || human_team_ >= kFirstNationalTeam || day_ + 1 > 232)
        return ContractResult::Unavailable;
    const size_t at = kSquad + slot * kSquadRecord;
    const int id = save_.state.at(at + 0x0F);
    const HokPlayer* player = id ? player_by_exe_index(id) : nullptr;
    if (!player || player->raw[0x1A] != 0 || save_.state.at(at + 0x0C) != 0) return ContractResult::Unavailable;
    if (save_.state.at(at + 0x18) != 0) return ContractResult::Blocked;
    const ContractPlayer current = contract_player(save_, 0, int(slot));
    if (years < current.years && !allow_shortening) return ContractResult::ShorteningNotConfirmed;
    // ovl11_41db bypasses the random player check for equal years and an
    // unchanged or higher salary, including an entirely unchanged contract.
    const bool guaranteed = years == current.years && salary >= current.salary;
    if (!guaranteed && !player_accepts(contracts_, current, years, salary,
                                      matchday_[size_t(std::clamp(budget_.league, 0, 2))], 44, day_ + 1, brng_)) {
        save_.state.at(at + 0x18) = uint8_t(brng_.range(10, 18));
        log_.push_back(player->name + " refuses contract; talks paused for " +
                       std::to_string(save_.state.at(at + 0x18)) + " squad updates");
        return ContractResult::PlayerRefused;
    }
    save_.state.at(at + 0x0B) = uint8_t(years);
    for (int byte = 0; byte < 4; byte++) save_.state.at(at + 0x28 + size_t(byte)) = uint8_t(uint32_t(salary) >> (8 * byte));
    save_.squad.at(slot).contract_years = years;
    save_.squad.at(slot).salary = salary / 3;
    refresh_budget_roster();
    log_.push_back(player->name + " signs contract for " + std::to_string(years) + " years at " +
                   std::to_string(salary / 3) + " DM per month");
    return ContractResult::Signed;
}

void Game::decline_offer(size_t index) {
    if(index>=manager_offers_.size())return;
    const auto offer=manager_offers_[index];const auto& player=save_.players.at(size_t(offer.player));
    const int id=int((player.raw-save_.state.data()-kPlayers)/kPlayerRecord)+1,block=offer.listed?4:0;
    for(int slot=0;slot<(offer.listed?11:29);++slot){
        if(save_.state.at(kSquad+size_t(block)*0x618+size_t(slot)*52+15)!=id)continue;
        SaleQuote quote;if(begin_sale_offer(block,slot,0,quote))(void)finish_sale_offer(quote,false);return;
    }
}

void Game::train_squad() {
    budget_.balance -= budget_.open_97ee;  // weekly recovery costs (146e:0c89)
    for (size_t i = 0; i < save_.squad.size(); i++) {
        HokSquadPlayer& sp = save_.squad[i];
        if (sp.player < 0) continue;
        uint8_t* r = save_.state.data() + kSquad + i * kSquadRecord;
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
        // 146e:1205..1216: negotiation cooldown ticks on these squad
        // updates; the high-bit contract marker never counts down.
        if (r[0x18] != 0 && (r[0x18] & 0x80) == 0) r[0x18]--;
        sp.skill_a = p.condition;
        sp.skill_b = p.technique;
        sp.nibbles = p.nibbles;
        sp.exhaustion = p.exhaustion * 2 / 3;
        sp.raw_exhaustion = p.exhaustion;
    }
    refresh_human_team();
}

// ovl14_2e0f restores the saved automatic-lineup mode before entering the next
// manager menu. Recompute after loading as well: a HOK can contain an older lineup.
void Game::prepare_human_lineup(bool restore_saved_mode) {
    if(restore_saved_mode)save_.state.at(0x078A) = save_.state.at(0x078B);
    auto_lineup(save_, 0);
    refresh_human_team();
    human_.penalty_effort_adjustment = penalty_effort_adjustment(save_, human_team_);
    configure_human_tactics(human_, save_, 0);
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

void Game::refresh_human_team() {
    HokTeam& team = save_.teams[size_t(human_team_)];
    for (int kind = 0; kind < 2; kind++) {
        int sum[3] = {};
        for (int line = 0; line < 3; line++) {
            const LineValues v = line_strength(team, human_, line, kind, false, 0);
            sum[0] += v.skill1;
            sum[1] += v.skill2;
            sum[2] += v.form;
        }
        team.skill1[kind] = sum[0] / 3;
        team.skill2[kind] = sum[1] / 3;
        team.form[kind] = sum[2] / 3;
    }
    budget_.team_strength = team.skill1[0] + team.skill1[1] + team.skill2[0] + team.skill2[1];
}

bool Game::enter_advertising() {
    const long income=monthly_income(budget_,true);
    if(income==budget_.stored_income)return false;
    budget_.stored_income=income;return true;
}

bool Game::accept_sponsor_offer(int kind, size_t offer) {
    if (kind < 0 || kind >= kSponsorKinds || offer >= kSponsorOffers) return false;
    AdRecord& candidate = offers_[size_t(kind)][offer];
    if (candidate.amount <= 0 || candidate.months <= 0) return false;
    // ovl04_0ee0: the two facility-specific contracts require the corresponding
    // stadium extension at level 2 (DS:9771 / DS:9775).
    if ((kind == 3 && budget_.stadium[0] != 2) || (kind == 4 && budget_.stadium[2] != 2)) return false;
    auto free = [](const AdvertContract& a) { return a.amount == 0 || (a.flags & 0x80) != 0; };
    if (kind == 0) {
        if (candidate.boards < 1 || candidate.boards > 7) return false;
        if (std::count_if(budget_.adverts.begin(), budget_.adverts.begin() + 7, free) < candidate.boards) return false;
        // DS:017C (state 0x01F2) is the saved native board-contract serial.
        const int owner = save_.state.at(0x01F2);
        int left = candidate.boards;
        for (auto it = budget_.adverts.begin(); it != budget_.adverts.begin() + 7 && left; ++it)
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
                if(budget_.adverts[i].amount>budget_.adverts[j].amount){
                    std::swap(budget_.adverts[i],budget_.adverts[j]);changed=true;
                }
        }while(changed);
    } else {
        AdvertContract& contract = budget_.adverts[size_t(kind + 6)];
        if (!free(contract)) return false;
        contract = {candidate.amount, candidate.months, candidate.renewal, candidate.boards,
                    candidate.sponsor, candidate.min_image};
    }
    candidate.amount = 0;  // ovl04_0ee0 retains the other offer bytes
    // ovl04_0caa redraws the sponsor screen's full income after the decision.
    budget_.stored_income = monthly_income(budget_, true);
    return true;
}

bool Game::renew_advert_contract(size_t slot, bool accept) {
    AdvertRenewalDraft draft;
    if(!begin_advert_renewal(slot,draft))return false;
    return finish_advert_renewal(draft,accept);
}

bool Game::begin_advert_renewal(size_t slot,AdvertRenewalDraft& draft) {
    if (slot >= budget_.adverts.size()) return false;
    const AdvertContract original = budget_.adverts[slot];
    if (original.amount <= 0 || original.months_left != 0 || (original.flags & 0x7F) == 0) return false;
    // The native sponsor decision precedes the manager's confirmation, including
    // its random draws even if the manager later declines the offered extension.
    const long required = long(brng_.range(90, 120)) * original.min_image / 100;
    const bool sponsor_stays = required < current_club_image();
    draft={slot,original,sponsor_stays};return true;
}

bool Game::finish_advert_renewal(const AdvertRenewalDraft& draft,bool accept) {
    const size_t slot=draft.slot;
    if(slot>=budget_.adverts.size())return false;
    const auto& current=budget_.adverts[slot];const auto& original=draft.contract;
    if(original.amount<=0||original.months_left!=0||(original.flags&0x7f)==0||
       current.amount!=original.amount||current.months_left!=original.months_left||current.flags!=original.flags||
       current.board_owner!=original.board_owner||current.sponsor!=original.sponsor||current.min_image!=original.min_image)return false;
    const bool stays = accept && draft.sponsor_stays;
    AdvertContract resolved = original;
    resolved.flags = 0;  // the option can only be exercised once
    if (stays) resolved.months_left = original.flags & 0x7F;
    else resolved.amount = 0;
    for (size_t i = 0; i < budget_.adverts.size(); i++) {
        if (i != slot && !(slot < 7 && i < 7 && budget_.adverts[i].board_owner == original.board_owner)) continue;
        budget_.adverts[i] = resolved;  // ovl04_0ee0 copies the complete ten-byte record
    }
    budget_.stored_income = monthly_income(budget_, true);  // ovl04_0caa
    return stays;
}

void Game::expire_advert_options() {
    // ovl10_1cd2: options left unresolved when leaving the day's menu expire.
    for (AdvertContract& contract : budget_.adverts)
        if (contract.months_left == 0 && contract.flags != 0) {
            contract.amount = 0;
            contract.flags = 0;
            contract.board_owner = 255;
        }
}

int Game::current_club_image() {
    ImageInputs in;
    const auto competition = image_competition_factors(save_, 0);
    in.in_competition = competition.playoffs;
    in.cup = competition.cup;
    in.euro = competition.euro;
    in.league = budget_.league;
    in.matchday = matchday_[size_t(std::clamp(budget_.league, 0, 2))];
    const int team = human_team_;
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
        ContractPlayer c = contract_player(save_, 0, slot, &p);
        if (p >= 0) image_squad.push_back(c);
    }
    in.stadium = stadium_score(sponsor_tables_, budget_.stadium, budget_.places);
    return club_image(in, image_squad, contracts_, brng_);
}

void Game::update_sponsors() {
    // ovl08_2ba7 uses the image already stored by the preceding daily
    // preparation; calculating a new image here changes both phase and RNG.
    generate_sponsor_offers(sponsor_tables_, image_, false, offers_, brng_);
    char line[96];
    std::snprintf(line, sizeof line, "image %d, sponsor offers renewed", image_);
    log_.push_back(line);
}

void Game::play_euro_day() {
    if (euro_round_ >= 12) return;
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
        if (minute_path) {
            res = league_match(save_.teams[size_t(home)], save_.teams[size_t(away)], rng_, 0,
                               home == human_team_ ? &human_ : nullptr,
                               away == human_team_ ? &human_ : nullptr, &stats);
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
        if (home != human_team_ && away != human_team_) return;
        const bool forfeited = book_forfeit(stats);
        if (!forfeited) {
            book_match_condition(stats, home == human_team_ ? 0 : 1, 0);
            book_special_teams(stats, home == human_team_ ? 0 : 1);
            book_club_match(home == human_team_ ? res.home : res.away,
                            home == human_team_ ? res.away : res.home, 2, false);
            human_played_ = true;
        }
        record_head_to_head(home, away, res.home, res.away);
        char line[160];
        std::snprintf(line, sizeof line, "European cup: %s - %s %d:%d", save_.teams[size_t(home)].name.c_str(),
                      save_.teams[size_t(away)].name.c_str(), res.home, res.away);
        log_.push_back(line);
        if (!forfeited && home == human_team_)  // DS:D0C2 = 3 in the groups, 5 in the final round
            home_game(away, r < 6 ? 2 : 3, team_class(finance_, home), team_class(finance_, away), team_rank(home), 5);
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
    save_.state.at(kEuroRound) = uint8_t(euro_round_);
}

void Game::play_playoff_day() {
    const int day = playoff_day_++;
    const int stage = day / 5;  // five days per best-of-five round
    MatchStats stats;
    HumanSide side{human_team_, &human_, &stats};
    const League& first = leagues_[0];
    auto position = [&](int team) { return first.rows[size_t(team - first.first_team)].position; };
    if (day % 5 == 0) {
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
            s.higher = position(teams[i]) < position(teams[i + 1]) ? teams[i] : teams[i + 1];
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
    for (Series& s : series_) {
        if (s.wins_higher == 3 || s.wins_lower == 3) continue;
        const bool higher_home = s.games.size() % 2 == 0;  // games 1, 3, 5 at the higher seed
        const int home = higher_home ? s.higher : s.lower, away = higher_home ? s.lower : s.higher;
        const MatchResult r = playoff_game(save_.teams, home, away, rng_, side);
        s.games.push_back(r);
        if ((r.home > r.away) == higher_home)
            s.wins_higher++;
        else
            s.wins_lower++;
        if (home != human_team_ && away != human_team_) continue;
        const bool forfeited = book_forfeit(stats);
        if (!forfeited) {
            book_match_condition(stats, home == human_team_ ? 0 : 1, 0);
            book_special_teams(stats, home == human_team_ ? 0 : 1);
            book_club_match(home == human_team_ ? r.home : r.away,
                            home == human_team_ ? r.away : r.home, 0, false,
                            r.shootout_goals[home == human_team_ ? 0 : 1]);
            human_played_ = true;
        }
        record_head_to_head(home, away, r.home, r.away);
        char line[160];
        std::snprintf(line, sizeof line, "play-offs: %s - %s %d:%d (series %d:%d)", save_.teams[size_t(home)].name.c_str(),
                      save_.teams[size_t(away)].name.c_str(), r.home, r.away, s.wins_higher, s.wins_lower);
        log_.push_back(line);
        if (!forfeited && home == human_team_) home_game(away, 1, 0, 0, position(home), position(away));
    }
    // relegation rounds on the first and third day of each play-off round (round index =
    // DS:07B0 * 2 + [DS:CCA3 = 2]): a double round robin of four, letter table DS:0474
    if (day % 5 == 0 || day % 5 == 2) {
        const int round = stage * 2 + (day % 5 == 2 ? 1 : 0);
        for (size_t g = 0; g < 2; g++) {
            GroupTable& t = relegation_[g];
            const auto pairs = round_robin_pairings(schedule_.teams4, round);
            for (size_t pair = 0; pair < pairs.size(); pair++) {
                const Pairing& p = pairs[pair];
                const int home = t.teams[size_t(p.home)], away = t.teams[size_t(p.away)];
                const MatchResult r = league_match(save_.teams[size_t(home)], save_.teams[size_t(away)], rng_, 0,
                                                   home == human_team_ ? &human_ : nullptr,
                                                   away == human_team_ ? &human_ : nullptr, &stats);
                t.book(home, away, r);
                const size_t o = kGroupResults + 0x48 + g * 0x24 + size_t(round) * 6 + pair * 2;
                save_.state.at(o) = uint8_t(r.home);
                save_.state.at(o + 1) = uint8_t(r.away);
                if (home != human_team_ && away != human_team_) continue;
                const bool forfeited = book_forfeit(stats);
                if (!forfeited) {
                    book_match_condition(stats, home == human_team_ ? 0 : 1, 0);
                    book_special_teams(stats, home == human_team_ ? 0 : 1);
                    book_club_match(home == human_team_ ? r.home : r.away,
                                    home == human_team_ ? r.away : r.home, 0, false);
                    human_played_ = true;
                }
                record_head_to_head(home, away, r.home, r.away);
                char line[160];
                std::snprintf(line, sizeof line, "relegation round %d: %s - %s %d:%d", round + 1,
                              save_.teams[size_t(home)].name.c_str(), save_.teams[size_t(away)].name.c_str(), r.home, r.away);
                log_.push_back(line);
                if (!forfeited && home == human_team_) home_game(away, 1, team_class(finance_, home), team_class(finance_, away), 5, 5);
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
        state.at(kPlayoffLive + slot) = state.at(kPlayoffLive + slot + 1) = uint8_t(finished ? 100 : 0);
    }
    int next_stage = stage, next_game = completed_days;
    if (completed_days == 5 && stage < 2) {
        std::vector<int> winners;
        for (const Series& s : series_) winners.push_back(s.winner());
        if (stage == 0) std::swap(winners[1], winners[2]);
        const size_t next = size_t(kPlayoffBase[stage + 1]);
        for (size_t i = 0; i + 1 < winners.size(); i += 2) {
            if (team_rank(winners[i]) > team_rank(winners[i + 1])) std::swap(winners[i], winners[i + 1]);
            for (size_t side = 0; side < 2; side++) {
                state.at(kPlayoffTeams + next + i + side) = uint8_t(winners[i + side]);
                state.at(kPlayoffWins + next + i + side) = 0;
                state.at(kPlayoffLive + next + i + side) = 0;
            }
        }
        next_stage++;
        next_game = 0;
        open = true;
    }
    const size_t end = size_t(kPlayoffBase[next_stage] + (8 >> next_stage));
    state.at(kPlayoffTeams + end) = 0xFF;
    state.at(kPlayoffStage) = uint8_t(next_stage);
    state.at(kPlayoffGame) = uint8_t(next_game);
    state.at(kPlayoffCursor) = uint8_t(kPlayoffBase[next_stage]);
    state.at(kPlayoffPending) = uint8_t(open);
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
    // ovl14_2e0f clears last season's achievement flags before a new WM
    // appointment. Its 0x40 achievement then survives the following summer.
    save_.state.at(kManagerBlock + 0xF3) = 0;
    if (human_team_ < 0 || human_team_ >= 12 || team_rank(human_team_) >= 5) return;
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
    save_.state.at(kManagerBlock + 0xF3) |= 0x40;
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

void Game::book_club_match(int own_goals, int goals_against, int competition, bool league_statistics, int shootout_goals) {
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
        const GoalCredit credit = credit_goal(save_.squad, save_.players, brng_, competition, penalty);
        if (credit.scorer < 0) continue;
        auto& scorer = save_.players[size_t(save_.squad[size_t(credit.scorer)].player)];
        if (competition == 0 && !penalty) scorer.league_goals = std::min(255, scorer.league_goals + 1);
        // ovl14_2349 also credits this assist for a successful penalty shot.
        if (credit.assist >= 0) add_assists(save_.players[size_t(save_.squad[size_t(credit.assist)].player)], 1);
    }
    bool goalkeeper_booked = false;
    for (size_t slot = 0; slot < save_.squad.size(); slot++) {
        auto& sp = save_.squad[slot];
        if (sp.player >= 0) {
            save_.state.at(kSquad + slot * kSquadRecord + 0x1A) &= 0x7F;
            sp.status = sp.role == 0 ? SquadStatus::None : SquadStatus::Reserve;
        }
        if (sp.player < 0 || sp.role == 0 || sp.line == 0) continue;
        auto& player = save_.players[size_t(sp.player)];
        if (competition == 2) {
            const size_t at = kSquad + slot * kSquadRecord;
            save_.state.at(at + 8)++;
            const int games = le16(save_.state, at + 0x20) + 1;
            save_.state.at(at + 0x20) = uint8_t(games);
            save_.state.at(at + 0x21) = uint8_t(games >> 8);
        } else (cup ? sp.cup_games : sp.league_games)++;
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
    if (manager == 0) budget_.balance -= fine;
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
    const int team = manager == 0 ? human_team_ : national_team();
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
        if (manager == 0) {
            auto& stored = save_.squad.at(slot);
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
    refresh_national_team();
    const auto national = national_squad();
    HumanSquad squad{&national, &save_.players};
    for(int kind=0;kind<2;kind++)squad.per_line[kind]=line_counts_[5][size_t(kind)];
    squad.minimum_players = 9;
    squad.level = finance_.level;
    squad.selection_rng = &brng_;
    squad.rotation = &national_rotation_;
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
    if (r < 5) {
        for (size_t g = 0; g < 2; g++)
            for (size_t pair = 0; pair < 3; pair++) {
                const HokPairing& p = save_.wm_rounds[g][size_t(r)][pair];
                MatchStats stats;
                const MatchResult res = league_match(save_.teams[size_t(p.home)], save_.teams[size_t(p.away)], rng_, 0,
                                                     p.home == nation ? &squad : nullptr, p.away == nation ? &squad : nullptr, &stats);
                if (p.home == nation || p.away == nation)
                    book_national_match(p.home == nation ? res.home : res.away, stats, p.home == nation ? 0 : 1);
                wm_groups_[g].book(p.home, p.away, res);
                const size_t o = kGroupResults + g * 0x24 + size_t(r) * 6 + pair * 2;
                save_.state.at(o) = uint8_t(res.home);
                save_.state.at(o + 1) = uint8_t(res.away);
            }
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
    const std::vector<Tie> ties = play_cup_round(save_.teams, draw, rng_, HumanSide{nation, &squad, &stats});
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
        if (pl.team >= 0 && pl.team == human_team_) continue;  // a manager's club keeps its players
        for (;;) {
            const int team = brng_.range(r.team_lo, r.team_hi);
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
        r[0x03] = pr[0x1B];
        r[0x06] = pr[0x1C];
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
void Game::record_head_to_head(int home, int away, int home_goals, int away_goals) {
    book_head_to_head(save_.state, home, away, home_goals, away_goals, human_team_);
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
        if (p.team == a)
            p.team = b;
        else if (p.team == b)
            p.team = a;
        // Contract renewal and player redistribution read the native byte
        // before the next export, just as ovl06_0c29 updates it immediately.
        save_.state.at(size_t(p.raw - save_.state.data()) + 0x1D) = uint8_t(p.team);
    }
    if (a < kHokLeagueTeams && b < kHokLeagueTeams) {  // the eternal table and the titles travel with the club
        uint8_t* s = save_.state.data();
        std::swap_ranges(s + kEternal + size_t(a) * 0x50, s + kEternal + size_t(a) * 0x50 + 4, s + kEternal + size_t(b) * 0x50);
        std::swap_ranges(s + kTitles + size_t(a) * 0x50, s + kTitles + size_t(a) * 0x50 + 3, s + kTitles + size_t(b) * 0x50);
    }
    if (a >= kFirstNationalTeam && a < kFirstNationalTeam + 12 &&
        b >= kFirstNationalTeam && b < kFirstNationalTeam + 12) {
        auto history_a = save_.state.begin() + long(kWorldHistory + size_t(a) * 0x50);
        auto history_b = save_.state.begin() + long(kWorldHistory + size_t(b) * 0x50);
        std::swap_ranges(history_a, history_a + 16, history_b);
    }
    if (save_.state.at(0x068C) == a) save_.state.at(0x068C) = uint8_t(b);
    else if (save_.state.at(0x068C) == b) save_.state.at(0x068C) = uint8_t(a);
    for (int* t : {&human_team_, &champion_, &euro_champion_, &cup_winner_}) {  // titles stay with the club
        if (*t == a)
            *t = b;
        else if (*t == b)
            *t = a;
    }
    save_.state.at(kManagerBlock + 0x14) = uint8_t(human_team_);
}

void Game::new_season() {
    const int previous_division = human_team_ >= 0 ? human_team_ / kHokLeagueSize : 3;
    CareerSeason history;
    history.league = std::clamp(budget_.league, 0, 2);
    history.place = human_team_ < 36
        ? leagues_[size_t(human_team_ / 12)].rows[size_t(human_team_ % 12)].position + 1
        : save_.table.at(size_t(human_team_)).position + 1;
    if (history.league == 0 && history.place < 9) {
        history.place = 53;
        int slot = 13;
        for (; slot > 7; slot--) {
            if (slot == 11) history.place--;
            if (save_.state.at(kPlayoffTeams + size_t(slot)) == human_team_) break;
        }
        if (slot == 7) history.place--;
    }
    history.champion = champion_ == human_team_;
    history.cup_round = save_.state.at(kManagerBlock + 0xE6);
    history.cup_winner = cup_winner_ == human_team_;
    history.euro_phase = save_.state.at(kManagerBlock + 0xE7);
    history.euro_winner = euro_champion_ == human_team_ && history.euro_phase != 0 && history.euro_phase != 30;
    append_career_season(save_.state, 0, le16(save_.state, kCareerYear), history);
    // ovl14 clears the annual attendance totals before the new season's games.
    budget_.home_games = 0;
    budget_.attendance_total = budget_.attendance_record = 0;
    budget_.attendance_min = 99999;
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
    const bool scorer_award = scoring_winners[0] == human_team_;
    const bool goalkeeper_award = scoring_winners[1] == human_team_;
    // Native annual flags are rebuilt before licensing and promotion. The two
    // title flags exempt a financially weak club from license withdrawal.
    uint8_t season_flags = uint8_t((save_.state.at(kManagerBlock + 0xF3) & 0x40) |
                                   (champion_ == human_team_ ? 0x08 : 0) |
                                   (cup_winner_ == human_team_ ? 0x10 : 0) |
                                   (history.euro_winner ? 0x20 : 0));
    save_.state.at(kManagerBlock + 0xF3) = season_flags;
    if (champion_ == human_team_) save_.state.at(kManagerBlock + 0x15)++;
    if (history.euro_winner) save_.state.at(kManagerBlock + 0x17)++;
    if (cup_winner_ == human_team_) {
        // ovl14:3e43: booked before the strict -2,000,000 license threshold.
        budget_.balance += kCupPrize;
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
    if (human_team_ >= 0 && human_team_ < 3 * kHokLeagueSize) {
        const HokTableRow& r = leagues_[size_t(human_team_ / kHokLeagueSize)].rows[size_t(human_team_ % kHokLeagueSize)];
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
    auto swap_clubs = [&](int a, int b) {
        swap_teams(a, b);
        std::swap(origin[size_t(a)], origin[size_t(b)]);
    };
    if (financial_license_due(budget_.balance, previous_division, season_flags, 0)) {
        const size_t group = size_t(previous_division);
        auto work = relegation_[group].teams;
        const auto order = relegation_[group].order();
        insert_financial_relegation(work, order, previous_division, human_team_, 0);
        for (size_t slot = 0; slot < work.size(); slot++) {
            // DS:CF20 is the first working schedule row, not DS:CFFC's
            // immutable list of the original relegation participants.
            save_.state.at(kGroupSchedule + 0x48 + group * 0x24 + slot) = uint8_t(work[slot]);
            relegation_upper_[group][slot] = work[size_t(order[slot])];
        }
        season_flags |= 0x04;
        log_.push_back("financial license withdrawn: " + save_.teams[size_t(human_team_)].name);
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
            if (human_team_ == down && (season_flags & 0x04) == 0) {
                // ovl14:440d..4467: a human Oberliga loser receives a
                // replacement balance and an extra full club swap. The
                // common swap below cancels it; both season flags remain.
                season_flags |= 0x02;
                budget_.balance = 1200000;
                swap_clubs(down, up);
                log_.push_back("Oberliga relegation review: account reset to 400000 DM");
            } else {
                log_.push_back("promoted from the regional league: " + save_.teams[size_t(up)].name +
                               ", relegated: " + save_.teams[size_t(down)].name);
            }
            if (human_team_ == up && (season_flags & 0x04) == 0) season_flags |= 0x01;
            swap_clubs(down, up);
        }
    }
    if ((season_flags & 0x04) == 0) {
        const int division = human_team_ / kHokLeagueSize;
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
    // ovl06_099d: the clubs of each division change places at random (55 swaps per division,
    // which reshuffles the schedule), the national teams 20 times
    for (int l = 0; l < 3; l++)
        for (int i = 0; i < 55; i++) {
            const int a = int(rng_.range(l * kHokLeagueSize, l * kHokLeagueSize + kHokLeagueSize - 1));
            const int b = int(rng_.range(l * kHokLeagueSize, l * kHokLeagueSize + kHokLeagueSize - 1));
            if (a != b) swap_clubs(a, b);
        }
    for (int i = 0; i < 20; i++) {
        const int a = int(rng_.range(kFirstNationalTeam, kFirstNationalTeam + 11));
        const int b = int(rng_.range(kFirstNationalTeam, kFirstNationalTeam + 11));
        if (a != b) swap_teams(a, b);
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
    // 162c:1c5b: new season values of the German clubs in their new class
    for (int t = 0; t < kFirstNationalTeam; t++)
        rerate_team(save_.teams[size_t(t)], team_class(finance_, t), rating_, rng_);
    // prizes of the season review (ovl02_0000)
    const int league = team_class(finance_, human_team_);
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
                                     fair_play_winner == human_team_);
    if (promoted)
        for (auto& advert : budget_.adverts) advert.flags |= 0x80;
    if (prize) {
        budget_.balance += prize;
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
            HokPlayer* player=player_by_exe_index(intake->player);
            if(!player)throw FormatError("Season: youth player ID missing");
            player->age=player->raw[0x14];player->skill_a=player->raw[0x15];
            player->skill_b=player->raw[0x16];player->form=player->raw[0x17];
            player->team=player->raw[0x1d];
            if(intake->shifted)
                for(size_t slot=injured_role_.size()-1;slot>size_t(intake->slot);--slot)
                    injured_role_[slot]=injured_role_[slot-1];
            injured_role_[size_t(intake->slot)]=0;
            save_.squad=hok_squad(save_,0);
            log_.push_back(player->name+" joins from the youth team");
        }
        refresh_budget_roster();
    }
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
        const auto review=annual_owned_player(save_,uint8_t(id),0,annual_draw);
        if(review.status==AnnualPlayerStatus::Retired||review.status==AnnualPlayerStatus::Disabled){
            budget_.balance+=review.insurance_payment;
            if(review.insurance_payment)
                log_.push_back(pl.name+" disability insurance payment "+std::to_string(review.insurance_payment/3)+" DM");
            log_.push_back(pl.name+(review.status==AnnualPlayerStatus::Disabled?" leaves after permanent injury":" retires from ice hockey"));
            std::move(injured_role_.begin()+review.slot+1,injured_role_.end(),injured_role_.begin()+review.slot);
            injured_role_.back()=0;
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
        sp.league_games = sp.cup_games = sp.league_goals = sp.cup_goals = sp.other_goals = 0;
        sp.goals_against = sp.league_goals_byte = sp.cup_goals_byte = sp.other_goals_byte = 0;
        sp.major_penalties = sp.minor_penalties = 0;
        r[0x00] = r[0x01] = 0;
    }
    log_.push_back(std::to_string(retired) + " players retired");
    manager_offers_.clear();  // the offer flags are cleared above (ovl02_0000)
    // power play counters (manager block +0x177) start from 0 again (NEUSAIS); the series records stay
    std::fill(save_.state.begin() + long(kSpecialTeams), save_.state.begin() + long(kSpecialTeams + 12), uint8_t(0));
    expire_contracts();
    redistribute_players(0);
    redistribute_players(1);
    budget_.league = team_class(finance_, human_team_);
    budget_.team = human_team_;
    refresh_budget_roster();
    const auto& summer_team = save_.teams.at(size_t(human_team_));
    budget_.team_strength = summer_team.skill1[0] + summer_team.skill1[1] +
                            summer_team.skill2[0] + summer_team.skill2[1];
    // ovl14:462c..470b: May 1 (234) through September 9 (365).
    // There are no matches, transfer offers or normal training in this loop.
    for (int summer_day = 234; summer_day <= 365; summer_day++) {
        const GameDate summer_date = date_of_season_day(summer_day, save_.year);
        if (summer_day % 7 == 0) stadium_week(finance_, budget_, save_.state, 0, brng_);
        const int month_days = kMonthDays[size_t(summer_date.month - 1)];
        stadium_daily_refusal_reset(stadium_refusals_,0,brng_);
        if (summer_date.day == month_days / 2 || summer_date.day == month_days) {
            // ovl08_1d61 calls 2ba7 with the stored image. Unlike the
            // playable daily preparation, summer does not recompute it.
            update_sponsors();
        }
        if (summer_date.day == month_days) clear_insurance_locks();
        finance_day(finance_, budget_, summer_date.day, kMonthDays[size_t(summer_date.month - 1)]);
        heal_squad(summer_day % 7 == 0);
    }
    // a fresh season: tables, schedule position, cup draw, competitions
    for (int l = 0; l < 3; l++) {
        League& lg = leagues_[size_t(l)];
        lg.first_team = l * kHokLeagueSize;
        for (int s = 0; s < kHokLeagueSize; s++) {
            lg.rows[size_t(s)] = HokTableRow{};
            lg.rows[size_t(s)].form = "--------";
            lg.rows[size_t(s)].position = s;
            lg.ranking[size_t(s)] = s;
        }
        matchday_[size_t(l)] = 1;
    }
    cup_draw_ = draw_initial_cup({human_team_}, brng_);
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
    save_.state.at(0x8F84) = 1;
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
    std::fill(save_.state.begin() + long(kGroupResults + 0x48), save_.state.begin() + long(kGroupResults + 0x90), uint8_t(0));
    std::fill(save_.state.begin() + long(kPlayoffScores), save_.state.begin() + long(kPlayoffGame + 1), uint8_t(0));
    std::fill(save_.state.begin() + long(kPlayoffTeams), save_.state.begin() + long(kPlayoffTeams + 32), uint8_t(0xFF));
    save_.state.at(kPlayoffStage) = 0;
    save_.state.at(kPlayoffCursor) = 0xFF;
    save_.state.at(kPlayoffPending) = 0;
    std::fill(save_.state.begin() + long(kLeagueResults), save_.state.begin() + long(kLeagueResults + 0x630), uint8_t(0));
    budget_.league = team_class(finance_, human_team_);
    budget_.team = human_team_;
    save_.year++;
    const uint16_t career_year = uint16_t(le16(save_.state, kCareerYear) + 1);
    save_.state.at(kCareerYear) = uint8_t(career_year);
    save_.state.at(kCareerYear + 1) = uint8_t(career_year >> 8);
    save_.season_year = save_.year;  // DS:03BB follows at the season change (NEUSAIS)
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
    // The first playable menu already includes September 10's booking.
    stadium_daily_refusal_reset(stadium_refusals_,0,brng_);
    finance_day(finance_, budget_, 10, 30);
    log_.push_back("new season " + std::to_string(save_.year) + "/" + std::to_string(save_.year + 1));
}

Bytes Game::save_bytes() const {
    HokFile h = clone_hok(save_);
    // date and matchdays
    const GameDate next = date_of_season_day(day_ + 1, h.year);  // the day to play after loading
    h.date.day = next.day;
    h.date.month = next.month;
    h.date.year = next.year;
    h.date.day_index = day_ + 1;
    for (int l = 0; l < 3; l++) {
        h.matchday[l] = matchday_[size_t(l)];
        const League& lg = leagues_[size_t(l)];
        for (int s = 0; s < kHokLeagueSize; s++) {
            HokTableRow row = lg.rows[size_t(s)];
            if (row.form.size() < 8) row.form.insert(0, 8 - row.form.size(), '-');
            h.table[size_t(lg.first_team + s)] = row;
        }
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
    if (playoff_day_ > 0)
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
    store_budget(h, budget_, 0);
    store_sponsor_offers(s, offers_, 0);
    s[kManagerBlock + 0x173] = uint8_t(image_);
    s[kManagerBlock + 0x174] = uint8_t(image_ >> 8);
    // plain header: calendar year of the next day (MONAT4: 1994), next matchday of the manager's league
    h.header[0x0A] = uint8_t(next.year);
    h.header[0x0B] = uint8_t(next.year >> 8);
    h.header[0x0F] = uint8_t(matchday_[size_t(std::clamp(budget_.league, 0, 2))]);
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

void Game::advance_day() {
    interactive_played_ = false;
    expire_advert_options();
    if (day_ + 1 >= int(calendar_.events.size())) {
        // Native club-menu saves can already show April 30 while WM rounds are still
        // pending. Complete one such event per call before rolling the season over;
        // do not charge that displayed day's finances again.
        if (wm_round_ > 0 && wm_round_ < 8) {
            play_wm_day();
            return;
        }
        new_season();
    }
    refresh_human_team();  // 162c:146c, daily
    // Native6AF8:313E..3158 preserves the menu's latest selection for
    // the next menu, then disables automatic selection during this match.
    save_.state.at(0x078B)=save_.state.at(0x078A);
    save_.state.at(0x078A)=0;
g_draw_scope = "transfer-list";
    if (brng_.range(0, kHokManagers + 2) == 0) refresh_transfer_list();  // ovl14_2e0f, DS:0386 managers
    day_++;
    const uint8_t event = calendar_.event(day_);
    human_played_ = false;
    if (calendar_.league_day(day_)) {
        //6AF8:3178..3198 updates every club manager before the league games.
        //Game currently simulates manager0; all clubs use first-league DS07A9.
g_draw_scope = "youth";
        youth_fixture_day(save_.state,finance_,std::max(0, matchday_[0] - 1),0,[&](int lo,int hi){return brng_.range(lo,hi);});
g_draw_scope = "league-day";
        play_league_day();
        human_played_ = human_team_ >= 0 && human_team_ < 3 * kHokLeagueSize;
    }
g_draw_scope = "cup/euro/playoff/wm";
    if (event == kEventCup) play_cup_day();
    if (event == kEventEuroCup) play_euro_day();
    if (event == kEventPlayoff) play_playoff_day();
    if (event == kEventWorldChampionship) play_wm_day();
    if (event == kEventSectionEnd && calendar_.event(day_ + 1) == kEventWorldChampionship)
        begin_world_championship();
g_draw_scope = "match-injuries";
    // After the matches of the day the original leaves every team of the 150 with fresh line energy 90 (whole-program test:
    // even clubs that did not play); the weekly drift below overwrites the byte +0x1E afterwards.
    for (HokTeam& team : save_.teams)
        for (int kind = 0; kind < 2; kind++)
            for (int line = 0; line < 4; line++) team.energy[kind][line] = 90;
    if (human_played_) match_injuries();
    const bool world_phase = wm_round_ > 0 || event == kEventWorldChampionship;
    // The WM branch selects manager 5 on a match date; 146e:0c89's
    // human-manager guard then excludes the club. Empty dates retain club
    // processing before the next date's finances (ovl14:3727..388f).
    if (world_phase && event == 0 && (day_ % 7 == 0 || (day_ + 3) % 7 == 0)) {
        weekly_team_drift();
        squad_injuries(false, true);  // today's healing was already prepared
        make_offers();
        train_squad();
    }
    // A native menu save is before today's match, but today's finances are already booked.
    // After playing this day, prepare the finances of the next (saveable) date. Charging the
    // played day again duplicates its interest on import and exports month-start contracts late.
    const GameDate finance_date = date_of_season_day(day_ + 1, save_.year);
    const int finance_month_days = kMonthDays[size_t(finance_date.month - 1)];
    refresh_budget_roster();
    const long before = budget_.balance;
g_draw_scope = "refusal-reset";
    stadium_daily_refusal_reset(stadium_refusals_,0,brng_);
    if (finance_date.day == finance_month_days / 2 || finance_date.day == finance_month_days)
        update_sponsors();
    if (finance_date.day == finance_month_days) clear_insurance_locks();
    // ovl08_1d61 (daily finance of the manager): its 11 draws (0,10) are the stadium refusal reset above, then the share
    // market (ovl39_03ca; REAL-CODE test realcode_market); the finance view mirrors the state.
g_draw_scope = "market";
    market_day(save_.state, brng_);
    for (int c = 0; c < kHokShares; c++) {
        save_.finance.shares[c] = le32(save_.state, 0x30 + size_t(c) * 8 + 4);
        for (int e = 0; e < kHokShareHistory; e++) save_.finance.share_price[c][e] = le16(save_.state, 0xB0 + size_t(c) * 72 + size_t(e) * 2) / 3;
    }
g_draw_scope = "finance";
    finance_day(finance_, budget_, finance_date.day, finance_month_days);
    if (finance_date.day == finance_month_days) {
        char line[128];
        std::snprintf(line, sizeof line, "month end %02d.%02d.: balance %ld DM (%+ld DM)", finance_date.day, finance_date.month,
                      budget_.balance / 3, (budget_.balance - before) / 3);
        log_.push_back(line);
    }
g_draw_scope = "image";
    image_ = current_club_image();  // ovl08_2893, after each daily finance booking
    // Club branch 6af8:3374..3443: advance date, finance, then the
    // twice-weekly training and finally stadium. The WM preparation branch
    // instead advances the stadium and healing without normal club training.
    if (!world_phase && ((day_ + 1) % 7 == 0 || (day_ + 4) % 7 == 0)) {
g_draw_scope = "weekly-injuries-offers-training";
        weekly_team_drift();
        squad_injuries((day_ + 1) % 7 == 0, calendar_.event(day_ + 1) == 0);
        make_offers();
        train_squad();
    }
g_draw_scope = "stadium-week";
    if ((day_ + 1) % 7 == 0) stadium_week(finance_, budget_, save_.state, 0, brng_);
    if (world_phase) heal_squad((day_ + 1) % 7 == 0);
    prepare_human_lineup();
}

}  // namespace em
