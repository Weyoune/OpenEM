// SPDX-License-Identifier: GPL-3.0-only
#include "game/game.hpp"

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
constexpr int kManagers = 1;                                 // DS:0386: only manager 0 plays
// state offsets of the season review values: champion DS:D0E6, European cup winner DS:0388, and
// the German European cup entrants of ovl05_51a5 (DS:A776 list, DS:A763 slots 3 x 6, 0x80 = free)
constexpr size_t kStateChampion = 0x0DC8, kStateEuroWinner = 0x0DC9, kStateEntrants = 0x8E33,
                 kStateEntrantSlots = 0x8E3D;

void put32(Bytes& b, size_t o, long v) {
    for (int i = 0; i < 4; i++) b.at(o + size_t(i)) = uint8_t(uint32_t(v) >> (8 * i));
}
constexpr int kMonday = 1, kFriday = 5;  // the two calls of 146e:0c89 per week

}  // namespace

Game::Game(const Bytes& exe, const Bytes& save, uint32_t seed)
    : save_(decode_hok(save)),
      schedule_(load_schedule_tables(exe)),
      calendar_(load_season_calendar(exe)),
      finance_(load_finance_tables(exe)),
      contracts_(load_contract_tables(exe)),
      sponsor_tables_(load_sponsor_tables(exe)),
      training_(load_training_tables(exe)),
      injuries_(load_injury_tables(exe)),
      rating_(load_rating_bounds(exe)),
      rng_(seed),
      brng_(seed) {
    for (int l = 0; l < 3; l++) {
        leagues_[size_t(l)] = league_from_save(save_, l);
        matchday_[size_t(l)] = save_.matchday[l];
    }
    for (size_t t = 0; t < save_.teams.size(); t++)
        if (save_.teams[t].name == save_.club) human_team_ = int(t);
    // the header holds the calendar year of the save (1994 in MONAT4, February), DS:03BB the year
    // the season started; the day loop counts from the season start
    if (save_.season_year > 0) save_.year = save_.season_year;
    human_.squad = &save_.squad;
    human_.players = &save_.players;
    human_.level = finance_.level;
    budget_ = budget_from_save(save_, 0);
    for (int d = 0; d < kTrainingDays; d++)
        for (int u = 0; u < kTrainingUnits; u++) plan_[size_t(d)][size_t(u)] = save_.state[0x070C + size_t(d * 4 + u)];
    image_ = le16(save_.state, 0x0DCD + 0x173);
    cup_draw_ = save_.cup_draw;
    for (size_t g = 0; g < 2; g++) euro_groups_[g].teams = save_.euro_groups[g];
    // the save's day has not been played yet: the next advance_day() handles it
    day_ = save_.date.day_index - 1;
    for (int d = 0; d < day_; d++) {
        cup_day_ += calendar_.event(d) == kEventCup;
        euro_round_ += calendar_.event(d) == kEventEuroCup;
        playoff_day_ += calendar_.event(d) == kEventPlayoff;
    }
    // A save made during the world championship: its group tables and knock-out slots are not
    // read yet, so the tournament restarts from its first round
    reset_wm();
}

int Game::team_rank(int team) const {
    for (int l = 0; l < 3; l++) {
        const League& lg = leagues_[size_t(l)];
        if (team >= lg.first_team && team < lg.first_team + kHokLeagueSize)
            return lg.rows[size_t(team - lg.first_team)].position;
    }
    return 5;
}

void Game::home_game(int guest, int kind, int home_class, int guest_class, int home_rank, int guest_rank) {
    AttendanceMatch m;
    m.home_class = home_class;
    m.away_class = guest_class;
    m.home_strength = attendance_strength(save_.teams[size_t(human_team_)]);
    m.away_strength = attendance_strength(save_.teams[size_t(guest)]);
    m.home_rank = home_rank;
    m.away_rank = guest_rank;
    m.guest = guest;
    m.kind = kind;
    const long spectators = home_attendance(finance_, budget_, m, brng_);
    char line[160];
    std::snprintf(line, sizeof line, "  %ld spectators against %s, %ld DM tickets", spectators,
                  save_.teams[size_t(guest)].name.c_str(), long(budget_.ticket_price) * spectators);
    log_.push_back(line);
}

// Series (shown on "Totale"): DS:B57B + team * 0x15 (state 0x9988), 7 kinds x {all, home, away};
// the manager's records at DS:BABB + manager * 0x15 (state 0x9EC8). Kinds: won, lost, drawn, not
// won, not lost, without goal against, without goal scored. Checked against MONAT1's screen.
void Game::update_series(int team, int side, int goals_for, int goals_against) {
    if (team < 0 || team >= kSeriesTeams) return;
    const bool cond[7] = {goals_for > goals_against, goals_for < goals_against, goals_for == goals_against,
                          goals_for <= goals_against, goals_for >= goals_against, goals_against == 0, goals_for == 0};
    uint8_t* cur = save_.state.data() + kSeries + size_t(team) * 0x15;
    uint8_t* rec = save_.state.data() + kSeriesRecord;
    for (size_t k = 0; k < 7; k++)
        for (size_t col : {size_t(0), size_t(side) + 1}) {
            uint8_t& v = cur[k * 3 + col];
            v = cond[k] ? uint8_t(std::min(255, v + 1)) : 0;
            if (team == human_team_) rec[k * 3 + col] = std::max(rec[k * 3 + col], v);
        }
}

