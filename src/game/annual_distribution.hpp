// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Season change: how the players of the global player array (savegame state 0x4FC6, see formats/hok.hpp) are
// moved between the computer-managed clubs. All functions work directly on the raw savegame state bytes; player
// ids are 1-based (record of id at 0x4FC6 + (id - 1) * 0x21), the team index of a player is its byte +0x1D
// (255 = no club, 64..149 = a club abroad). Human-managed clubs (team record +0x16 != 0) never receive players.
// Random numbers come from the game's random generator through a ContractRandomDraw (inclusive range).
#include "game/contracts.hpp"

namespace em {
// Positions in the 17-entry weighted cycle per league (first division, second division, Oberliga); they persist
// from season to season and are part of the saved game state.
using DistributionCursors=std::array<uint8_t,3>;
// A computer-managed German club (team 0..42) for player `player`, chosen by strength: clubs are drawn at random
// until one is accepted. A club is accepted when draw(2, tolerance) > |club quality - player strength|, where the
// player strength is (skill 1 + skill 2 + form) / 3 and the club quality is the sum of its four skill bytes plus
// 100, divided by 6. The tolerance widens from 10 to 20 after 100 and to 30 after 200 tries; after 300 tries any
// computer club is accepted. Throws FormatError for an invalid state or player id.
int annual_cpu_target(const Bytes&,uint8_t player,const ContractRandomDraw&);
// The next computer-managed club of `league` (0..2) in the weighted cycle over the league's twelve team indices:
// the first five clubs of the league come up twice in 17 steps, the other seven once (the cursor of the league
// also advances past human clubs, which are skipped). The original passes
// a ranking argument here that it never uses. League 3 draws a club abroad (64..150); the annual distribution
// never asks for it.
int annual_cyclic_target(const Bytes&,int league,DistributionCursors&,const ContractRandomDraw&);
// Target numbers of players per league [0..2] (entry 3 stays 0), computed after the random club changes with a
// fresh count of players abroad: the players still to place, 179 minus (players in human squads + players abroad),
// are split in proportion to the human squad players of each league (only managers playing in that league count).
// The original computes with 8-bit values, which this reproduces.
std::array<int,4> annual_distribution_quotas(const Bytes&);
// The random part of the season-change player shift, run right after the global skill changes of
// annual_global_skills: rand(5, 15) changes. A drawn player (ids 1..179) of a human-managed club is drawn again.
// While more than four players play abroad, every drawn player abroad is brought back to a computer club
// (annual_cpu_target) without counting as one of the changes; otherwise the drawn player of a computer club (or
// without club) moves to a club chosen by annual_cpu_target.
void annual_random_club_changes(Bytes&,const ContractRandomDraw&);
// Fills the leagues up to the quotas (from annual_distribution_quotas): each league's deficit is the quota minus
// the players it already has; players whose record byte +0x1A is above 3 form the donor pool of their league.
// Donors are drawn at random from the other leagues' pools and moved to the next club of the cycle
// (annual_cyclic_target). Loops are bounded (1000 rounds / 1000 redraws) like in the original.
void annual_rebalance_clubs(Bytes&,std::array<int,4>,DistributionCursors&,const ContractRandomDraw&);
// The whole distribution: random changes, quotas, rebalancing. Raw bytes only: the caller supplies current team
// ratings and manager leagues in the state, synchronises its decoded team and player views afterwards, and keeps
// the cursors across loads of a live game.
void annual_distribute_players(Bytes&,DistributionCursors&,const ContractRandomDraw&);
} // namespace em
