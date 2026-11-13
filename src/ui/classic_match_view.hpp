// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Match display of the original game, shown while a day is played minute by minute:
//   - one scoreboard per watched match, 45 lines apart from y = 4: matches of clubs whose reporting mode is 2 ("Tore",
//     team byte +0x34 bit 7), at most four;
//   - the minute panel bottom left (three period bars and the minute in KB/38 digits), the reporter and the message
//     panel bottom right, where every goal of a reported match without a board is flashed (classic_goal_flashes);
//   - without any board but with a display switch on: "Spiele laufen" and the minute panel.
// Plus the goal scenes, the board messages, and the cup and league pages shown during the matches.
// The frames are snapshots of the day computed by Session::advance_day; drawing never changes the game.
// Team-record offsets (+0x..) below refer to the 61-byte team records of the save state.
#include "formats/hok.hpp"
#include "game/calendar.hpp"
#include "ui/classic_canvas.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace em {
class Game;
struct LiveMatch;
}

namespace em::ui {

// One side of a scoreboard.
struct ClassicBoardTeam {
    std::string name;   // UTF-8
    int crest = 143;    // KB/80 + (team +0x34 & 63), 143 for the national teams
    int line[2] = {};   // +0x2E / +0x30: lines on the ice (defence, attack)
    int energy[2] = {}; // +0x1E: energy of the line on the ice per kind
    int skaters = 5;    // +0x32
    int penalty_code = 0;  // +0x33: low nibble first penalty, high nibble second (minutes left & 7)
    int effort = 0;     // +0x26
    int offence = 0;    // +0x2C bits 4..6
    int chances[3] = {};   // per period, shown up to 9
    int penalty_minutes = 0;
    int shootout_goals = 0;  // goals of a shoot-out (part of the score, no chance message)
    int colour = 0;          // +0x3B: shirt colour of the goal scenes (0xRGB nibbles)
    bool human = false;      // +0x34 bit 7: the manager's club (its match plays the goal scenes)
    int id = -1;             // team record
    bool fourth_line[2] = {};  // a squad player with a role sits in line 4 (defence / attack): the fourth lamp is lit
    // The manager's club only: goal and assist counters of the squad players (squad goals of all competitions,
    // player record +0x1F). The board message of a goal names the players whose counters rose in that minute.
    struct Scorer { std::string name; int goals = 0, assists = 0; bool injured = false; };  // injured: squad +0x09 status 2
    std::vector<Scorer> scorers;
    // The manager's club: the game's scorer / assist picks for its chances and shoot-out shots of the day so far, in order
    // (Game::chance_credits); the board message of a chance names them, also for a missed chance.
    struct Credit { bool goal = false, shot = false; std::string scorer, assist; };
    std::vector<Credit> credits;
    std::string penalized;    // the player penalised in this minute (manager's club, message draws only)
    int penalty_reason = -1;  // the game's rand(0, 12) pick from its 13 penalty reasons for a two-minute penalty in this
                              // minute, -1 none
};

// One scoreboard (one watched match).
struct ClassicBoard {
    std::array<ClassicBoardTeam, 2> team;
    int goals[2] = {};          // shown modulo 30 (the original marks decisions with +30)
    int period_goals[3][2] = {};
    long attendance = 0;        // home game of a manager, 0 = "(ausw.)"
    bool sold_out = false;      // attendance equals the capacity: colour 0x10
    int arena = 2;              // goal scene rink: 4 * attendance / capacity at the manager's home, else 2
    // The goal scenes the simulation drew in this minute (MatchProgress::scenes), in chance order: home before
    // away, the shots of a shoot-out last. Empty without goal scenes; the view then picks the scenes itself.
    struct Scene { int side = 0; bool goal = false; int number = 2; char variant = 0; bool penalty = false; bool shown = false; };
    std::vector<Scene> scenes;
    std::vector<std::pair<int, bool>> shots;  // the shoot-out shots of this minute (side, goal): board messages kind 5 / 6
};

// Live result of one pair of a cup round (or league round) for the table after minute 60 / 65 / 70.
struct ClassicCupLive {
    int home = -1, away = -1;
    int goals[2] = {};
    int markers = 0;  // 1 after the regular time of a drawn game (the game stores +30), 2 after a shoot-out
};

// A match without a board whose goals the original flashes in the message panel (a club with reporting
// mode 1 or 2, team byte +0x34 & 0xC0; mode 2 clubs beyond the four boards included).
struct ClassicReportedMatch {
    int home = -1, away = -1;
    std::string names[2];  // UTF-8
    int goals[2] = {};     // goals from chances (without shoot-out goals)
};

// The goal flash over the message panel: home name centred on x 0xF5 (y 0xC0), "-" centred on 0x127, away name centred
// on 0x10D (y 0xC7), all in the small font, colour 1 with black shadow, a colon of two short diagonals; after delay(50)
// the score in KB/38 digits at (0xEF, 0xCC) (the home goals reduced below 30), then delay(70) and the panel is restored
// from the background page.
struct ClassicGoalFlash {
    std::string home, away;  // UTF-8
    int goals[2] = {};       // score after the goal
    bool score = false;      // second stage: the digits are drawn
};

// The board message (only at speed < 70): an event of a match with a board, shown in the message panel through a
// 0x6F x 0x20 work buffer copied to (0xC7, 0xB7) over the panel restored from the background page.
//   kind: 0 / 1 chance missed / goal ("Chance:", "Vergeben" / "TOR!"), 2 / 3 penalty ("Strafzeit:", "2 Minuten" /
//   "5 Minuten"), 4 injury ("Verletzt:", no result), 5 / 6 penalty shot ("Penalty:"). The reason of a penalty is
//   "wegen " + one of the game's 13 reasons picked with rand(0, 12), always reason 5 for five minutes.
// Steps (without goal scenes; with goal scenes switched on only the final lines):
//   Caption  the caption in the bold font (colour 0x18, black shadow) on baseline 0x14, blinking 4 times (2 at speed 60);
//   Team     plus the club name in the small font (colour 7) on baseline 0x1E;
//   Result   the result in the bold font on baseline 0x16, blinking 4 times (once at speed 60);
//   Final    the result in the small font on baseline 0x0A, the player on 0x12 and "Vorlage: <assist>" or the reason of
//            a penalty on 0x1A, all colour 0x18. Only drawn with a player: for a computer club the game passes player
//            record 0, which has no name, so its message ends with the bold-font result.
struct ClassicBoardMessage {
    enum class Step { Caption, Team, Result, Final };
    int kind = 0;        // 0..6, see above
    std::string team;    // UTF-8
    std::string player;  // scorer or penalised player; empty for a computer club
    std::string second;  // assist (without "Vorlage: ") or the penalty reason
    Step step = Step::Caption;
    bool text = true;    // Caption / Result: blink phase with the text (false: the cleared buffer)
    int lines = 1;       // Final: 1 = player, 2 = player and second line
};


// A goal scene ("Torszenen" switched on): the game plays one (after delay(30)) for every chance of every match on a board
// before its message. The scene replaces the whole screen: the rink of the TORE script (KB/231/232 tiles, map KB/236 or
// KB/242..245) as a 320 x 200 window at the camera with the puck and the 12 skaters of the script (KB/233..235), palette
// KB/247 with the team colours in 0xFE / 0xFD, and below it the panel KB/246 (VGA split screen) with the club names, the
// score, "<n> ZUSCHAUER" / "AUSWÄRTS" and "<m>.MINUTE". The script runs one frame per BIOS tick; after its end mark the
// score of a goal goes up by one and stays for delay(6).
struct ClassicGoalScene {
    std::string script;               // file in TORE, e.g. "3.at": n + ".h"/".a" (attacking side) + "t"/"v" (goal / miss)
    int arena = 2;                    // fill level 4 * attendance / capacity (2 away): 1..4 -> KB/242..245, else KB/236
    std::array<int, 2> colour{};      // team +0x3B after the clash rule (sum of the nibble differences < 6 -> away
                                      // = home ^ 0xFFF); DAC (c >> 6, c >> 2, c << 2) & 63
    std::array<std::string, 2> names; // UTF-8
    std::array<int, 2> goals{};       // the score shown (before the goal; +1 in the final frame)
    std::string info;                 // "<attendance> ZUSCHAUER" at the manager's home, else "AUSWÄRTS"
    int minute = 1;
    int frame = 0;                    // script frames applied (0 = the start positions; empty script: camera (0, 0), no
                                      // objects, the rink of Stufe 2 before its first scene)
    // The chance behind the scene (for a replay with r / w): attacking side, goal, plays so far (1..3)
    int side = 0;
    bool goal = false;
    int play = 1;
    bool final = false;               // the last frame (score after the goal, delay(6))
};
// One screen of the match display, with how long it stays.
struct ClassicMatchFrame {
    int minute = 1;                   // shown minute (1..70); before minute 1 the original shows 1
    std::vector<ClassicBoard> boards; // empty: "Spiele laufen"
    std::vector<ClassicReportedMatch> reported;  // reported matches without a board (goal flashes)
    std::optional<ClassicGoalFlash> flash;       // a goal flash drawn over this frame
    std::optional<ClassicBoardMessage> message;  // a board message drawn over this frame
    std::optional<ClassicGoalScene> scene;       // a goal scene drawn instead of the match view
    std::optional<ClassicGoalScene> after_scene; // the delay(20) after a scene: r / w replays this chance
    int hold_ms = -1;                 // how long a message step stays (-1: classic_match_frame_ms decides)
    bool settings_lamp = false;       // the "quick" switch bottom left is lit (opens the Einstellungen)
    bool live_scene = false;          // a scene shown at its chance (g_scene_replay): r / w after it answers the simulation
    bool blink = false;               // reporter eyes closed (KB/38 at 0x80) instead of open (KB/38 at 0x50)
    int cup_table = 0;                // 60 / 65 / 70: the DEB-Pokal table of the round, waits for a click
    std::vector<ClassicCupLive> cup;  // live results for cup_table
    // After minute 20 / 40 (switch Drittelpause) and 60 (Ergebnisse): the Paarungen page of the league with the running
    // scores, waits for a click. 1 + league, 0 = none.
    int league_page = 0;
    std::vector<ClassicCupLive> league;  // the pairs of that league with their running scores
    // True when the frame waits for a click instead of a timeout.
    bool waits() const { return cup_table != 0 || league_page != 0; }
};

// Draws one frame: boards (or "Spiele laufen"), minute panel, reporter, and any goal flash, board message, goal scene,
// cup table or league page it carries.
void draw_classic_match_view(ClassicCanvas& canvas, const ClassicMatchFrame& frame);

// A click during the minute loop: what the original writes into the match before the chances of the next minute.
// Only the boards of a manager's club react.
struct ClassicBoardOrder {
    bool quick = false;      // the switch bottom left (speed 70, no goal scenes; clicked again: back)
    int team = -1;           // team record of the clicked club
    int effort = -1;         // +0x26 from the effort bar (0..100)
    int offence = -1;        // +0x2C bits 4..6 from the offence bar (0..7)
    int line[2] = {-1, -1};  // +0x2E / +0x30 from the line lamps
    bool squad = false;      // the middle of the board: Aufstellung (not built)
};
// Maps a click on the match display to an order; nullopt if the click does nothing.
std::optional<ClassicBoardOrder> classic_board_click(const ClassicMatchFrame& frame, int x, int y);

// Draws a goal scene frame (ClassicMatchFrame::scene): palette, rink window, objects and panel.
void draw_classic_goal_scene(ClassicCanvas& canvas, const ClassicGoalScene& scene);
// Number of frames of a TORE script (frames before its 0xAAAA end mark).
int classic_goal_scene_length(const std::filesystem::path& data, const std::string& script);
// The TORE file of a chance: n + ".h" / ".a" (attacking side) + "t" / "v" (goal / missed); a penalty shot (kind 5 / 6 of
// the message; also when the original's rand(0, 0xD2 resp. 0xFF) gives 0) takes "2.?(t|v)e", otherwise a side with
// skaters + running penalties = 6 "2.?(t|v)j" (TORE/anzahl: two e and two j scenes, rand(2, 2) = 2).
std::string classic_goal_scene_script(int n, int side, bool goal, bool penalty_shot, bool six);
// The frames of one goal scene: script frames 1..n, one BIOS tick each, the final frame with the new score of a goal for
// delay(6) (two ticks), then delay(20) on `base` (the board) or, with rink_stays (level 2), on the final view; that
// frame carries after_scene, where r / w replays the chance (ClassicApplication). `scene` holds the score before the
// chance; `goal` adds one for `side`.
std::vector<ClassicMatchFrame> classic_goal_scene_frames(const std::filesystem::path& data, const ClassicMatchFrame& base,
                                                         ClassicGoalScene scene, int side, bool goal, bool rink_stays = false);
// The replay of a chance (a typed 'r' or 'w' after the scene, at most three plays): the original rolls a new scene;
// OpenEM takes the next script number of the same kind. Empty after the third play.
std::vector<ClassicMatchFrame> classic_goal_scene_replay(const std::filesystem::path& data, const ClassicMatchFrame& after);
// The scene of a chance on board b: rink fill, shirt colours after the clash rule, names, score, info line, minute.
ClassicGoalScene classic_goal_scene_of(const ClassicBoard& b, int minute);

// The DEB-Pokal page during the cup matches (after minutes 41-60, 61-65, 66-70): the page of Tabellen -> DEB-Pokal
// (draw_classic_cup) with the caption "<round>   <minute>.Minute" and the running game as the newest score column.
// before_day is the state before the day (draw, game index, earlier games of the series).
void draw_classic_cup_minute(ClassicCanvas& canvas, const HokFile& before_day, const SeasonCalendar& calendar, int minute,
                             const std::vector<ClassicCupLive>& live);
// The live results of all pairs of a cup round after a minute (observer of play_cup_round_live).
std::vector<ClassicCupLive> classic_cup_live(int minute, const std::vector<LiveMatch>& matches);

// The delay per minute while the display runs, in milliseconds: 30 (no button held) or 45 units of the game's delay
// routine, minus speed / 2; none from speed 70. One delay unit is a third of a BIOS tick (about 18.3 ms).
int classic_match_minute_ms(int speed);
// How long a frame stays: a minute (classic_match_minute_ms), or a goal flash stage independent of the speed:
// delay(50) = 16 BIOS ticks with the team names, delay(70) = 23 ticks with the score.
int classic_match_frame_ms(const ClassicMatchFrame& frame, int speed);

// Inserts the goal flashes into the observed minutes: every goal of a reported match without a board that falls in
// minute m becomes two frames (names, then names and score) shown before the frame of minute m. They are drawn over the
// previous minute's screen with the minute display already at m (the game updates the minute panel before the chances
// and the boards after them). Home goals of a minute come first (the minute loop asks the home side first). The original
// only flashes while a board is shown (not with "Spiele laufen"). Cup table frames are left as they are.
std::vector<ClassicMatchFrame> classic_goal_flashes(std::vector<ClassicMatchFrame> frames);

// The steps of one board message over `base`, each with its hold time at this speed: the blink loops wait (5 at speed
// 50) + 2 (Caption) or 1 (+1 at speed 50, Result) delay units per step plus 4 units for a step whose lip-sync entry (one
// 0 per vowel of "25AEIOU", padded with 9) is not 0, or the mouth animation below speed 41; after the caption
// delay(80 - speed), after the club name delay(25) at speed 50 / 60, after the final lines delay(45 - 10 * (speed > 49)).
// None at speed >= 70.
std::vector<ClassicMatchFrame> classic_board_message_steps(const ClassicMatchFrame& base, const ClassicBoardMessage& message,
                                                           int speed, bool goal_scenes);

// Inserts the board messages into the observed minutes: per board and minute the penalties (penalty minutes rising by 2
// or 5) and the chances of the home side, then the away side (chances of the regular time from the chance counters,
// goals from the score; the goals of the manager's club name scorer and assist from the squad counters). The steps are
// drawn over the previous minute's screen with the new minute display, like the goal flashes. Nothing at speed >= 70.
// With goal scenes (the Torszenen level 1 or 2) and a data path, every chance of a board first waits delay(30) and
// plays its goal scene (classic_goal_scene_frames), followed by delay(20) on the board, or at level 2
// ("Eisfeld bleibt an") on the rink, which then also stays between the scenes with the minute renewed (no boards,
// messages or goal flashes). The script number n = 2..11 is rolled by the original with the game's random generator
// (the same stream as the match simulation); OpenEM derives it from the minute and the chance.
std::vector<ClassicMatchFrame> classic_board_messages(std::vector<ClassicMatchFrame> frames, int speed, int goal_scenes,
                                                      const std::filesystem::path& data = {});

// Builds the frame of one observed minute (season.cpp / competitions.cpp observer) from the day's matches. Boards are
// chosen as in the game: every match in pair order where a club has reporting mode 2, at most four; the attendance of
// the human home game is passed in (Game::match_attendance). The other matches with a reporting club are listed in
// `reported`.
ClassicMatchFrame classic_match_frame(const Game& game, int minute, const std::vector<LiveMatch>& matches,
                                      long attendance, bool sold_out);

// Whether the original shows the minute display for this day: always when the human club plays, otherwise only
// with the display switch of the competition (Ergebnisse of any league, DEB-Pokal, Europapokal, WM-Spiele). `event` is the
// calendar byte of the day (SeasonCalendar::event).
bool classic_match_display_wanted(const Game& game, bool human_plays, uint8_t event);

}  // namespace em::ui