// Power play and short-handed figures of manager 0 (u16 at state 0x0F44.., "Totale").
void Game::book_special_teams(const MatchStats& st, int side) {
    const int other = 1 - side;
    // goals in power play, goals against in power play, goals short-handed, goals against
    // short-handed, power plays, penalty kills (order of the screen; MONAT1: 6 0 0 11 36 39)
    const int add[6] = {st.power_play_goals[side], st.short_handed_goals[other], st.short_handed_goals[side],
                        st.power_play_goals[other], st.penalties[other], st.penalties[side]};
    for (size_t i = 0; i < 6; i++) {
        const size_t o = kSpecialTeams + 2 * i;
        const int v = le16(save_.state, o) + add[i];
        save_.state[o] = uint8_t(v);
        save_.state[o + 1] = uint8_t(v >> 8);
    }
}

void Game::play_league_day() {
    for (int l = 0; l < 3; l++) {
        const int md = matchday_[size_t(l)];
        if (md < 1 || md > 4 * (kHokLeagueSize - 1)) continue;
        League& lg = leagues_[size_t(l)];
        const auto pairs = round_robin_pairings(schedule_.teams12, md - 1);
        MatchStats stats;
        const auto results = play_matchday(lg, save_.teams, schedule_, md - 1, rng_, &human_, human_team_, &stats);
        matchday_[size_t(l)]++;
        for (size_t k = 0; k < pairs.size(); k++) {
            const int hi = lg.first_team + pairs[k].home, ai = lg.first_team + pairs[k].away;
            update_series(hi, 0, results[k].home, results[k].away);
            update_series(ai, 1, results[k].away, results[k].home);
            update_records(hi, ai, results[k].home, results[k].away);
            // 1238:114f: scorers of the computer clubs (player records), home first
            if (hi != human_team_) credit_club_scorers(save_.players, save_.state, hi, results[k].home, results[k].away, true, brng_);
            if (ai != human_team_) credit_club_scorers(save_.players, save_.state, ai, results[k].away, results[k].home, true, brng_);
            if (hi != human_team_ && ai != human_team_) continue;
            record_head_to_head(hi, ai, results[k].home, results[k].away);
            book_special_teams(stats, hi == human_team_ ? 0 : 1);
            char line[160];
            std::snprintf(line, sizeof line, "league matchday %d: %s - %s %d:%d%s", md, save_.teams[size_t(hi)].name.c_str(),
                          save_.teams[size_t(ai)].name.c_str(), results[k].home, results[k].away,
                          results[k].shootout ? " (shoot-out)" : "");
            log_.push_back(line);
            const int own = hi == human_team_ ? results[k].home : results[k].away;
            for (int g = 0; g < own; g++) credit_goal(save_.squad, save_.players, brng_);
            for (HokSquadPlayer& sp : save_.squad)  // every player in a line plays (squad +0x15)
                if (sp.line != 0) {
                    sp.league_games++;
                    save_.players[size_t(sp.player)].league_games++;  // player +0x1C: the scorer list divides by it
                    sp.status = SquadStatus::Team;  // +0x1A bit 7, as after a game in the original
                }
            if (hi == human_team_)
                home_game(ai, 0, team_class(finance_, hi), team_class(finance_, ai), team_rank(hi), team_rank(ai));
        }
    }
}

void Game::play_cup_day() {
    const bool first_of_round = cup_day_ % 3 == 0;
    cup_day_++;
    if (!first_of_round || cup_draw_.empty()) return;
    HumanSide side{human_team_, &human_};
    const auto ties = play_cup_round(save_.teams, cup_draw_, rng_, side);
    std::vector<int> winners;
    for (const Tie& t : ties) {
        winners.push_back(t.winner());
        update_records(t.home, t.away, t.result.home, t.result.away);  // DS:D0C2 = 8 counts too
        if (t.home != human_team_ && t.away != human_team_) continue;
        human_played_ = true;
        record_head_to_head(t.home, t.away, t.result.home, t.result.away);
        for (HokSquadPlayer& sp : save_.squad)
            if (sp.line != 0) {
                sp.cup_games++;
                sp.status = SquadStatus::Team;
            }
        char line[160];
        std::snprintf(line, sizeof line, "DEB cup: %s - %s %d:%d%s", save_.teams[size_t(t.home)].name.c_str(),
                      save_.teams[size_t(t.away)].name.c_str(), t.result.home, t.result.away,
                      t.result.shootout ? " (shoot-out)" : "");
        log_.push_back(line);
        if (t.home == human_team_)
            home_game(t.away, 1, team_class(finance_, t.home), team_class(finance_, t.away), team_rank(t.home),
                      team_rank(t.away));
    }
    cup_draw_ = winners.size() > 1 ? draw_cup_round(winners, brng_) : std::vector<HokPairing>{};
    if (winners.size() == 1) {
        cup_winner_ = winners[0];
        log_.push_back("DEB cup winner: " + save_.teams[size_t(winners[0])].name);
    }
}

