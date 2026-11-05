// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "game/schedule.hpp"
#include "ui/classic_canvas.hpp"
#include <array>

namespace em::ui {
struct ClassicLeagueFixtureRow {
    int home=-1,away=-1,baseline=48;
    uint8_t home_color=30,away_color=1;
    std::string native_home,native_away,native_home_stats,native_away_stats,native_score;
};
struct ClassicLeagueFixtures {
    int league=0,round=1,next_round=1,outgoing_numeric_width=1;
    GameDate preview_date{};
    std::string native_title,native_competition;
    std::array<ClassicLeagueFixtureRow,6> rows;
    std::array<HokPairing,6> pairings;
};
enum class ClassicLeagueAction { None, Previous, Next, Competition, ClubInfo };
struct ClassicLeagueHit {ClassicLeagueAction action=ClassicLeagueAction::None;int team=-1,mode=2;};
//6668:15B3(league,0,0,1). Pure view: saved caches, signed historical scores,
//and native formatter width; no RNG, strength refresh or date writes.
ClassicLeagueFixtures classic_league_fixtures(const HokFile&,const ScheduleTables&,
    const SeasonCalendar&,int league,int round=-1,int incoming_numeric_width=1);
void draw_classic_league_fixtures(ClassicCanvas&,const HokFile&,const ScheduleTables&,
    const SeasonCalendar&,int league,int round=-1,int incoming_numeric_width=1);
ClassicLeagueHit classic_league_fixtures_hit(const ClassicLeagueFixtures&,int x,int y);
void prepare_classic_league_fixtures_working(HokFile&,const ScheduleTables&,
    const SeasonCalendar&,int league,int round=-1,int incoming_numeric_width=1);
void finish_classic_league_fixtures_working(HokFile&,const ScheduleTables&,int league);
} //namespace em::ui
