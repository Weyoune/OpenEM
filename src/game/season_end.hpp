// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// End of a season. Once the year rolls over, the original runs the season-end steps (finances,
// promotion and relegation, new team ratings, league shuffle, cup draw, the season review and the
// redistribution of free players twice); afterwards the season year is incremented and the
// tables are cleared. This file covers the season review rules of each human manager, the
// financial licence withdrawal, the manager's career history and the player turnover.
// Amounts are raw (DM * 3).
//
// Prizes (added to the balance and to the manager's season prize total):
//   promotion (achievement bit 0 set and bit 1 clear): 1,500,000 raw (500,000 DM) in the
//   first division, 900,000 raw (300,000 DM) below; every advertising contract then gets a
//   renewal offer (+6 bit 7)
//   league's leading scorer, best goalkeeper and fair-play winner: 300,000 raw each.
// The cup winner receives 1,500,000 raw earlier, before the licence check.
// Players (all 199):
//   every player gets one year older (+0x14)
//   squad records: offer flags (+0x09 bits 6-7) cleared, contract years (+0x0B) minus one
//   (not in the first season set-up and not in a WM-only game); a player whose owner
//   byte (+0x1A) names another manager moves to that manager's squad now (transfers between
//   managers take effect at the season end), otherwise he becomes a free player (+0x1A = 6)
//   the statistics are reset: league goals, games and assists of the player record
//   (+0x1B, +0x1C, +0x1F) and the squad bytes +0x00/+0x01/+0x03..+0x08
// Retirement: a player retires if his age is above rand(37, 42) - squad players only when
//   their contract has run out (+0x0B = 0) and squad flag +0x18 bit 7 is set. A new player takes
//   his place (with a newly generated name): age rand(18, 25), base = rand(40, 92),
//   skills rand(base - 5, base + 5) each, +0x19 = 0, free (+0x1A = 6).
#include <array>
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/match.hpp"
#include "game/rules.hpp"
#include "game/scorers.hpp"

namespace em {

// Prize amounts (raw): promotion into/within the first division, promotion below, each title, cup win.
constexpr long kPromotionPrize = 1500000, kPromotionPrizeLower = 900000,
               kTitlePrize = 300000, kCupPrize = 1500000;

// Prize money of the season review (raw); `league` is the manager's league (0 = first division).
long season_prizes(bool promoted, int league, bool scorer, bool goalkeeper, bool fair_play);

// One player's season statistics as used to find the best scorer and goalkeeper.
struct SeasonScorer {
    int team = -1, position = 0, goals = 0;  // team index, position value (0 = goalkeeper), goals (goals against for goalkeepers)
    int assists_or_minutes = 0;              // assists for skaters, minutes for goalkeepers (u16 in the save)
    int games = 0;
};
// Sorts all player records like the original (exchange sort; skaters by goals + assists, goalkeepers
// by -goals against * 6000 / minutes with at least 400 minutes; fewer games first on a tie) and then
// takes the first skater and goalkeeper of the manager's league (teams first_team..last_team).
// A skater needs more than one point, a goalkeeper at least 399 minutes. Assists/minutes are read as
// signed 16-bit values. Returns the clubs of the best skater and goalkeeper (-1 = none).
std::array<int, 2> season_scoring_winners(const std::vector<SeasonScorer>& players,
                                          int first_team, int last_team);

// One season of a manager's career history.
struct CareerSeason {
    int place = 1;  // 1..12, or first-division play-off code 51/52/53
    int league = 0;
    bool champion = false;
    int cup_round = 0;
    bool cup_winner = false;
    int euro_phase = 0;     // 0 / 30 = did not take part
    bool euro_winner = false;
};
// Writes the annual four-byte record of the manager history (manager block +0x18 + 4 * year), at
// most 50 seasons: from year 50 on the history shifts up by one record and the counter at block
// +0xEE is incremented. Bytes: place (+0x80 champion) + 12 * league, league, cup round (+0x80
// winner), EuroCup phase + 8 (0x8D winner). A nonparticipant's EuroCup byte is left untouched,
// including after a shift. Throws FormatError for out-of-range values.
void append_career_season(Bytes& state, int manager, int year, const CareerSeason& season);

// The season-end financial licence withdrawal: due when the balance is below -2,000,000 raw in the
// first or second division, neither of the two title flags (season flags 0x28) protects the club,
// and fewer than two cases of this league were handled already.
bool financial_license_due(long balance, int league, uint8_t season_flags, int withdrawals);

// Puts the club that loses its licence into the relegation working schedule (the four teams of the
// promotion/relegation round), indexed by the saved table permutation `ranking`. The first loser
// place (from place withdrawals + 2) still held by a team with an index below the end of this
// league (i.e. not a lower-division team) is replaced; if both
// remaining losers already belong to the lower division, the first of them takes the matching
// winner slot and the club takes its place. The original participant IDs stay intact otherwise
// and another occurrence of the club is not removed. `withdrawals` is the number of earlier licence
// cases in this division (0 or 1). Throws FormatError for invalid input.
void insert_financial_relegation(std::array<int, 4>& working_teams,
                                const std::array<int, 4>& ranking,
                                int league, int team, int withdrawals);

// Age above rand(37, 42)?
bool player_retires(int age, BorlandRandom& rng);

// Age and skills of a newly generated player.
struct NewPlayer {
    int age = 0, skill1 = 0, skill2 = 0;
};
// The replacement for a retired player: age rand(18, 25), skills rand(base - 5, base + 5) around
// base = rand(40, 92).
NewPlayer new_player(BorlandRandom& rng);

// New season values of a club (mode 3), run after promotion and relegation for the 52 German teams
// with the game random generator:
//   r = rand(0, 6); trend = rand(0, 2r + 6) - 6
//   skill 1 of both kinds += rand(trend - 1, trend + 1); skill 2 likewise (the rand(0, 2) chance is
//   drawn but ignored in this mode); both kept in the bounds of the club's class (first division
//   69..94, second 39..73, Oberliga 15..43, read from EM.EXE)
//   form of both kinds = rand(45, 55); in version 1.00 the loop runs over three bytes, so the
//   energy of the first defence line (+0x1E) is overwritten with rand(45, 55) as well (original
//   quirk, one more draw per team); version 1.30 re-rolls the two form values only (reroll_form)
// This matches the club values of the original from the last day of a season to the first of the next.
struct RatingBounds {
    std::array<std::array<int, 2>, 3> bounds{};  // [class] = {low, high}
};
// Reads the class bounds from the EM.EXE data segment (offset 0x0125, three byte pairs); throws
// FormatError if the first pair is not {69, 94}.
RatingBounds load_rating_bounds(const Bytes& exe);
// mode 3: new season (class bounds, the skill 2 step always happens); mode 1: the twice-weekly drift of the daily loop:
// r = rand(0, 2), trend = rand(0, 2r + 2) - 2, bounds 1..99, skill 2 changes only if rand(0, 2) == 0.
void rerate_team(HokTeam& team, int team_class, const RatingBounds& b, GameRandom& rng, int mode = 3,
                 const GameRules& rules = GameRules{});
// The last step of the rating, which every team of the routine gets (the national teams only this
// step): form of both kinds = rand(45, 55), and in version 1.00 also the energy of the first defence
// line (+0x1E) = rand(45, 55).
void reroll_form(HokTeam& team, GameRandom& rng, const GameRules& rules);

}  // namespace em