// 1000:3d1a on squad slot `slot` (see injuries.hpp). The role is remembered so that the player
// can return to his line when he is fit again (the original leaves that to the manager).
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
}

// 146e:0c89, called on Mondays (weekly part) and Fridays: the injury countdown 146e:1732 once a
// week, then the training injuries of every fit player.
void Game::squad_injuries(bool weekly, bool no_match_today) {
    for (size_t i = 0; i < save_.squad.size() && i < size_t(kHokSquadSlots); i++) {
        uint8_t* r = save_.state.data() + kSquad + i * kSquadRecord;
        if (save_.squad[i].player < 0) continue;
        if (weekly && (r[0x09] & 2) && r[0x0D] != kCareerOver && r[0x0D] != 0) r[0x0D]--;
        if (weekly && (r[0x09] & 3) && r[0x0D] == 0) {
            r[0x09] &= 0xFC;
            if (injured_role_[i]) {  // back in his line (stand-in for the manager)
                r[0x0A] = uint8_t(injured_role_[i]);
                save_.squad[i].role = injured_role_[i];
                injured_role_[i] = 0;
                log_.push_back(save_.players[size_t(save_.squad[i].player)].name + " is fit again");
            }
        }
        if (r[0x0D] == 0 && r[0x09] == 0 && training_injury(r[0x13], no_match_today, finance_.level, brng_)) injure(i, 0);
    }
}

// ovl06_364f once per minute of a match of the human club (here after the match, so a player hurt
// in it misses the next match on), ovl06_2385 picks the player.
void Game::match_injuries() {
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

// ovl09_25d6 with force: the records behind `slot` move up one place, the last one is cleared.
void Game::remove_squad_record(int block, int slot, int slots) {
    uint8_t* base = save_.state.data() + kSquad + size_t(block) * 0x618;
    std::memmove(base + size_t(slot) * kSquadRecord, base + size_t(slot + 1) * kSquadRecord,
                 size_t(slots - 1 - slot) * kSquadRecord);
    std::memset(base + size_t(slots - 1) * kSquadRecord, 0, kSquadRecord);
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
        o.amount = listed ? club_offer(value, brng_) : sale_offer(value, bidder > 63, brng_);
        manager_offers_.push_back(o);
        char line[160];
        std::snprintf(line, sizeof line, "offer: %s bids %ld DM for %s", save_.teams[size_t(bidder)].name.c_str(),
                      o.amount / 3, pl->name.c_str());
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
        remove_squad_record(0, int(slot), kHokSquadSlots);
        save_.squad.erase(save_.squad.begin() + long(slot));
        std::move(injured_role_.begin() + long(slot) + 1, injured_role_.end(), injured_role_.begin() + long(slot));
        injured_role_.back() = 0;
        save_.state[kPlayers + size_t(exe_index - 1) * kPlayerRecord + 0x1A] = 6;
        pl.team = -1;
        log_.push_back(pl.name + " leaves the club, " + std::to_string(fee / 3) + " DM transfer fee");
    }
}

bool Game::accept_offer(size_t i) {
    if (i >= manager_offers_.size()) return false;
    const ManagerOffer o = manager_offers_[i];
    manager_offers_.erase(manager_offers_.begin() + long(i));
    HokPlayer& pl = save_.players[size_t(o.player)];
    const int exe_index = int((pl.raw - save_.state.data() - kPlayers) / kPlayerRecord) + 1;
    uint8_t* st = save_.state.data();
    for (int slot = 0; slot < kTransferListSlots; slot++)  // off the transfer list
        if (st[kSquad + 4 * 0x618 + size_t(slot) * kSquadRecord + 0x0F] == exe_index) {
            remove_squad_record(4, slot, kTransferListSlots);
            break;
        }
    for (size_t slot = 0; slot < save_.squad.size(); slot++)  // out of the squad
        if (save_.squad[slot].player == o.player) {
            remove_squad_record(0, int(slot), kHokSquadSlots);
            save_.squad.erase(save_.squad.begin() + long(slot));
            std::move(injured_role_.begin() + long(slot) + 1, injured_role_.end(), injured_role_.begin() + long(slot));
            injured_role_.back() = 0;
            break;
        }
    st[kPlayers + size_t(exe_index - 1) * kPlayerRecord + 0x1A] = 6;
    pl.team = o.bidder;
    budget_.balance += o.amount;
    log_.push_back(pl.name + " sold to " + save_.teams[size_t(o.bidder)].name + " for " +
                   std::to_string(o.amount / 3) + " DM");
    refresh_human_team();
    return true;
}

