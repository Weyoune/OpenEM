// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Advertising and sponsors (overlays 8 and 4 of EM.EXE). Amounts are raw (shown DM times 3).
//
// Contracts: 11 slots of 10 bytes at DS:C15B + manager * 0x6E: slots 0..6 are the boards
// ("Banden"), 7..10 one each for the offer kinds 1..4. A slot holds {long amount per month,
// +4 sponsor of the boards (0xFF = free), +5 months left, +6 renewal option in months (bit 7 =
// offer pending), +7 sponsor id, +8 u16 image the sponsor requires}. The monthly income
// (game/finances) adds the amounts; on the 1st every running contract loses a month.
//
// Offers: DS:C313 + manager * 0x1F4, 5 kinds x 10 offers of the same 10-byte layout (+4 =
// number of boards the sponsor wants). ovl08_2ba7(forced) builds them on month_length/2 and the last
// day of the month (forced: all of them, at the game start), with D = image * 10:
//   for offer i of kind k: unless forced, keep the offer if rand(0, 2i/3) != 0; kind 2 offers
//   5..9 are removed; a new offer appears if rand(0, 5 - D/200) == 0 or rand(0, 4) > i, else the
//   slot is emptied. New offer: r = rand(20, 22),
//     amount = rand(D*r, D*r/10*13) * kind_factor[k] / 10      (DS:4F05: 8 20 32 15 15)
//            * ((i + 3 * (k == 2)) * 8 + 65) * 6 / 100 * (economy - 20) / 100  (DS:02C0 = 110)
//     boards = rand(2, 3) if rand(0, r - 15) == 0, else 1
//     years = rand(1, 2); with chance 1/8 a renewal option of rand(1, 3 - years) * 12 months
//     months = years * 12, required image = D / 10
// Pending offers that were not taken expire at the end of the day (ovl10_1cd2).
// Renewal (ovl04_0ee0): a contract with an option that runs out is renewed for the option's
// months if rand(90, 120) * required image / 100 < the current image, else the sponsor leaves.
//
// Image, ovl08_2893 (0..100, stored at manager block +0x173 = DS:97CE):
//   S = (2 - league) * (A * 80 + 170) + 32 * (C - 2) + 47 * E + 12 * (11 - F) + V
//     + rand(20, 50) * stars + 3 * stadium score
//   A = 1 if the team takes part in competition 0x40 (ovl10_18f8), C = (in the cup) * (DS:5CF3 +
//   1) or 2 while DS:5CF3 = 0, E = (in the European cup) * (DS:07AF + 1), F = mean of (result
//   + 1) over the last up to 5 matchdays (divided by matchday - max(matchday - 5, 0), so too
//   few games early in the season lower it; 6 without any; ovl10_0a9d: odd matchdays use the high
//   nibble of the table byte, even ones the low nibble), V = mean of
//   value / 5500 over the squad, stars = players with value / 7000 > V * rand(35, 55) / 10 (at
//   most 4)
//   image = min((S / 12 + (2 - league) * 10) * 85 / 100, 100)
// Stadium score, ovl08_1c4a: (places[0] * 3 / 2 + places[1]) / 2 (at most 50000) + sum of level[i]
//   * DS:4F9C[i] (4333 2333 1166 2833 2000 3333 2333 3166 2250) with level 0 lowered by one,
//   level 2 at least 1 and levels 3 and 4 fixed at 2 and 3 (as in the original); result / 700.
// Checked: MONAT1 holds image 70; the formula gives 70 with A = 1, C = 2 and the RAM inputs.
#include <array>
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/contracts.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr int kSponsorKinds = 5, kSponsorOffers = 10;

struct SponsorTables {
    std::array<long, kSponsorKinds> kind_factor{};  // DS:4F05
    long economy = 110;                              // DS:02C0
    std::array<long, 9> stadium_weight{};            // DS:4F9C
};

SponsorTables load_sponsor_tables(const Bytes& exe);

struct AdRecord {
    long amount = 0;     // raw per month, 0 = none
    int boards = 0;      // +4 (contracts: sponsor of the boards, 0xFF = free)
    int months = 0;      // +5
    int renewal = 0;     // +6
    int sponsor = 0;     // +7
    int min_image = 0;   // +8
};

using SponsorOffers = std::array<std::array<AdRecord, kSponsorOffers>, kSponsorKinds>;

// Native HOK block at state 0xA52C + manager * 0x1F4 (ovl16_11bd / ovl16_1bc3).
SponsorOffers sponsor_offers_from_save(const Bytes& state, int manager);
void store_sponsor_offers(Bytes& state, const SponsorOffers& offers, int manager);

// ovl08_2ba7
void generate_sponsor_offers(const SponsorTables& t, int image, bool forced, SponsorOffers& offers,
                             BorlandRandom& rng);

// ovl04_0ee0: does a sponsor with a renewal option stay?
bool sponsor_renews(const AdRecord& contract, int image, BorlandRandom& rng);

// ovl08_1c4a
long stadium_score(const SponsorTables& t, const std::array<int, 9>& stadium, const std::array<int, 2>& places);

struct ImageInputs {
    int league = 0;
    int in_competition = 1;  // A
    int cup = 2;             // C
    int euro = 0;            // E
    int matchday = 0;                 // next matchday of the league (DS:07A9)
    std::vector<int> recent_results;  // ovl10_0a9d for matchdays max(matchday - 5, 1) .. matchday - 1
    std::vector<long> squad_values;   // raw market values
    long stadium = 0;                 // stadium_score()
};

struct ImageCompetitionFactors {
    int playoffs = 0;  // A: active play-off or relegation participant
    int cup = 2;       // C: active cup participant * (draw round + 1), 2 before a draw
    int euro = 0;      // E: active EuroCup participant * (completed rounds + 1)
};

// Read the native participation queries of ovl10_18f8 (codes 0x40, 8, 0x20).
// These use the current draw/bracket, rather than the manager's historical phase.
ImageCompetitionFactors image_competition_factors(const HokFile& save, int manager);

// ovl08_2893
int club_image(const ImageInputs& in, BorlandRandom& rng);

// Native day path: calculate market values separately in the mean and star
// loops, preserving flagged-player RNG calls between the image draws.
int club_image(const ImageInputs& in, const std::vector<ContractPlayer>& squad,
               const ContractTables& contracts, BorlandRandom& rng);

}  // namespace em
