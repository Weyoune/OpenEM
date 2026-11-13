// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "game/schedule.hpp"
#include "ui/classic_canvas.hpp"
#include <array>
#include <vector>

// Classic league fixtures page ("Paarungen"): view model, renderer, hit
// testing and the page's effects on the game's working state.
namespace em::ui {
// One pairing of the round.
struct ClassicLeagueFixtureRow {
    int home=-1,away=-1,baseline=48;          // team indices, text baseline
    uint8_t home_color=30,away_color=1;       // 24 for teams run by a human manager
    std::string native_home,native_away,native_home_stats,native_away_stats,native_score;  // game encoding
};
// The page of one league round.
struct ClassicLeagueFixtures {
    int league=0,round=1,next_round=1;  // league 0..2, shown round, next round to play (1-based)
    int outgoing_numeric_width=1;       // number width the game leaves set after the page (2 if the round was played)
    GameDate preview_date{};            // date of the shown round
    std::string native_title,native_competition;  // title; label of the middle button ("Play-Off"/"Aufstieg")
    std::array<ClassicLeagueFixtureRow,6> rows;
    std::array<HokPairing,6> pairings;
};
// What a click on the page does.
enum class ClassicLeagueAction { None, Previous, Next, Competition, ClubInfo };
// Click result; team and club-info mode (0 home, 1 away) for ClubInfo.
struct ClassicLeagueHit {ClassicLeagueAction action=ClassicLeagueAction::None;int team=-1,mode=2;};
// Builds the page for league (0..2) and round (1..44; -1 = the next round,
// or round 1 after the regular season). incoming_numeric_width is the
// number width the game has set when the page is opened; it is used for the
// round number in the title. Reads stored data only (cached positions,
// signed historical scores): no random numbers, no strength refresh and no
// date writes. Throws FormatError on invalid arguments or data.
ClassicLeagueFixtures classic_league_fixtures(const HokFile&,const ScheduleTables&,
    const SeasonCalendar&,int league,int round=-1,int incoming_numeric_width=1);
// Builds and draws the page with its three buttons.
void draw_classic_league_fixtures(ClassicCanvas&,const HokFile&,const ScheduleTables&,
    const SeasonCalendar&,int league,int round=-1,int incoming_numeric_width=1);
// The page during the matches: the running round with "<m>.MINUTE" and the live scores
// {home, away, goals home, goals away}; no buttons, waits for a click.
void draw_classic_league_live(ClassicCanvas&,const HokFile&,const ScheduleTables&,const SeasonCalendar&,
    int league,int minute,const std::vector<std::array<int,4>>& scores);
// Maps a click to an action.
ClassicLeagueHit classic_league_fixtures_hit(const ClassicLeagueFixtures&,int x,int y);
// Applies the page's side effects to the working state when a round is
// shown: current pairings and date are set to that round's.
void prepare_classic_league_fixtures_working(HokFile&,const ScheduleTables&,
    const SeasonCalendar&,int league,int round=-1,int incoming_numeric_width=1);
// Restores the current pairings to the league's next round when the page is left.
void finish_classic_league_fixtures_working(HokFile&,const ScheduleTables&,int league);
} //namespace em::ui