void Game::decline_offer(size_t i) {
    if (i >= manager_offers_.size()) return;
    const ManagerOffer o = manager_offers_[i];
    manager_offers_.erase(manager_offers_.begin() + long(i));
    for (size_t slot = 0; slot < save_.squad.size(); slot++)
        if (save_.squad[slot].player == o.player) save_.state[kSquad + slot * kSquadRecord + 0x09] &= 0x3F;
}

void Game::train_squad() {
    budget_.balance -= budget_.open_97ee;  // weekly recovery costs (146e:0c89)
    for (size_t i = 0; i < save_.squad.size(); i++) {
        HokSquadPlayer& sp = save_.squad[i];
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
        sp.skill_a = p.condition;
        sp.skill_b = p.technique;
        sp.nibbles = p.nibbles;
        sp.exhaustion = p.exhaustion * 2 / 3;
    }
    refresh_human_team();
}

// 162c:146c (called by the day loop): the human club's team record follows its squad. Per kind
// the line strengths (FUN_162c_16bf, human branch, not in play, no man advantage) of lines 0..2
// are summed and divided by 3. The monthly income (finances) uses these values.
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

void Game::update_sponsors() {
    ImageInputs in;
    in.league = budget_.league;
    in.matchday = matchday_[size_t(std::clamp(budget_.league, 0, 2))];
    const int team = human_team_;
    for (int j = std::max(in.matchday - 5, 1); j < in.matchday; j++) {
        // ovl10_0a9d reads the fever curve byte: odd matchdays the high nibble, even ones the
        // whole byte (original quirk); index = matchday / 2
        const HokTableRow& row = leagues_[size_t(in.league)].rows[size_t(team - leagues_[size_t(in.league)].first_team)];
        const int lo = row.position_history[size_t((j / 2) * 2)], hi = (j / 2) * 2 + 1 < 44 ? row.position_history[size_t((j / 2) * 2 + 1)] : 0;
        const int byte = lo | hi << 4;
        in.recent_results.push_back(j % 2 ? hi : byte);
    }
    for (int slot = 0; slot < kHokSquadSlots; slot++) {
        int p = -1;
        ContractPlayer c = contract_player(save_, 0, slot, &p);
        if (p >= 0) in.squad_values.push_back(c.value);
    }
    in.stadium = stadium_score(sponsor_tables_, budget_.stadium, budget_.places);
    image_ = club_image(in, brng_);
    generate_sponsor_offers(sponsor_tables_, image_, false, offers_, brng_);
    char line[96];
    std::snprintf(line, sizeof line, "image %d, sponsor offers renewed", image_);
    log_.push_back(line);
}

void Game::play_euro_day() {
    const int r = euro_round_++;  // DS:07AF
    auto game = [&](GroupTable& table, int home, int away) {
        const MatchResult res = league_match(save_.teams[size_t(home)], save_.teams[size_t(away)], rng_, 0,
                                             home == human_team_ ? &human_ : nullptr,
                                             away == human_team_ ? &human_ : nullptr);
        table.book(home, away, res);
        if (home != human_team_ && away != human_team_) return;
        human_played_ = true;
        record_head_to_head(home, away, res.home, res.away);
        char line[160];
        std::snprintf(line, sizeof line, "European cup: %s - %s %d:%d", save_.teams[size_t(home)].name.c_str(),
                      save_.teams[size_t(away)].name.c_str(), res.home, res.away);
        log_.push_back(line);
        if (home == human_team_)  // DS:D0C2 = 3 in the groups, 5 in the final round
            home_game(away, r < 6 ? 2 : 3, team_class(finance_, home), team_class(finance_, away), team_rank(home), 5);
    };
    if (r < 6) {
        for (size_t g = 0; g < 2; g++)
            for (const HokPairing& p : save_.euro_rounds[g][size_t(r)]) game(euro_groups_[g], p.home, p.away);
        if (r == 5) {  // ovl05_0ef7(3): the two best of each group play the final round
            const auto a = euro_groups_[0].order(), b = euro_groups_[1].order();
            euro_final_ = GroupTable{};
            euro_final_.teams = {euro_groups_[0].teams[size_t(a[0])], euro_groups_[1].teams[size_t(b[0])],
                                 euro_groups_[0].teams[size_t(a[1])], euro_groups_[1].teams[size_t(b[1])]};
            std::string line = "European cup final round:";
            for (int t : euro_final_.teams) line += " " + save_.teams[size_t(t)].name + ";";
            log_.push_back(line);
        }
    } else if (r < 12) {  // final round: double round robin of four (letter table DS:0474)
        for (const Pairing& p : round_robin_pairings(schedule_.teams4, r - 6))
            game(euro_final_, euro_final_.teams[size_t(p.home)], euro_final_.teams[size_t(p.away)]);
        if (r == 11) {
            euro_champion_ = euro_final_.teams[size_t(euro_final_.order()[0])];
            log_.push_back("European cup winner: " + save_.teams[size_t(euro_champion_)].name);
        }
    }
}

