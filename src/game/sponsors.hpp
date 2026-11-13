// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Advertising and sponsors: the sponsor offers of each manager, the renewal of contracts, and the
// club image that decides how much sponsors pay. Amounts are raw (shown DM times 3).
//
// Contracts: 11 slots of 10 bytes per manager (save state 0xACFC + manager * 0x6E): slots 0..6 are
// the boards ("Banden"), 7..10 one each for the offer kinds 1..4. A slot holds {long amount per month,
// +4 sponsor of the boards (0xFF = free), +5 months left, +6 renewal option in months (bit 7 =
// offer pending), +7 sponsor id, +8 u16 image the sponsor requires}. The monthly income
// (game/finances) adds the amounts; on the 1st every running contract loses a month.
//
// Offers: state 0xA52C + manager * 0x1F4, 5 kinds x 10 offers of the same 10-byte layout (+4 =
// number of boards the sponsor wants). generate_sponsor_offers builds them in the middle
// (month_length / 2) and on the last day of the month (forced: all of them, at the game start),
// with D = image * 10:
//   for offer i of kind k: unless forced, keep the offer if rand(0, 2i/3) != 0; kind 2 offers
//   5..9 are removed; a new offer appears if rand(0, 5 - D/200) == 0 or rand(0, 4) > i, else the
//   slot is emptied. New offer: r = rand(20, 22),
//     amount = rand(D*r, D*r/10*13) * kind_factor[k] / 10      (kind factors 8 20 32 15 15)
//            * ((i + 3 * (k == 2)) * 8 + 65) * 6 / 100 * (economy - 20) / 100  (economy = 110)
//     boards = rand(2, 3) if rand(0, r - 15) == 0, else 1
//     years = rand(1, 2); with chance 1/8 a renewal option of rand(1, 3 - years) * 12 months
//     months = years * 12, required image = D / 10
// Pending offers that were not taken expire at the end of the day.
// Renewal: a contract with an option that runs out is renewed for the option's
// months if rand(90, 120) * required image / 100 < the current image, else the sponsor leaves.
//
// Image (0..100, stored at manager block +0x173):
//   S = (2 - league) * (A * 80 + 170) + 32 * (C - 2) + 47 * E + 12 * (11 - F) + V
//     + rand(20, 50) * stars + 3 * stadium score
//   A = 1 if the team takes part in the play-offs / relegation round, C = (in the cup) * (cup
//   round + 1), or 2 before the first draw, E = (in the European cup) * (European cup round + 1),
//   F = mean of (result + 1) over the last up to 5 matchdays (divided by matchday - max(matchday
//   - 5, 0), so too few games early in the season lower it; 6 without any; the results are the
//   table positions of the fever curve: odd matchdays use the high nibble of the table byte, even
//   ones the low nibble), V = mean of value / 5500 over the squad, stars = players with value /
//   7000 > V * rand(35, 55) / 10 (at most 4)
//   image = min((S / 12 + (2 - league) * 10) * 85 / 100, 100)
//   The constant factors 32, 47 and 12 are themselves drawn as rand(32, 32) etc. in the original,
//   so each consumes a random number.
// Stadium score: (places[0] * 3 / 2 + places[1]) / 2 (at most 50000) + sum of level[i]
//   * weight[i] (4333 2333 1166 2833 2000 3333 2333 3166 2250) with level 0 lowered by one,
//   level 2 at least 1 and levels 3 and 4 fixed at 2 and 3 (as in the original); result / 700.
// The formula reproduces the image stored in the original's saves.
#include <array>
#include <cstdint>
#include <vector>

#include "formats/binary.hpp"
#include "formats/hok.hpp"
#include "game/contracts.hpp"
#include "game/scorers.hpp"

namespace em {

constexpr int kSponsorKinds = 5, kSponsorOffers = 10;  // offer kinds, offers per kind

// Constants read from EM.EXE.
struct SponsorTables {
    std::array<long, kSponsorKinds> kind_factor{};  // amount factor per offer kind (8 20 32 15 15)
    long economy = 110;                              // economy level used in the amount formula
    std::array<long, 9> stadium_weight{};            // weight per stadium facility in the stadium score
};

// Reads the sponsor constants from the EM.EXE data segment (offsets 0x4F05, 0x02C0, 0x4F9C, i32
// each); throws FormatError if they do not look like V1.00.
SponsorTables load_sponsor_tables(const Bytes& exe);

// One 10-byte advertising record (offer or contract).
struct AdRecord {
    long amount = 0;     // raw per month, 0 = none
    int boards = 0;      // +4 offers: boards wanted (contracts: sponsor of the boards, 0xFF = free)
    int months = 0;      // +5 contract length / months left
    int renewal = 0;     // +6 renewal option in months (bit 7 = offer pending)
    int sponsor = 0;     // +7 sponsor id
    int min_image = 0;   // +8 u16 image the sponsor requires
};

// Offers of one manager, [kind][offer].
using SponsorOffers = std::array<std::array<AdRecord, kSponsorOffers>, kSponsorKinds>;

// Reads / writes the offer block of a human manager (0..3) at state 0xA52C + manager * 0x1F4.
// Both throw FormatError for an invalid manager.
SponsorOffers sponsor_offers_from_save(const Bytes& state, int manager);
void store_sponsor_offers(Bytes& state, const SponsorOffers& offers, int manager);

// Renews the offers as described above (`forced`: rebuild all of them).
void generate_sponsor_offers(const SponsorTables& t, int image, bool forced, SponsorOffers& offers,
                             BorlandRandom& rng);

// Does a sponsor with a renewal option stay? rand(90, 120) * required image / 100 < image.
bool sponsor_renews(const AdRecord& contract, int image, BorlandRandom& rng);

// The stadium score of the image formula (see above) from the 9 facility levels and the two
// place counts. Uses the original's unsigned 16-bit arithmetic for the places.
long stadium_score(const SponsorTables& t, const std::array<int, 9>& stadium, const std::array<int, 2>& places);

// The inputs of the image formula.
struct ImageInputs {
    int league = 0;          // 0..2
    int in_competition = 1;  // A
    int cup = 2;             // C
    int euro = 0;            // E
    int matchday = 0;                 // next matchday of the league
    std::vector<int> recent_results;  // fever-curve positions for matchdays max(matchday - 5, 1) .. matchday - 1
    std::vector<long> squad_values;   // raw market values
    long stadium = 0;                 // stadium_score()
};

// The competition factors A, C and E of the image formula.
struct ImageCompetitionFactors {
    int playoffs = 0;  // A: active play-off or relegation participant
    int cup = 2;       // C: active cup participant * (draw round + 1), 2 before a draw
    int euro = 0;      // E: active EuroCup participant * (completed rounds + 1)
};

// Determines A, C and E for `manager` (0..5) from the current play-off draw, relegation and
// European cup groups and the DEB cup draw in the save state, as the original's participation
// queries do. These use the current draw/bracket rather than the manager's historical phase.
// Throws FormatError for an invalid manager or a truncated state.
ImageCompetitionFactors image_competition_factors(const HokFile& save, int manager);

// The club image from precomputed squad market values.
int club_image(const ImageInputs& in, BorlandRandom& rng);

// The club image as computed on the original's day path: the market values are recalculated
// separately in the mean and the star loop, so the random draws a value calculation can make
// interleave with the image draws exactly as in the original. `in.squad_values` is not used.
int club_image(const ImageInputs& in, const std::vector<ContractPlayer>& squad,
               const ContractTables& contracts, BorlandRandom& rng);

}  // namespace em
