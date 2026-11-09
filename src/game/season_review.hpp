// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "game/calendar.hpp"

namespace em {

// The manager's page of the season review (ovl02_0000 = ovr026, docs/logic/MELDUNGEN.md): what the
// original draws for the human manager between the season overview and the summer. Game::new_season()
// fills it; the UI shows it once after the step and discards it with the staged game.
struct SeasonReview {
    // Manager history row of the season just ended (manager +0x18 + 4 * min(career year, 49)): place code and
    // league, read by ovl04_1eb9 for the line "10. Platz der Bundesliga".
    uint8_t place = 0, league = 0;
    // Event symbols of KB13 in drawing order (ovr026:022A..03FA): season flags +0xF3 bits 0..6 through DS:4B2E
    // {2,1,0,5,4,6,8}, then 12 best scorer, 3 best goalkeeper, 13 fair play, 7 German EuroCup entrant and 9 for
    // every youth player who joins. At most 12 fit the panel; the original draws more over the frame.
    std::vector<uint8_t> icons;
    long prizes = 0;   // raw DS:E289[manager]: cup prize (ovr038:3E55) plus the review prizes
    long balance = 0;  // raw balance in the header, before the review prizes
    GameDate date{};   // header date: the closing section-end day

    // The overview page "Saisonende" before it (ovl12_1657 = ovr036:1657, Amiga FUN_00146dd0): world champion,
    // cup winner, champion and European cup winner, and for each of the three leagues the clubs relegated into it
    // (DS:D0E7, the champion in the first division) and promoted into it (DS:D0ED), at most two each (DS:07B9).
    std::array<std::string, 4> winners;
    std::array<bool, 4> human_winner{};  // the manager's face and name under that title
    std::array<std::vector<std::string>, 3> relegated_into, promoted_into;
};

// DS:4B2E: symbol of each season flag bit (+0xF3): 0 promoted, 1 relegated, 2 licence withdrawn, 3 champion,
// 4 cup winner, 5 EuroCup winner, 6 (0x40, kept from the season).
inline constexpr uint8_t kSeasonFlagIcons[7] = {2, 1, 0, 5, 4, 6, 8};
inline constexpr uint8_t kIconScorer = 12, kIconGoalkeeper = 3, kIconFairPlay = 13, kIconEuroEntrant = 7,
                         kIconYouth = 9;

}  // namespace em