void Game::play_playoff_day() {
    const int day = playoff_day_++;
    const int stage = day / 5;  // five days per best-of-five round
    HumanSide side{human_team_, &human_};
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
            }
        } else {  // bracket (1-8 / 4-5) and (2-7 / 3-6), then the final
            std::vector<int> w;
            for (const Series& s : series_) w.push_back(s.winner());
            if (stage == 1 && w.size() == 4) teams = {w[0], w[3], w[1], w[2]};
            if (stage == 2 && w.size() == 2) teams = {w[0], w[1]};
        }
        series_.clear();
        for (size_t i = 0; i + 1 < teams.size(); i += 2) {
            Series s;
            s.higher = position(teams[i]) < position(teams[i + 1]) ? teams[i] : teams[i + 1];
            s.lower = s.higher == teams[i] ? teams[i + 1] : teams[i];
            series_.push_back(s);
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
        human_played_ = true;
        record_head_to_head(home, away, r.home, r.away);
        char line[160];
        std::snprintf(line, sizeof line, "play-offs: %s - %s %d:%d (series %d:%d)", save_.teams[size_t(home)].name.c_str(),
                      save_.teams[size_t(away)].name.c_str(), r.home, r.away, s.wins_higher, s.wins_lower);
        log_.push_back(line);
        if (home == human_team_) home_game(away, 1, 0, 0, position(home), position(away));
    }
    // relegation rounds on the first and third day of each play-off round (round index =
    // DS:07B0 * 2 + [DS:CCA3 = 2]): a double round robin of four, letter table DS:0474
    if (day % 5 == 0 || day % 5 == 2) {
        const int round = stage * 2 + (day % 5 == 2 ? 1 : 0);
        for (size_t g = 0; g < 2; g++) {
            GroupTable& t = relegation_[g];
            for (const Pairing& p : round_robin_pairings(schedule_.teams4, round)) {
                const int home = t.teams[size_t(p.home)], away = t.teams[size_t(p.away)];
                const MatchResult r = league_match(save_.teams[size_t(home)], save_.teams[size_t(away)], rng_, 0,
                                                   home == human_team_ ? &human_ : nullptr,
                                                   away == human_team_ ? &human_ : nullptr);
                t.book(home, away, r);
                if (home != human_team_ && away != human_team_) continue;
                human_played_ = true;
                record_head_to_head(home, away, r.home, r.away);
                char line[160];
                std::snprintf(line, sizeof line, "relegation round %d: %s - %s %d:%d", round + 1,
                              save_.teams[size_t(home)].name.c_str(), save_.teams[size_t(away)].name.c_str(), r.home, r.away);
                log_.push_back(line);
                if (home == human_team_) home_game(away, 1, team_class(finance_, home), team_class(finance_, away), 5, 5);
            }
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
// out in the groups get 11 there). The prize money for a manager's nation (DS:06AC) is not kept.
void Game::play_wm_day() {
    const int r = wm_round_++;
    if (r < 5) {
        for (size_t g = 0; g < 2; g++)
            for (const HokPairing& p : save_.wm_rounds[g][size_t(r)]) {
                const MatchResult res = league_match(save_.teams[size_t(p.home)], save_.teams[size_t(p.away)], rng_, 0);
                wm_groups_[g].book(p.home, p.away, res);
            }
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
    const std::vector<Tie> ties = play_cup_round(save_.teams, draw, rng_, HumanSide{});
    for (size_t k = 0; k < ties.size(); k++) {
        const Tie& t = ties[k];
        const int loser = t.winner() == t.home ? t.away : t.home;
        wm_slots_[base + 2 * k] = t.winner();
        wm_slots_[base + 2 * k + 1] = loser;
    }
    if (r < 7) {
        for (size_t i = base; i < base + 8; i++) wm_slots_[size_t(kNextSlot[i] - 1)] = wm_slots_[i];
        return;
    }
    wm_ranking_.assign(wm_slots_.begin() + 16, wm_slots_.end());
    wm_champion_ = wm_ranking_[0];
    char line[160];
    std::snprintf(line, sizeof line, "World championship: %s beats %s %d:%d", save_.teams[size_t(ties[0].home)].name.c_str(),
                  save_.teams[size_t(ties[0].away)].name.c_str(), ties[0].result.home, ties[0].result.away);
    log_.push_back(line);
    log_.push_back("World champion: " + save_.teams[size_t(wm_champion_)].name);
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
//   2. 10 - kept times a slot rand(0, 9) is drawn: if it holds a listed player the try is lost,
//      else it is cleared and a candidate is drawn (pick_transfer_candidate; no candidate in 1500
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
    constexpr int kSlots = kTransferListSlots;
    uint8_t* st = save_.state.data();
    auto rec = [&](int slot) { return st + kBlock + size_t(slot) * kSquadRecord; };
    auto player = [&](int exe_index) -> HokPlayer* {  // EXE index = HOK record + 1
        for (HokPlayer& pl : save_.players)
            if (pl.raw == st + kPlayers + size_t(exe_index - 1) * kPlayerRecord) return &pl;
        return nullptr;
    };
    auto owner = [&](int exe_index) -> uint8_t& { return st[kPlayers + size_t(exe_index - 1) * kPlayerRecord + 0x1A]; };
    auto remove = [&](int slot) {  // ovl09_25d6: the slots behind move up, the last one is cleared
        std::memmove(rec(slot), rec(slot + 1), size_t(kSlots - 1 - slot) * kSquadRecord);
        std::memset(rec(kSlots - 1), 0, kSquadRecord);
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
        if (old && owner(old) == 4) continue;  // the try is lost
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
        if (at >= kSlots) continue;
        std::memmove(rec(at + 1), rec(at), size_t(kSlots - 1 - at) * kSquadRecord);
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
    auto put = [&](int opp, int side, int own, int against) {
        if (opp < 0 || opp >= kSeriesTeams) return;  // the block has 64 rows
        uint8_t* row = save_.state.data() + kHeadToHead + size_t(opp) * 10;
        const uint8_t v = uint8_t(std::min(against, 15) << 4 | std::min(own, 15));
        std::vector<uint8_t*> slots;  // the column, or both for nations and later teams
        if (opp < kFirstNationalTeam) {
            for (int i = 0; i < 5; i++) slots.push_back(row + 2 * i + side);
        } else {
            for (int i = 0; i < 5; i++) slots.push_back(row + 2 * i);
            for (int i = 0; i < 5; i++) slots.push_back(row + 2 * i + 1);
        }
        for (uint8_t* p : slots)
            if (*p == 0xFF) {
                *p = v;
                return;
            }
        for (size_t i = 0; i + 1 < slots.size(); i++) *slots[i] = *slots[i + 1];
        *slots.back() = v;
    };
    if (home == human_team_) put(away, 0, home_goals, away_goals);
    if (away == human_team_) put(home, 1, away_goals, home_goals);
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
    auto record = [&](int team, int rec, int a, int b, bool less, int wa, int wb, int opp) {
        if (team < 0 || team >= kSeriesTeams) return;
        uint8_t* v = save_.state.data() + kRecords + size_t(team) * 16 + size_t(rec) * 2;
        const int hi = v[1], lo = v[0];
        const uint16_t now = uint16_t(a * wa - b * wb), old = uint16_t(hi * wa - lo * wb);
        if (less ? !(now < old) : !(now > old)) return;
        if (wb < 0) wb = 1;
        const int x = (a * wa) << 8 | (b * wb);
        v[0] = uint8_t(x);
        v[1] = uint8_t(x >> 8);
        save_.state[kRecordOpponents + size_t(team) * 8 + size_t(rec)] = uint8_t(opp);
    };
    const int hg = std::min(home_goals, 255), ag = std::min(away_goals, 255);
    if (hg > ag) {
        record(home, 0, hg, ag, false, 1, 1, away);
        record(away, 5, hg, ag, false, 1, 1, home);
    } else if (hg < ag) {
        record(home, 1, hg, ag, true, 1, 1, away);
        record(away, 4, hg, ag, true, 1, 1, home);
    }
    record(home, 2, hg, ag, false, 1, 0, away);
    record(away, 7, hg, ag, false, 1, 0, home);
    record(home, 3, hg, ag, false, 0, -1, away);
    record(away, 6, hg, ag, false, 0, -1, home);
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
    }
    if (a < kHokLeagueTeams && b < kHokLeagueTeams) {  // the eternal table and the titles travel with the club
        uint8_t* s = save_.state.data();
        std::swap_ranges(s + kEternal + size_t(a) * 0x50, s + kEternal + size_t(a) * 0x50 + 4, s + kEternal + size_t(b) * 0x50);
        std::swap_ranges(s + kTitles + size_t(a) * 0x50, s + kTitles + size_t(a) * 0x50 + 3, s + kTitles + size_t(b) * 0x50);
    }
    for (int* t : {&human_team_, &champion_, &euro_champion_, &cup_winner_}) {  // titles stay with the club
        if (*t == a)
            *t = b;
        else if (*t == b)
            *t = a;
    }
}

void Game::new_season() {
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
            log_.push_back("promoted from the regional league: " + save_.teams[size_t(up)].name +
                           ", relegated: " + save_.teams[size_t(down)].name);
            swap_clubs(down, up);
        }
    }
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
            if (a != b) swap_teams(a, b);
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
    if (euro_champion_ >= 0) st[kStateEuroWinner] = uint8_t(euro_champion_);
    std::fill(st + kStateEntrants, st + kStateEntrants + 10, uint8_t(0));
    std::fill(st + kStateEntrantSlots, st + kStateEntrantSlots + 18, uint8_t(0x80));
    for (size_t i = 0; i < german.size(); i++) st[kStateEntrants + i] = st[kStateEntrantSlots + i] = uint8_t(german[i]);
    save_.euro_groups = draw_euro_groups(save_.teams, german, brng_);
    for (size_t g = 0; g < 2; g++) {
        euro_groups_[g] = GroupTable{save_.euro_groups[g]};
        for (int r = 0; r < 6; r++) {
            const std::vector<Pairing> pairs = round_robin_pairings(schedule_.teams4, r);
            for (size_t p = 0; p < 2 && p < pairs.size(); p++)
                save_.euro_rounds[g][size_t(r)][p] = {save_.euro_groups[g][size_t(pairs[p].home)],
                                                      save_.euro_groups[g][size_t(pairs[p].away)]};
        }
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
    const long prize = season_prizes(champion_ == human_team_, league, cup_winner_ == human_team_,
                                     euro_champion_ == human_team_, false);
    if (prize) {
        budget_.balance += prize;
        log_.push_back("prize money " + std::to_string(prize / 3) + " DM");
    }
    // players: one year older, contracts one year shorter, statistics cleared; computer club
    // players above rand(37, 42) retire and are replaced by a newcomer (the name is kept here)
    std::vector<bool> in_squad(save_.players.size(), false);
    for (const HokSquadPlayer& sp : save_.squad) in_squad[size_t(sp.player)] = true;
    int retired = 0;
    for (size_t p = 0; p < save_.players.size(); p++) {
        HokPlayer& pl = save_.players[p];
        pl.age++;
        // all players: both season start skills move by rand(-3, 3), kept in 30..99 (seen from
        // SAISEND to NEUSAIS for 199 of 199 players, also for the human squad; the code is not
        // found yet, so the random generator and the order are assumptions)
        pl.skill_a = std::clamp(pl.skill_a + brng_.range(-3, 3), 30, 99);
        pl.skill_b = std::clamp(pl.skill_b + brng_.range(-3, 3), 30, 99);
        pl.league_goals = pl.league_games = 0;
        {  // +0x1F u16 assists (goalkeepers: minutes) start again too
            uint8_t* r = save_.state.data() + (pl.raw - save_.state.data());
            r[0x1F] = r[0x20] = 0;
        }
        if (!in_squad[p] && pl.team >= 0 && player_retires(pl.age, brng_)) {
            const NewPlayer n = new_player(brng_);
            pl.age = n.age;
            pl.skill_a = n.skill1;
            pl.skill_b = n.skill2;
            retired++;
        }
    }
    for (size_t i = 0; i < save_.squad.size(); i++) {
        HokSquadPlayer& sp = save_.squad[i];
        uint8_t* r = save_.state.data() + kSquad + i * kSquadRecord;
        r[0x09] &= 0x3F;  // offer flags
        if (sp.contract_years > 0) sp.contract_years--;
        r[0x0B] = uint8_t(sp.contract_years);
        sp.league_games = sp.cup_games = sp.league_goals = sp.cup_goals = 0;
        sp.goals_against = sp.league_goals_byte = 0;
    }
    log_.push_back(std::to_string(retired) + " players retired");
    manager_offers_.clear();  // the offer flags are cleared above (ovl02_0000)
    // power play counters (manager block +0x177) start from 0 again (NEUSAIS); the series records stay
    std::fill(save_.state.begin() + long(kSpecialTeams), save_.state.begin() + long(kSpecialTeams + 12), uint8_t(0));
    expire_contracts();
    redistribute_players(0);
    redistribute_players(1);
    // a fresh season: tables, schedule position, cup draw, competitions
    for (int l = 0; l < 3; l++) {
        League& lg = leagues_[size_t(l)];
        lg.first_team = l * kHokLeagueSize;
        for (int s = 0; s < kHokLeagueSize; s++) {
            lg.rows[size_t(s)] = HokTableRow{};
            lg.rows[size_t(s)].form = "--------";
            lg.rows[size_t(s)].position = s;
        }
        matchday_[size_t(l)] = 1;
    }
    std::vector<int> cup_teams(3 * kHokLeagueSize);
    for (size_t t = 0; t < cup_teams.size(); t++) cup_teams[t] = int(t);
    cup_draw_ = draw_cup_round(cup_teams, brng_);  // 18 pairs; the original lets 4 teams skip round 1
    cup_draw_.resize(16);
    cup_day_ = 0;
    cup_winner_ = -1;
    euro_round_ = 0;
    euro_champion_ = -1;
    playoff_day_ = 0;
    champion_ = -1;
    reset_wm();
    series_.clear();
    budget_.league = team_class(finance_, human_team_);
    budget_.team = human_team_;
    save_.year++;
    save_.season_year = save_.year;  // DS:03BB follows at the season change (NEUSAIS)
    seasons_++;
    day_ = 0;
    log_.push_back("new season " + std::to_string(save_.year) + "/" + std::to_string(save_.year + 1));
}

Bytes Game::save_bytes() {
    HokFile& h = save_;
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
            h.table_order[size_t(l)][size_t(row.position)] = lg.first_team + s;
        }
        const int md = matchday_[size_t(l)];
        if (md >= 1 && md <= 4 * (kHokLeagueSize - 1)) {
            const auto pairs = round_robin_pairings(schedule_.teams12, md - 1);
            for (size_t i = 0; i < pairs.size() && i < h.pairings[size_t(l)].size(); i++)
                h.pairings[size_t(l)][i] = {lg.first_team + pairs[i].home, lg.first_team + pairs[i].away};
        }
    }
    h.cup_draw = cup_draw_;
    h.cup_round = cup_day_ / 3 + 1;
    store_hok(h);
    // manager block of manager 0 (raw money)
    Bytes& s = h.state;
    put32(s, kManagerBlock + 0x18F, budget_.balance);
    put32(s, kManagerBlock + 0x197, monthly_income(budget_, true));
    put32(s, kManagerBlock + 0x19B + 4 * 4, budget_.bank);          // manager -> bank
    put32(s, kManagerBlock + 4 * 0x1AF + 0x19B, -budget_.bank);     // the bank's mirror entry
    put32(s, kManagerBlock + 0x183, budget_.attendance_total);
    put32(s, kManagerBlock + 0x187, budget_.attendance_record);
    put32(s, kManagerBlock + 0x18B, budget_.attendance_min);
    s[kManagerBlock + 0x0ED] = uint8_t(budget_.home_games);
    if (budget_.record_guest >= 0) s[kManagerBlock + 0x114] = uint8_t(budget_.record_guest);
    if (budget_.min_guest >= 0) s[kManagerBlock + 0x115] = uint8_t(budget_.min_guest);
    s[kManagerBlock + 0x014] = uint8_t(human_team_);
    s[kManagerBlock + 0x0EB] = uint8_t(budget_.league);
    s[kManagerBlock + 0x173] = uint8_t(image_);
    s[kManagerBlock + 0x174] = uint8_t(image_ >> 8);
    // plain header: calendar year of the next day (MONAT4: 1994), next matchday of the manager's league
    h.header[0x0A] = uint8_t(next.year);
    h.header[0x0B] = uint8_t(next.year >> 8);
    h.header[0x0F] = uint8_t(matchday_[size_t(std::clamp(budget_.league, 0, 2))]);
    return encode_hok(h);
}

