// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// End of a season (day loop ovl14_2e0f once the year rolls over, then ovl16_30ed, ovl14_00ad,
// ovl00_058a, 162c:1c5b, ovl06_099d, ovl05_51a5, ovl02_0000 and ovl16_0480 twice; afterwards
// the year DS:03BD is incremented and the tables are cleared). This file covers ovl02_0000, the
// season review of each human manager and the player turnover. Amounts are raw (DM * 3).
//
// Prizes (ovl02_0000, added to the balance and to DS:E289[manager]):
//   promotion (achievement bit 0 set and bit 1 clear): 1,500,000 raw (500,000 DM) in the
//   first division, 900,000 raw (300,000 DM) below; every advertising contract then gets a
//   renewal offer (+6 bit 7)
//   league's leading scorer, best goalkeeper and fair-play winner: 300,000 raw each.
// The cup winner receives 1,500,000 raw earlier, before the license check (ovl14:3e43).
// Players (all 199):
//   every player gets one year older (+0x14)
//   squad records: offer flags (+0x09 bits 6-7) cleared, contract years (+0x0B) minus one
//   (not in the first season set-up, DS:01DB, and not with DS:D0B9 = 1); a player whose owner
//   byte (+0x1A) names another manager moves to that manager's squad now (transfers between
//   managers take effect at the season end), otherwise he becomes a free player (+0x1A = 6)
//   the statistics are reset: league goals, games and assists of the player record
//   (+0x1B, +0x1C, +0x1F) and the squad bytes +0x00/+0x01/+0x03..+0x08
// Retirement: a player retires if his age is above rand(37, 42) - squad players only when
//   their contract has run out (+0x0B = 0) and squad flag +0x18 bit 7 is set. A new player takes
//   his place (new name from 1000:1239): age rand(18, 25), base = rand(40, 92),
//   skills rand(base - 5, base + 5) each, +0x19 = 0, free (+0x1A = 6).
#include <array>
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/match.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr long kPromotionPrize = 1500000, kPromotionPrizeLower = 900000,
               kTitlePrize = 300000, kCupPrize = 1500000;

// Prize money of the season review (raw).
long season_prizes(bool promoted, int league, bool scorer, bool goalkeeper, bool fair_play);

struct SeasonScorer {
    int team = -1, position = 0, goals = 0, assists_or_minutes = 0, games = 0;
};
// ovl01_0000(1): native exchange sort of all player records, followed by the
// manager's league filter. Returns the clubs of the best skater and goalkeeper.
std::array<int, 2> season_scoring_winners(const std::vector<SeasonScorer>& players,
                                          int first_team, int last_team);

struct CareerSeason {
    int place = 1;  // 1..12, or first-division play-off code 51/52/53
    int league = 0;
    bool champion = false;
    int cup_round = 0;
    bool cup_winner = false;
    int euro_phase = 0;
    bool euro_winner = false;
};
// ovl14: annual four-byte manager history, capped at fifty visible seasons.
// A nonparticipant's EuroCup byte is left untouched, including after a shift.
void append_career_season(Bytes& state, int manager, int year, const CareerSeason& season);

// ovl14_2e0f, 4087..4258: a season-end financial license withdrawal.
// The two title flags protect the club; at most two cases per league are handled.
bool financial_license_due(long balance, int league, uint8_t season_flags, int withdrawals);

// Modify the first four bytes of the relegation working schedule (DS:CF20),
// indexed by the saved table permutation. Original participant IDs stay intact.
// withdrawals is the number of earlier license cases in this division (0 or 1).
void insert_financial_relegation(std::array<int, 4>& working_teams,
                                const std::array<int, 4>& ranking,
                                int league, int team, int withdrawals);

// Age above rand(37, 42)?
bool player_retires(int age, BorlandRandom& rng);

struct NewPlayer {
    int age = 0, skill1 = 0, skill2 = 0;
};
// The replacement for a retired player.
NewPlayer new_player(BorlandRandom& rng);

// New season values of a club, 162c:1c5b(mode != 1 -> 3), run after promotion and relegation for
// the 52 German teams with the game random generator (199f:07e5):
//   r = rand(0, 6); trend = rand(0, 2r + 6) - 6
//   skill 1 of both kinds += rand(trend - 1, trend + 1); skill 2 likewise with chance 1/3 (when
//   rand(0, 2) = 0); both kept in the bounds of the club's class (DS:0125: first division 69..94,
//   second 39..73, Oberliga 15..43)
//   form of both kinds = rand(45, 55); the loop runs over three bytes, so the first energy byte
//   (+0x1E) is overwritten with rand(45, 55) as well (original quirk)
// Checked: SAISEND -> NEUSAIS, all 52 clubs within these rules.
struct RatingBounds {
    std::array<std::array<int, 2>, 3> bounds{};  // [class] = {low, high}
};
RatingBounds load_rating_bounds(const Bytes& exe);
void rerate_team(HokTeam& team, int team_class, const RatingBounds& b, GameRandom& rng);

}  // namespace em
