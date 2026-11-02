// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// End of a season (day loop ovl14_2e0f once the year rolls over, then ovl16_30ed, ovl14_00ad,
// ovl00_058a, 162c:1c5b, ovl06_099d, ovl05_51a5, ovl02_0000 and ovl16_0480 twice; afterwards
// the year DS:03BD is incremented and the tables are cleared). This file covers ovl02_0000, the
// season review of each human manager and the player turnover. Amounts are raw (DM * 3).
//
// Prizes (ovl02_0000, added to the balance and to DS:E289[manager]):
//   champion (achievement bit 0 of manager block +0xF3): 1,500,000 raw (500,000 DM) in the
//   first division, 900,000 raw (300,000 DM) below; every advertising contract then gets a
//   renewal offer (+6 bit 7)
//   cup winner, a second title (high / low byte of the caller's word) and ovl00 1000:0e20(0):
//   300,000 raw (100,000 DM) each
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

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/match.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr long kChampionPrize = 1500000, kChampionPrizeLower = 900000, kTitlePrize = 300000;

// Prize money of the season review (raw).
long season_prizes(bool champion, int league, bool cup, bool second_title, bool third_title);

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
