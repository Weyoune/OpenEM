// SPDX-License-Identifier: GPL-3.0-only
// The standalone world championship ("Nur WM"), see wm_mode.hpp and docs/logic/WM.md "WM-Modus".
#include "game/wm_mode.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "game/game.hpp"
#include "game/manager_layout.hpp"
#include "game/national_team.hpp"
#include "game/result_books.hpp"
#include "game/scorers.hpp"

namespace em {
namespace {
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;  // see schedule.cpp
constexpr size_t kMenuTable = 0x5159;             // DS:5159, ovr034:2166 / 2585
constexpr size_t kFileNumber = 0x06AE;            // DS:0256
constexpr size_t kNation = 0x068C, kFriendlyCount = 0x068D, kFriendlyHome = 0x068E, kFriendlyAway = 0x0696,
                 kFriendlyHomeGoals = 0x069E, kFriendlyAwayGoals = 0x06A6, kFriendlyPrice = 0x0658;
constexpr size_t kAppointment = 0x0688;  // DS:D0BA, one byte per manager
constexpr size_t kNationalSquad = squad_block(5), kSquadRecord = 0x34;
constexpr size_t kNationalManager = manager_block(5);
constexpr int kFirstNation = 52;  // DS:06AD

long s32(const Bytes& b, size_t o) { return long(int32_t(le32(b, o))); }
void put32(Bytes& b, size_t o, long v) {
    for (int k = 0; k < 4; k++) b.at(o + size_t(k)) = uint8_t(uint32_t(v) >> (8 * k));
}
// ovl12_0d27(5, 0): the number of called-up players.
int nominees(const HokFile& h) {
    int n = 0;
    for (int slot = 0; slot < 0x1D; slot++)
        if (h.state.at(kNationalSquad + size_t(slot) * kSquadRecord + 0x0F) != 0) n++;
    return n;
}
}  // namespace

uint8_t native_game_mode(int bits) {
    if (bits == kStartModeBoth) return kModeLeagueAndWm;
    return uint8_t((bits == kStartModeWm ? 1 : 0) + (bits == kStartModeLeague ? 2 : 0));
}

bool wm_only_mode(const HokFile& h) { return h.state.at(kStateGameMode) == kModeWmOnly; }

bool start_menu_highscore(int bits, const std::array<uint8_t, 4>& o) {
    // ovr027:096D: var_57 == 2, or the league bit with level > 2 and (strength 1, or duration 0 with start league 0)
    if (bits == kStartModeWm) return true;
    return (bits & 1) && o[0] > 2 && (o[3] == 1 || (o[1] == 0 && o[2] == 0));
}

void apply_start_mode(HokFile& h, int bits) {
    h.state.at(kStateGameMode) = native_game_mode(bits);
    if (bits == kStartModeWm) {  // ovr027:0A0A
        h.state.at(kFileNumber) = uint8_t(kWmOnlyHighscoreFile);
        h.state.at(kFileNumber + 1) = uint8_t(kWmOnlyHighscoreFile >> 8);
    }
}

HokFile start_wm_only_game(const HokFile& base, const Database& db, const NewGameSetup& setup) {
    HokFile h = start_new_game(base, db, setup);
    apply_start_mode(h, kStartModeWm);
    return decode_hok(encode_hok(h));
}

MenuAvailability load_menu_availability(const Bytes& exe) {
    MenuAvailability m;
    for (size_t t = 0; t < 2; t++)
        for (size_t i = 0; i < 42; i++) m.table[t][i] = exe.at(kDgroupFile + kMenuTable + 0x2A * t + i);
    return m;
}

bool menu_item_disabled(const MenuAvailability& m, const HokFile& h, bool national, int managers, int menu, int item,
                        bool wm_started) {
    if (menu < 0 || menu > 6 || item < 0 || item > 5) return true;
    const uint8_t mode = h.state.at(kStateGameMode);
    const bool list = (h.state.at(kFileNumber) | h.state.at(kFileNumber + 1)) != 0;
    const size_t t = national ? 1 : 0;
    // ovr034:1E7E..1F48 rewrite single entries before the menu is drawn
    if (menu == 0 && item == 5) return !list || (national && mode == kModeLeagueAndWm);  // Highscore
    if (!national) {
        if (mode == kModeLeagueOnly && ((item == 5 && menu >= 2 && menu <= 4) || (menu == 5 && item == 3)))
            return true;                                                         // the WM items (and byte_2A14A)
        if (menu == 0 && item == 4) return managers == 4;                        // Neuer
        if (menu == 0 && item == 1) return le16(h.state, 0xAEB5) == 0x7CA;      // Werbung in a one-year game
    }
    if (national && wm_started && menu == 1 && (item == 3 || item == 4)) return true;  // NATLIVQ: Transfer, Training
    return m.table[t][size_t(menu * 6 + item)] != 0;
}

long friendly_price(const HokFile& h, int nation, bool home) {
    if (nation < kFirstNation || nation >= kFirstNation + 12) return 0;
    const long base = s32(h.state, kFriendlyPrice + 4 * size_t(nation - kFirstNation));
    return home ? base * 2 : base;  // ovr031:24A8: LXLSH by (Heim ? 1 : 0)
}

std::vector<FriendlyRecord> friendly_records(const HokFile& h) {
    std::vector<FriendlyRecord> out;
    const int n = std::min<int>(h.state.at(kFriendlyCount), kMaxFriendlies);
    for (size_t i = 0; i < size_t(n); i++)
        out.push_back({h.state.at(kFriendlyHome + i), h.state.at(kFriendlyAway + i), h.state.at(kFriendlyHomeGoals + i),
                       h.state.at(kFriendlyAwayGoals + i)});
    return out;
}

int friendly_bonus(int a, int q, bool away, BorlandRandom& rng) {
    if (away && q != 0) a += rng.range(1, q);
    else a--;
    if (q < 2) a -= 2 - q - (away ? 1 : 0);
    if (q > 2) a += q;
    a = int(int8_t(uint8_t(a)));  // byte arithmetic of var_A
    const int low = q > 2 ? 1 : 0;
    if (a < low) a = low;
    const int high = 2 - (q < 2 ? 1 : 0);
    if (a > high) a = high;
    if (q > 3) a++;
    return a;
}

void apply_friendly_bonus(HokFile& h, int bonus, int q) {
    if (bonus <= 0) return;
    const int count = nominees(h);
    for (int slot = 0; slot < count; slot++)
        for (int field = 0; field < 3; field++) {
            // q > 2: the first skill gets one less; otherwise the second skill
            const int add = bonus - ((q > 2 && field == 0) || (q < 3 && field == 1) ? 1 : 0);
            uint8_t& v = h.state.at(kNationalSquad + size_t(slot) * kSquadRecord + 0x10 + size_t(field));
            v = uint8_t(v + add);
            if (v > 99) v = 99;
        }
}

// ---- Game: the WM-only flow (members declared in game.hpp)

bool Game::wm_only() const { return wm_only_mode(save_); }

int Game::friendlies_played() const { return std::min<int>(save_.state.at(kFriendlyCount), kMaxFriendlies); }

// ovr038:2E5E..2EA8 and the appointment ovr038:35AF..362A for manager 0. OpenEM runs it when a WM-only game is opened
// without a national coach (new game, loaded preparation save) and after the season change; the original repeats the
// appointment with every entry of the day loop, i.e. also after loading a save that already has one (the "WM cheat",
// docs/DEVIATIONS.md).
void Game::wm_only_prepare() {
    if (!wm_only() || national_coach_ >= 0 || wm_round_ > 0) return;
    const int first = calendar_.nth_day(kEventWorldChampionship, 1);
    if (first < 1) return;
    if (day_ < first - 1) {
        // REAL-CODE WM-only new game (11.11.2026): the energies keep the values of the new world on the way to the WM
        if (day_ == 0 && std::getenv("EM_OLD_WMENERGY"))
            for (HokTeam& team : save_.teams)
                for (auto& kind : team.energy)
                    for (auto& e : kind) e = 90;
        day_ = first - 1;  // the counter and cursor only run on: no matches, finances or training on the way
        // the month routines on the way leave the month counter DS:A8A0 at the WM month (0-based; REAL-CODE WM-only game: 3)
        if (!std::getenv("EM_OLD_WMMONTH")) save_.state.at(0xB965) = uint8_t((date_of_season_day(day_, save_.year).month + 11) % 12);
        message_step_pending_ = true;
    }
    const int nation = save_.state.at(kNation);
    if (nation < kFirstNation || nation >= kFirstNation + 12) return;
    national_coach_ = 0;
    std::copy_n(save_.state.begin() + long(manager_block(0)), 20, save_.state.begin() + long(kNationalManager));
    save_.state.at(kNationalManager + 0x14) = uint8_t(nation);
    save_.state.at(kNationalManager + 0xE4) = 0;  // byte_2EF7A: the human manager
    save_.state.at(kNationalManager + 0xEB) = 0;  // byte_2EF81
    put32(save_.state, kNationalManager + 0x18F, s32(save_.state, kNationalManager + 0x18F) + national_team_grant(save_));
    const size_t team = size_t(save_.teams[size_t(nation)].raw - save_.state.data());
    save_.state.at(team + 0x16) = 6;
    save_.state.at(team + 0x34) |= 0x80;  // sub_1902B(nation, 1, 2)
    save_.state.at(kAppointment) = 0;
    save_.state.at(kFriendlyCount) = 0;  // ovl05_194b(0) of the new season (ovr029:198E)
    save_.state.at(0x0788) = save_.state.at(0x0789) = 5;
    // the menu before the WM day restores the coach's line-up mode (DS:037F <- DS:0380; REAL-CODE WM-only game, 1995)
    if (!std::getenv("EM_OLD_WMMODE")) save_.state.at(0x078A + 10) = save_.state.at(0x078B + 10);
    save_.state.at(0xA528) = 1;  // DS:0229: native WM match display
    refresh_national_team();
    log_.push_back("WM-only mode: national team manager of " + save_.teams[size_t(nation)].name);
}

bool Game::enter_friendlies() {
    if (national_coach_ < 0 || national_coach_ != active_manager_ || wm_round_ != 0) return false;
    // ovr031:2150..218A: every called-up player gets exhaustion 70
    for (int slot = 0; slot < 0x1D; slot++) {
        const size_t at = kNationalSquad + size_t(slot) * kSquadRecord;
        if (save_.state.at(at + 0x0F) != 0) save_.state.at(at + 0x13) = 0x46;
    }
    return true;
}

// ovl07_1af6, the line overview of a selected opponent (first click on a flag): for the own nation it runs the automatic
// line-up ovl09_26dc of the national squad first (REAL-CODE WM-only game, friendly page on 13.04.1994)
bool Game::preview_friendly() {
    if (national_coach_ < 0 || national_coach_ != active_manager_ || wm_round_ != 0) return false;
    if (std::getenv("EM_OLD_FRIENDLYLINEUP")) return false;
    auto_lineup(save_, 5);
    if (std::getenv("EM_OLD_FRIENDLYREFRESH")) refresh_national_team();
    return true;
}

Game::FriendlyOutcome Game::play_friendly(int opponent, bool home) {
    FriendlyOutcome out;
    const int nation = save_.state.at(kNation);
    if (national_coach_ < 0 || national_coach_ != active_manager_ || wm_round_ != 0 || opponent == nation ||
        opponent < kFirstNation || opponent >= kFirstNation + 12)
        return out;
    if (save_.state.at(kFriendlyCount) >= kMaxFriendlies) {  // ovr031:25CB
        out.refusal = "Nicht mehr als\n8 Freundschaftsspiele";
        return out;
    }
    const long price = friendly_price(save_, opponent, home);
    const long account = s32(save_.state, kNationalManager + 0x18F);
    if (account < price) {  // ovr031:25D5
        out.refusal = "Sie haben nicht\ngenug Geld !";
        return out;
    }
    put32(save_.state, kNationalManager + 0x18F, account - price);
    const int q = int((price / 2000 + brng_.range(0, 50)) / 50);
    // ovl05_0e35: the pair list DS:034B holds this one game, competition DS:76B2 = 0x0D, and ovl06_161d(0, 8, 0, 0, 3)
    // plays it like a WM group round: gate of the nation's home game, manager pass of manager 5, the minute loop with
    // the coach's checks (REAL-CODE WM-only game, friendly against Russia on 13.04.1994)
    if (std::getenv("EM_OLD_FRIENDLYREFRESH")) refresh_national_team();  // the record keeps its values (REAL-CODE)
    auto national = national_squad();
    HumanSquad squad{&national, &save_.players};
    squad.national = !std::getenv("EM_OLD_FRIENDLY");
    for (int kind = 0; kind < 2; kind++) squad.per_line[kind] = line_counts_[5][size_t(kind)];
    squad.minimum_players = 9;
    squad.level = finance_.level;
    squad.selection_rng = &brng_;
    squad.rotation = &national_rotation_;
    squad.timers = &national_timers_;
    configure_human_tactics(squad, save_, 5);
    squad.penalty_effort_adjustment = penalty_effort_adjustment(save_, nation);
    const int h = home ? nation : opponent, a = home ? opponent : nation;
    MatchResult r;
    if (std::getenv("EM_OLD_FRIENDLY")) {
        MatchStats stats;
        r = league_match(save_.teams[size_t(h)], save_.teams[size_t(a)], rng_, 0, h == nation ? &squad : nullptr,
                         a == nation ? &squad : nullptr, &stats);
    } else {
        // the match day of ovl06_161d books like a WM day: rest of the club squads, caps of all nominees, and after the game
        // the coach's match bookings (appearances, special teams); no table, series or head-to-head booking
        const bool book = !std::getenv("EM_OLD_FRIENDLYBOOK");
        if (book) {
            rest_for_national_match();
            count_national_caps(national);
        }
        MatchStats live_stats;
        HokPairing pair;
        pair.home = h;
        pair.away = a;
        const auto ties = national_live_round({pair}, false, squad, national, live_stats, std::getenv("EM_OLD_FRIENDLYLEVEL") ? 1 : 0);
        if (!ties.empty()) r = ties.front().result;
        if (book) {
            book_national_match(home ? r.home : r.away, live_stats, home ? 0 : 1, 0, false);
            // the result bytes DS:029B of the pair list land in the result slot 0 of the WM block (DS:5CAB), and ovl13_2f93
            // books the game into the head-to-head table from the nation's view
            save_.state.at(0x8F60) = uint8_t(r.home);
            save_.state.at(0x8F61) = uint8_t(r.away);
            book_head_to_head(save_.state, h, a, r.home, r.away, nation);
            // back on the page, ovr031:2150 sets the exhaustion of all nominees to 70 again
            enter_friendlies();
        }
    }
    const int own = home ? r.home : r.away, other = home ? r.away : r.home;
    out.bonus = friendly_bonus(int(int8_t(uint8_t(own))) - int(int8_t(uint8_t(other))), q, !home, brng_);
    apply_friendly_bonus(save_, out.bonus, q);
    const size_t n = save_.state.at(kFriendlyCount);
    save_.state.at(kFriendlyHome + n) = uint8_t(h);
    save_.state.at(kFriendlyAway + n) = uint8_t(a);
    save_.state.at(kFriendlyHomeGoals + n) = uint8_t(r.home);
    save_.state.at(kFriendlyAwayGoals + n) = uint8_t(r.away);
    save_.state.at(kFriendlyCount)++;
    if (std::getenv("EM_OLD_FRIENDLYREFRESH")) refresh_national_team();
    out.played = true;
    out.home = h;
    out.away = a;
    out.home_goals = r.home;
    out.away_goals = r.away;
    out.price = price;
    return out;
}

std::array<std::array<int, 3>, 2> Game::line_overview(int team) const {
    // ovl07_1af6: sub_1797F(team, line, kind, 0, 0) for the three lines of defence and attack, shown as the mean of two
    // of its bytes. The own nation is valued with its squad (sub_24BBD refreshes it first).
    std::array<std::array<int, 3>, 2> out{};
    if (team < 0 || size_t(team) >= save_.teams.size()) return out;
    const HokTeam& t = save_.teams[size_t(team)];
    const bool own = national_coach_ >= 0 && team == save_.state.at(kNation);
    const auto national = national_squad();
    HumanSquad squad{&national, &save_.players};
    for (int kind = 0; kind < 2; kind++) squad.per_line[kind] = line_counts_[5][size_t(kind)];
    squad.level = finance_.level;
    for (int kind = 0; kind < 2; kind++)
        for (int line = 0; line < 3; line++) {
            const LineValues v = own ? line_strength(t, squad, line, kind, false, 0) : line_strength(t, line, kind, false, 0);
            out[size_t(kind)][size_t(line)] = (v.skill1 + v.skill2) / 2;
            if (std::getenv("EM_DEBUG_OVERVIEW")) std::fprintf(stderr, "overview %d kind %d line %d: %d %d %d\n", team, kind, line, v.skill1, v.skill2, v.form);
        }
    return out;
}

}  // namespace em
