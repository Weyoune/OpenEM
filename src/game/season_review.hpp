// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Data of the season review pages shown at the end of a season ("Saisonende" overview and the
// manager's own page), filled by Game::new_season() and drawn by the UI.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "game/calendar.hpp"

namespace em {

// The manager's page of the season review: what the original draws for the human manager between
// the season overview and the summer. Game::new_season() fills it; the UI shows it once after the
// step and discards it with the staged game.
struct SeasonReview {
    // Manager history row of the season just ended (manager +0x18 + 4 * min(career year, 49)): place code and
    // league, as used for the line "10. Platz der Bundesliga".
    uint8_t place = 0, league = 0;
    // Event symbols of KB13 in drawing order: season flags +0xF3 bits 0..6 mapped through kSeasonFlagIcons
    // {2,1,0,5,4,6,8}, then 12 best scorer, 3 best goalkeeper, 13 fair play, 7 German EuroCup entrant and 9 for
    // every youth player who joins. At most 12 fit the panel; the original draws more over the frame.
    std::vector<uint8_t> icons;
    long prizes = 0;   // raw season prize total of the manager: cup prize plus the review prizes
    long balance = 0;  // raw balance in the header, before the review prizes
    GameDate date{};   // header date: the closing section-end day

    // The overview page "Saisonende" before it: world champion, cup winner, champion and European cup winner,
    // and for each of the three leagues the clubs relegated into it (the champion in the first division) and
    // promoted into it, at most two each.
    std::array<std::string, 4> winners;  // names: world champion, cup winner, champion, European cup winner
    std::array<bool, 4> human_winner{};  // the manager's face and name under that title
    std::array<int, 4> winner_manager{-1, -1, -1, -1};  // the human manager (0..3) holding each title, -1 = none
    std::array<std::vector<std::string>, 3> relegated_into, promoted_into;
};

// Symbol of each season flag bit (manager block +0xF3): 0 promoted, 1 relegated, 2 licence withdrawn,
// 3 champion, 4 cup winner, 5 EuroCup winner, 6 (0x40, kept from the season).
inline constexpr uint8_t kSeasonFlagIcons[7] = {2, 1, 0, 5, 4, 6, 8};
// Symbols for the review titles and events (indices into KB13).
inline constexpr uint8_t kIconScorer = 12, kIconGoalkeeper = 3, kIconFairPlay = 13, kIconEuroEntrant = 7,
                         kIconYouth = 9;

}  // namespace em