void Game::advance_day() {
    if (day_ + 1 >= int(calendar_.events.size())) new_season();
    refresh_human_team();  // 162c:146c, daily
    if (brng_.range(0, kManagers + 2) == 0) refresh_transfer_list();  // ovl14_2e0f, DS:0386 managers
    day_++;
    const GameDate d = date();
    const uint8_t event = calendar_.event(day_);
    human_played_ = false;
    if (calendar_.league_day(day_)) {
        play_league_day();
        human_played_ = human_team_ >= 0 && human_team_ < 3 * kHokLeagueSize;
    }
    if (event == kEventCup) play_cup_day();
    if (event == kEventEuroCup) play_euro_day();
    if (event == kEventPlayoff) play_playoff_day();
    if (event == kEventWorldChampionship) play_wm_day();
    if (human_played_) match_injuries();
    const int month_days = kMonthDays[size_t(d.month - 1)];
    const long before = budget_.balance;
    finance_day(finance_, budget_, d.day, month_days);
    if (d.day == month_days) {
        char line[128];
        std::snprintf(line, sizeof line, "month end %02d.%02d.: balance %ld DM (%+ld DM)", d.day, d.month,
                      budget_.balance / 3, (budget_.balance - before) / 3);
        log_.push_back(line);
    }
    if (d.weekday == kMonday || d.weekday == kFriday) {
        squad_injuries(d.weekday == kMonday, event == 0);
        make_offers();
    }
    if (d.weekday == kMonday) train_squad();
    if (d.day == 15 || d.day == month_days) update_sponsors();
}

}  // namespace em
