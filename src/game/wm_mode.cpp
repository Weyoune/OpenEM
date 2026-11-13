// SPDX-License-Identifier: GPL-3.0-only
// The standalone world championship ("Nur WM") and the national coach's menu and friendlies
// (rules in wm_mode.hpp), plus the Game members of the WM-only flow declared in game.hpp.
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
// EM.EXE file offset of the program's data segment (header 0x1E00 + segment start 0x14FD0, as
// in schedule.cpp); the menu tables lie at kMenuTable within it.
constexpr size_t kDgroupFile = 0x1E00 + 0x14FD0;
constexpr size_t kMenuTable = 0x5159;             // offset of the two 42-byte menu availability tables
// Save state offsets:
constexpr size_t kFileNumber = 0x06AE;            // u16 highscore file number (0 = no list)
constexpr size_t kNation = 0x068C,                // u8 team index of the coached nation (52..63)
                 kFriendlyCount = 0x068D,         // u8 friendlies played this season
                 kFriendlyHome = 0x068E,          // u8[8] home nation per friendly
                 kFriendlyAway = 0x0696,          // u8[8] away nation
                 kFriendlyHomeGoals = 0x069E,     // u8[8] home goals
                 kFriendlyAwayGoals = 0x06A6,     // u8[8] away goals
                 kFriendlyPrice = 0x0658;         // i32[12] friendly price per nation, raw
constexpr size_t kAppointment = 0x0688;  // appointment flag, one byte per manager
constexpr size_t kNationalSquad = squad_block(5), kSquadRecord = 0x34;  // national squad rows, 0x34 bytes each
constexpr size_t kNationalManager = manager_block(5);                   // the national coach's manager block
constexpr int kFirstNation = 52;  // team index of the first of the 12 nations

// Signed i32 from the state.
long s32(const Bytes& b, size_t o) { return long(int32_t(le32(b, o))); }
// Stores a little-endian i32.
void put32(Bytes& b, size_t o, long v) {
    for (int k = 0; k < 4; k++) b.at(o + size_t(k)) = uint8_t(uint32_t(v) >> (8 * k));
}
// The number of called-up players: occupied rows (player ID at +0x0F) among the first 29 rows
// of the national squad.
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
    // "Nur WM", or the league bit with level > 2 and (strength 1, or duration 0 with start league 0)
    if (bits == kStartModeWm) return true;
    return (bits & 1) && o[0] > 2 && (o[3] == 1 || (o[1] == 0 && o[2] == 0));
}

void apply_start_mode(HokFile& h, int bits) {
    h.state.at(kStateGameMode) = native_game_mode(bits);
    if (bits == kStartModeWm) {  // "Nur WM": highscore list H.30000
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
    // the game overrides single entries before the menu is drawn
    if (menu == 0 && item == 5) return !list || (national && mode == kModeLeagueAndWm);  // Highscore
    if (!national) {
        if (mode == kModeLeagueOnly && ((item == 5 && menu >= 2 && menu <= 4) || (menu == 5 && item == 3)))
            return true;                                                         // the WM items
        if (menu == 0 && item == 4) return managers == 4;                        // Neuer
        if (menu == 0 && item == 1) return le16(h.state, 0xAEB5) == 0x7CA;      // Werbung in a one-year game
    }
    if (national && wm_started && menu == 1 && (item == 3 || item == 4)) return true;  // during the WM: Transfer, Training
    return m.table[t][size_t(menu * 6 + item)] != 0;
}

long friendly_price(const HokFile& h, int nation, bool home) {
    if (nation < kFirstNation || nation >= kFirstNation + 12) return 0;
    const long base = s32(h.state, kFriendlyPrice + 4 * size_t(nation - kFirstNation));
    return home ? base * 2 : base;  // the game shifts the price left by one for a home game
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
    a = int(int8_t(uint8_t(a)));  // the original keeps the bonus in a signed byte
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

// Whether this is a "Nur WM" game.
bool Game::wm_only() const { return wm_only_mode(save_); }

// Friendlies played this season (0..8).
int Game::friendlies_played() const { return std::min<int>(save_.state.at(kFriendlyCount), kMaxFriendlies); }

// The WM-only start of a season: skips the days up to the first WM day and appoints manager 0
// as national coach. OpenEM runs it when a WM-only game is opened without a national coach (new
// game, loaded preparation save) and after the season change; the original repeats the
// appointment with every entry of the day loop, i.e. also after loading a save that already has
// one (the "WM cheat"), which is a documented deviation.
// Effects on the state: the day counter jumps to the day before the first WM day, manager block
// 5 takes manager 0's name, the nation and the appointment grant, the nation's team record is
// marked as coached, the appointment flag and friendly count are reset and the national team is
// refreshed. Does nothing outside WM-only games, with a coach already appointed, once the WM has
// started, or with an invalid nation.
void Game::wm_only_prepare() {
    if (!wm_only() || national_coach_ >= 0 || wm_round_ > 0) return;
    const int first = calendar_.nth_day(kEventWorldChampionship, 1);
    if (first < 1) return;
    if (day_ < first - 1) {
        // On the way to the WM the team energies keep the values of the new world
        // (EM_OLD_WMENERGY restores the old reset to 90).
        if (day_ == 0 && std::getenv("EM_OLD_WMENERGY"))
            for (HokTeam& team : save_.teams)
                for (auto& kind : team.energy)
                    for (auto& e : kind) e = 90;
        day_ = first - 1;  // the counter and cursor only run on: no matches, finances or training on the way
        // The month routines on the way leave the month counter (state 0xB965, 0-based) at the
        // month of the day reached, i.e. the WM month.
        if (!std::getenv("EM_OLD_WMMONTH")) save_.state.at(0xB965) = uint8_t((date_of_season_day(day_, save_.year).month + 11) % 12);
        message_step_pending_ = true;
    }
    const int nation = save_.state.at(kNation);
    if (nation < kFirstNation || nation >= kFirstNation + 12) return;
    national_coach_ = 0;
    std::copy_n(save_.state.begin() + long(manager_block(0)), 20, save_.state.begin() + long(kNationalManager));
    save_.state.at(kNationalManager + 0x14) = uint8_t(nation);
    save_.state.at(kNationalManager + 0xE4) = 0;  // block 5 is run by a human manager
    save_.state.at(kNationalManager + 0xEB) = 0;  // cleared by the appointment as well
    put32(save_.state, kNationalManager + 0x18F, s32(save_.state, kNationalManager + 0x18F) + national_team_grant(save_));
    const size_t team = size_t(save_.teams[size_t(nation)].raw - save_.state.data());
    save_.state.at(team + 0x16) = 6;
    save_.state.at(team + 0x34) |= 0x80;  // bit 7 of the nation's +0x34 flags (bits 6..7: reporting mode)
    save_.state.at(kAppointment) = 0;
    save_.state.at(kFriendlyCount) = 0;  // the new season starts without friendlies
    save_.state.at(0x0788) = save_.state.at(0x0789) = 5;  // national team phase bytes
    // the menu before the WM day restores the coach's line-up mode from its saved copy
    if (!std::getenv("EM_OLD_WMMODE")) save_.state.at(0x078A + 10) = save_.state.at(0x078B + 10);
    save_.state.at(0xA528) = 1;  // the WM matches are shown with the match display
    refresh_national_team();
    log_.push_back("WM-only mode: national team manager of " + save_.teams[size_t(nation)].name);
}

// Opening the friendlies page (national coach at the menu, before the WM): every called-up
// player's exhaustion (+0x13) is set to 70. Returns false when the page is not available.
bool Game::enter_friendlies() {
    if (national_coach_ < 0 || national_coach_ != active_manager_ || wm_round_ != 0) return false;
    // every called-up player gets exhaustion 70
    for (int slot = 0; slot < 0x1D; slot++) {
        const size_t at = kNationalSquad + size_t(slot) * kSquadRecord;
        if (save_.state.at(at + 0x0F) != 0) save_.state.at(at + 0x13) = 0x46;
    }
    return true;
}

// The squad block the line-up screens work on: 5 for the national coach, else the active manager.
int Game::lineup_manager() const {
    return national_coach_ >= 0 && national_coach_ == active_manager_ ? 5 : active_manager_;
}

// The line overview of a selected opponent (first click on a flag on the friendlies page): the game first runs the
// automatic line-up of the national squad. Returns false when not available (or with EM_OLD_FRIENDLYLINEUP).
bool Game::preview_friendly() {
    if (national_coach_ < 0 || national_coach_ != active_manager_ || wm_round_ != 0) return false;
    if (std::getenv("EM_OLD_FRIENDLYLINEUP")) return false;
    auto_lineup(save_, 5);
    if (std::getenv("EM_OLD_FRIENDLYREFRESH")) refresh_national_team();
    return true;
}

// Plays a friendly against `opponent` (52..63, not the own nation), at home or away. Refused
// with a message after 8 friendlies or when the national account cannot pay the price. Pays the
// price, draws q, plays the match like a WM group game, books it, then applies the squad bonus
// and records the result. Returns the outcome (played = false on any refusal).
Game::FriendlyOutcome Game::play_friendly(int opponent, bool home) {
    FriendlyOutcome out;
    const int nation = save_.state.at(kNation);
    if (national_coach_ < 0 || national_coach_ != active_manager_ || wm_round_ != 0 || opponent == nation ||
        opponent < kFirstNation || opponent >= kFirstNation + 12)
        return out;
    if (save_.state.at(kFriendlyCount) >= kMaxFriendlies) {
        out.refusal = "Nicht mehr als\n8 Freundschaftsspiele";
        return out;
    }
    const long price = friendly_price(save_, opponent, home);
    const long account = s32(save_.state, kNationalManager + 0x18F);
    if (account < price) {
        out.refusal = "Sie haben nicht\ngenug Geld !";
        return out;
    }
    put32(save_.state, kNationalManager + 0x18F, account - price);
    const int q = int((price / 2000 + brng_.range(0, 50)) / 50);
    // The game plays the friendly as a match day with this single pairing, like a WM group round: gate of the
    // nation's home game, the manager pass of manager 5, and the minute loop with the coach's checks.
    if (std::getenv("EM_OLD_FRIENDLYREFRESH")) refresh_national_team();  // by default the team record keeps its values
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
        // the match day books like a WM day: rest of the club squads, caps of all nominees, and after the game
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
            // the result lands in result slot 0 of the WM block (state 0x8F60/0x8F61), and the game is booked into
            // the head-to-head table from the nation's view
            save_.state.at(0x8F60) = uint8_t(r.home);
            save_.state.at(0x8F61) = uint8_t(r.away);
            book_head_to_head(save_.state, h, a, r.home, r.away, nation);
            // back on the page, the exhaustion of all nominees is set to 70 again
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
    // The line strength of the three lines of defence and attack, shown as the mean of both skill values. The own
    // nation is valued with its squad. Returns zeros for an invalid team; EM_DEBUG_OVERVIEW prints the values.
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
