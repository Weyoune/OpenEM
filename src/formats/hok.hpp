// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// *.HOK - savegame (AUTOSAVE.HOK, BACKUP.HOK, user saves), 47 949 bytes in V1.00.
//
// Layout (worked out from saves and the writer ovl16_11bd; full block map: re/out/hok_layout.csv):
//   0x00  char[10] "KRON_0.99\0"        version tag (also at DGROUP 0x5604)
//   0x0A  i32      calendar year (0x07C9 = 1993; decoder exposes its low16 bits)
//   0x0E  u8       human manager count (1..4)
//   0x0F  u8       first league's next matchday (state8F80)
//   0x10  4x40     previews: league, WM flag, manager[19], club[19]
//   0x12  char[]   manager name, NUL-terminated ("Nobody")
//   0x25  char[]   club of the human manager, NUL-terminated ("MANNHEIMER ERC")
//   0xB0  char[4]  "EH0\0" version marker
//   0xB4  i32      save nonce checked against50 runtime session IDs (not a checksum)
//   0xB8  u8       random filler. Unused preview records in original files contain stack residue.
//   0xB9  u8       key0
//   0xBA  ...      encrypted game state up to the end of the file
// Cipher: the one later used in BMH's .FBM files:
//   plain = ((c - k) ^ k) & 0xFF;  k -= 0x29   (k starts at key0)
// Two check bytes are written raw, outside the key stream (the key does not advance over them,
// they are not part of the state): file offset 0x8F09 (after state 0x8E4E) and the last byte of
// the file. Each is key0 plus the sum of all decrypted state bytes before it, modulo 256 (one
// running sum, DS:E34B in the loader ovl16_1bc3 / block reader ovl16_018e; writer ovl16_0074).
// A mismatch brings up the "Mogelguru" cheat screen and the game hangs. Checked with all
// reference saves.
//
// Decrypted state (indices into HokFile::state), found by comparing the reference saves
// START/TAG1/MONAT1 (work\EM) and matching the record sizes used by EM.EXE:
//   0x03A4          u8[8] European cup groups: 2 x 4 team indices, table order as on the
//                     "Europacup Gruppe" screens; the same 8 bytes again at 0x8E71
//   0x03B8          world championship schedule: 2 groups (0x03B8, 0x03DC) of 5 rounds x 3
//                     pairs of nation indices (52..63), a full round robin; round 1 lists the
//                     group in screen order (Russland, USA, ... / Schweiz ... Tschechei)
//   0x0448          European cup schedule: 2 groups (0x0448, 0x046C) of 6 rounds x {2 pairs,
//                     2 bytes (61, 63; open)}; rounds 4-6 are the return games (4.11.-16.11.)
//   0x0DCD          manager blocks, 6 x 0x1AF bytes = DS:965B in RAM (name, team, league, stadium,
//                     training, balance, debts; see game/finances.hpp)
//   0x17E7  150 x 61 (0x3D) team records, the in-memory array at DS:9657:
//             0-47 German clubs in league order (first division first; 36-47 a regional pool),
//             48-51 placeholders "LEER1..4", 52-63 national teams (DS:06AD = 52),
//             64-149 European clubs. Checked against the "Stärken Bundesliga" and "Taktik"
//             screens of MONAT1 (work\ref\MONAT1_staerken1.png, MONAT1_taktik.png):
//             +0x00 char[22] name; +0x16 u8 manager slot (0 = computer team, 1..6 = human
//               manager; FUN_162c_12da/0737); +0x17 u8 (47..53, open)
//             For each kind k (0 = defence, 1 = attack; FUN_162c_16bf): +0x18+k skill 1,
//               +0x1A+k skill 2, +0x1C+k form; +0x1E+4k+line energy of line 0..3 (90 = fresh,
//               also cached from human players with the native fixed divisor 3).
//               The "Stärken ... Gesamt" screen shows the mean
//               over both kinds of skill 1, skill 2 and form (truncated); its fourth column
//               "Gesamt" is computed elsewhere.
//             +0x26 current effort ("Einsatz", set per score by FUN_162c_12da), +0x27..+0x2B
//               effort for high lead, narrow lead, draw, narrow deficit, high deficit, +0x2D
//               effort base value ("Grundwert")
//             +0x2C flags: bits 4-6 = current offence (base + situation, FUN_162c_12da);
//               +0x2E/+0x30 current defence / forward line on the ice (with a counter in the
//               following byte each, FUN_162c_0737), +0x32 skaters on the ice (5, fewer while penalised, 6 with the goalkeeper pulled;
//               ovl06_34f6), +0x33 two 3-bit penalty timers (ovl06_0000/0151)
//             +0x34 u8 club id (0x80 set for the human club; open)
//             +0x35 offence base value, +0x36..+0x3A offence for the five situations
//             +0x3B/+0x3C open (0/15)
//   0x3BA8  48 x 80 (0x50) league table rows (DS:825A + team * 0x50), indexed like the team
//             records (0-11 first division, 12-23 second division, 24-35 Oberliga, 36-47 unused).
//             The offsets below count from 0x3BAC (= row + 4), as first decoded. Pairs are
//             [home, away]; checked against the in-game tables after matchdays 1 and 9:
//             -0x04 u8[2] points (2 * wins + draws) - the sort ovl13_3855 and the display read
//                   them, the minus points are 2 * games - points; -0x02 u8[2] copy
//             +0x00 char[9] form string of the last 8 games (Sieg/Niederlage/Unentschieden,
//                   lower case = home, upper case = away, newest right, '-' = not played;
//                   manual p. 29), +0x09 char[9] copy (role open)
//             +0x12 u8[2] goals scored, +0x14 u8[2] copy; +0x16 u8[2] goals against, +0x18 copy
//             +0x1A u8[2] games played; +0x22 u8[2] wins (+0x24 open); +0x26 u8[2] losses
//             (+0x28 open); draws = games - wins - losses; points = 2W+D : 2L+D
//             +0x2A u8 table position (0-based); +0x2D u8[22] position after each matchday,
//             4 bits each, low nibble = odd matchday (the "fever curve" on the desk, also read
//             for the club image by ovl10_0a9d); +0x43 u16 assessed league penalty minutes
//             The copies (+0x09, +0x14, +0x18, -0x02, and +0x24 / +0x28 = home wins / home losses
//             followed by 0) equal the main values during the league season in all reference
//             saves; at the season end they differ (play-off phase, open).
//   0x4FC6  199 x 33 (0x21) player records, the in-memory array at DS:8253: national-team
//             players and the stronger club players (the human club has its full squad).
//             +0x00 char[18] name, +0x12 u8 0..9 (open), +0x13 foreigner (1 = DOROCHIN,
//             CHAIDAROV), +0x14 age, +0x15/+0x16 two skill values (rolled 30..89 by ovl16_0730),
//             +0x17 form (45..55 at the start), +0x18 position value (0 goalkeeper, ~30-40
//             defence, ~75-90 forward), +0x19 national caps (all nominated players),
//             +0x1B league/WM goals, +0x1C league games, +0x1D team
//             index (0xFF = no team), +0x1E open, +0x1F u16 assists (ovl14_2349). The skills here are the values from
//             the season start; the squad records below hold the current ones.
//   0x696D  squad records: 6 managers x 30 slots x 52 (0x34) bytes = DS:5DC3 + manager * 0x618,
//             ending exactly at 0x8DFD. Manager block 4 is the pool of players without a human
//             club (12 slots). Each occupied slot references the explicit native player id
//             at +0x0F. Club slots remain occupied while players are called to manager 5's
//             national squad; filtering the player array by current club loses those slots.
//             Checked against the "Verträge" screen of MONAT1
//             (work\ref\MONAT1_vertraege.png), all 25 players, and against ovl11_3d36:
//             +0x00 / +0x01 u8 major (5 min) / other penalties (ovl06_02d8; value bonus 6 * a + b,
//             at most 20), +0x02 home line,
//             +0x03 league/WM goals (goalkeeper: goals against), +0x04 cup goals, +0x05 EuroCup goals,
//             +0x06 league/play-off games, +0x07 cup games, +0x08 EuroCup/WM games (SP = league+cup; the screens
//             read the u16 copies at +0x1C / +0x1E), +0x09 flags
//             (bit 7 with +0x16 > 63: salary * 1.6), +0x0A 0 = no role ("ohne Funktion"),
//             +0x09 bit 0 in the penalty box (+0x0D minutes), bit 1 injured (+0x0D weeks, 0xFF =
//             for good, +0x17 injury type; game/injuries.hpp), +0x0B contract years, +0x0E u8 experience-like value (35..65, counts for value and
//             salary), +0x0F player index + 1 (0 = free slot, ovl12_0d27), +0x10/+0x11 current
//             skills, +0x12 current form, +0x13 exhaustion x 1.5 (E = value * 2 / 3), +0x15 block
//             (1..4, +0x80 for forward lines), +0x16 u8 (open), +0x19 nibbles, +0x1A bit 0x80 =
//             in the team (else reserve), +0x1B fitness, +0x1C/+0x1E/+0x20 u16 game counters
//             (weighted 50/20/30 for value and salary), +0x22 league goals, +0x24 cup goals
//             (TO = both), +0x26 EuroCup goals, +0x28 u32 monthly salary x3, +0x2C u32 market value x3, +0x30 u32 x3
//             monthly injury-insurance premium (optional third contract-dialog row), +0x2E bit 15
//             is the insurance lock cleared every month end. Strength ST = (skill 1 + skill 2 + form) / 3, truncated.
//             Value and salary: game/contracts.hpp.
//   0x8C00  u16 tables, mostly 0 / 0xFFFF (open)
//   0x8DFD  u8[3][12] pairings of the next matchday per league (first division, second division,
//             Oberliga): pairs of team indices (home, away), e.g. START (6, 7) = Freiburg - Köln
//   0x8E33  10 bytes DS:A776, then 0x8E3D 18 bytes DS:A763 (open; differ between saves of the
//             same state)
//   0x8E51  u8[32] DEB cup draw of the current round: team indices 0..35 in pairs (home, away),
//             0xFF ends a shorter list (round 1: 32 teams, 4 have a bye; Achtelfinale: 16).
//             Checked against the "DEB-Pokalspiele" screen of MONAT1 (work\ref\MONAT1_pokal.png)
//   0x8ED1  u8 DEB cup round (1 = first round, 2 = Achtelfinale, ...)
//   0x8EDC  u8[3][12] league table order per league (team indices, leader first)
//   0x8E21  u32 day of month, u32 month (0-based), u32 year, u32 day counter (1 = Sunday
//             10 Sept 1993, the first day of the season; the weekday follows from it)
//   0x8E31  u16 the year the season started (DS:03BB; the screens show it, SAISEND: 1993); the
//             header year at 0x0A is the calendar year of the save (MONAT4, February: 1994)
//   0x8F80  u8[3] next matchday of the first division, second division and Oberliga
//   0x8F88  head-to-head results, 4 managers x 0x280 bytes = DS:AB7B (game/game.cpp,
//             record_head_to_head); the series follow at 0x9988
//   0x9F1C  record opponents, 64 x 8 bytes (DS:BB0F); 0xA11C club records, 64 x 8 u16 (DS:BD0F;
//             game.cpp, update_records)
//   0x0000  i32[6] accrued monthly interest (DS:02A0), see game/finances.cpp
//   0x06C1  three i32 monthly cost components per manager, stride 0x15 (DS:A07E)
//   0xA52C  sponsor offers: 4 managers x 0x1F4 bytes, 5 kinds x 10 records of 10 bytes
//             (DS:C313, game/sponsors.hpp); 0xACFC advertising contracts: 4 x 0x6E bytes,
//             11 records of 10 bytes (DS:C15B, game/finances.cpp)
//   0xB953  u16 original event-calendar cursor (DS:A969, alias 2a9e:4F59, ovl14_2e0f), separate from the displayed
//             day counter at 0x8E2D; Game::save_bytes keeps both on the next day to play
//   0x3BD9  u8[16] WM placements per nation, + team * 0x50 (DS:828B), 1..8 or 11 (group exit)
// Money is kept TIMES 3 in RAM and in the file (shown = stored / 3, truncated), so a memory
// search for the shown amount finds nothing - a cheap anti-cheat measure. Found by diffing the
// guest RAM of the running original around a bank loan, a savings deposit and a share purchase,
// then saving that state (work\EM\AKTIE.HOK) and checking against the "Totale" statistics:
//   0x0030  four managers x32 bytes; four8-byte holdings per manager:
//             {i32 purchase cost x3, u32 quantity} (Herrenhäuser, Playtime,
//             Software2000, Krow). Native7027:1F3A/1F47 uses strides32/8.
//   0x00B0  4 x 36 u16 share price history x3, oldest first (last entry = current price)
//   0x0F40  i32 (open: 66/71/70), then u16 power-play goals scored, power-play goals conceded,
//             short-handed goals scored, short-handed goals conceded, power plays, penalty kills
//   0x0F50  i32 home attendance total, i32 attendance record, i32 lowest attendance (99999 at
//             the start) - plain numbers, not tripled
//   0x0F5C  i32 account balance x3 (1500000 = 500 000 DM at the start)
//   0x0F64  i32 income of the current month x3 (sponsors, interest ...); expenses are not
//             stored, the game sums them from the contracts
//   0x0F78  i32 bank account x3: positive = savings book, negative = loan
//   0x1618  monthly booking list (+x3 / -x3 entries; layout open)
// The master data (club squads by name) is in KB\23.vga (plain).
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/binary.hpp"

namespace em {

// Situations of the tactics screen, in record order.
enum TacticSituation { kHighLead, kNarrowLead, kDraw, kNarrowDeficit, kHighDeficit, kTacticSituations };

struct HokTeam {
    std::string name;  // UTF-8, empty for unused slots
    // [0] = defence, [1] = attack
    int skill1[2] = {}, skill2[2] = {}, form[2] = {};
    int energy[2][4] = {};  // per kind and line 0..3
    int bonus = 0;           // +0x17, adds (bonus - 44) / 2 to every playing strength
    int effort_current = 0;  // +0x26
    int flags = 0;           // +0x2C: bits 4-6 current offence, bits 0-2 tactics mode
    int current_line[2] = {};  // +0x2E / +0x30: defence / forward line on the ice (0..3)
    int shift[2] = {};         // +0x2F / +0x31: minutes of the current shift
    int skaters = 0;      // +0x32: skaters on the ice (5; fewer while penalised, 6 with the goalkeeper
                          // pulled - ovl06_34f6); fewer skaters than the opponent divide the goals by 4
    int penalty_code = 0;  // +0x33: running penalties (low nibble first: minutes left | 8 for a major, high nibble second)
    int effort_base = 0, offence_base = 0;
    int effort[kTacticSituations] = {}, offence[kTacticSituations] = {};
    const uint8_t* raw = nullptr;  // 61 bytes inside HokFile::state

    // Columns 1-3 of the "Stärken ... Gesamt" screen
    int skill1_mean() const { return (skill1[0] + skill1[1]) / 2; }
    int skill2_mean() const { return (skill2[0] + skill2[1]) / 2; }
    int form_mean() const { return (form[0] + form[1]) / 2; }
};

struct HokPlayer {
    std::string name;  // UTF-8
    int age = 0;
    int skill_a = 0;   // +0x15
    int skill_b = 0;   // +0x16
    int form = 0;      // +0x17
    int position = 0;  // +0x18: 0 goalkeeper, low defence, high forward
    bool foreigner = false;  // +0x13
    int league_goals = 0, league_games = 0;  // +0x1B, +0x1C
    int team = -1;     // index into HokFile::teams, -1 = none
    const uint8_t* raw = nullptr;  // 33 bytes inside HokFile::state
};

// One row of a league table; index 0 = home games, 1 = away games.
struct HokTableRow {
    std::string form;      // last 8 results, e.g. "SssSssNS"
    int goals_for[2] = {};
    int goals_against[2] = {};
    int games[2] = {};
    int wins[2] = {};
    int losses[2] = {};
    int position = 0;      // 0-based table position
    int penalty_minutes = 0;  // u16 at row +0x43 (DS:82A1), league penalties only
    int position_history[44] = {};  // position after each matchday 1..44 (0-based)

    int draws(int side) const { return games[side] - wins[side] - losses[side]; }
    int total(const int (&v)[2]) const { return v[0] + v[1]; }
    int points_plus() const { return 2 * total(wins) + draws(0) + draws(1); }
    int points_minus() const { return 2 * total(losses) + draws(0) + draws(1); }
};

struct HokDate {
    int day = 0;        // 1..31
    int month = 0;      // 1..12
    int year = 0;
    int day_index = 0;  // 1 = first day of the season (a Sunday)
    int weekday() const { return ((day_index - 1) % 7 + 7) % 7; }  // 0 = Sunday
};

struct HokPairing {
    int home = 0, away = 0;  // team indices
};

constexpr int kHokShares = 4;        // listed companies
constexpr int kHokShareHistory = 36;  // price history entries per company

// Finances and season statistics of the human manager; all amounts in DM (already divided by 3).
struct HokFinance {
    long balance = 0;          // account balance
    long bank = 0;             // savings book (> 0) or bank loan (< 0)
    long month_income = 0;     // monthly income as last computed by ovl08_16b7 (game/finances)
    long share_cost[kHokShares] = {};  // purchase price of the held shares
    uint32_t shares[kHokShares] = {};  // native unsigned32 quantity
    int share_price[kHokShares][kHokShareHistory] = {};  // oldest first, last = current price
    long attendance_total = 0;  // home attendance, summed
    long attendance_record = 0;
    long attendance_min = 0;    // 99999 before the first home game
    int pp_goals = 0, pp_against = 0, sh_goals = 0, sh_against = 0;  // power play / short-handed
    int power_plays = 0, penalty_kills = 0;
};

constexpr int kHokTeams = 150;
constexpr int kHokPlayers = 199;
constexpr int kHokLeagueTeams = 48;  // table rows (4 x 12, the last 12 unused)
constexpr int kHokLeagueSize = 12;
constexpr int kHokManagers = 6, kHokSquadSlots = 30;

enum class SquadStatus { None, Reserve, Team };

// Contract and match data of a club player under a manager (squad record, see above).
struct HokSquadPlayer {
    int player = -1;  // index into HokFile::players
    int major_penalties = 0, minor_penalties = 0;  // squad +0x00/+0x01, native byte counters
    SquadStatus status = SquadStatus::None;
    int line = 0;            // block 1..4 (0 = none), forwards have bit 0x80
    int skill_a = 0, skill_b = 0, form = 0;
    int exhaustion = 0;      // E on the screens, 0..~100
    int raw_exhaustion = 0;  // exact squad +0x13, retained for match preparation and minute updates
    int contract_years = 0;
    long salary = 0;         // DM per month
    int league_games = 0, cup_games = 0, league_goals = 0, cup_goals = 0, other_goals = 0;
    int goals_against = 0;   // goalkeepers (+0x03)
    int league_goals_byte = 0;  // +0x03 for skaters: league goals, weighs in ovl06_3191 (= goals_against)
    int cup_goals_byte = 0;     // +0x04, native DEB cup scorer byte
    int other_goals_byte = 0;   // +0x05, EuroCup scorer byte
    int home_line = 0;       // +0x02: usual line (1..4), playing elsewhere costs strength
    int nibbles = 0;         // +0x19: high nibble attack bonus, low nibble defence value
    int fitness = 0;         // +0x1B: player energy (90 fresh); strength factor (energy / 2 + 50) %
    int role = 0;            // +0x0A: 0 = no role
    int strength() const { return (skill_a + skill_b + form) / 3; }
};

struct HokFile {
    std::string version;   // "KRON_0.99"
    int year = 0;          // season start year
    int season_year = 0;   // state 0x8E31 = DS:03BB: the year the screens show; stays at the old
                           // year until the season change (SAISEND: header 1994, this 1993)
    std::string manager;   // manager name (ISO 646-DE decoded to UTF-8)
    std::string club;      // human manager's club
    uint8_t key0 = 0;
    Bytes header;          // plain bytes 0x00..0xB8
    Bytes state;           // decrypted game state (file offsets 0xBA.., without the extra byte)
    std::vector<HokTeam> teams;      // 150 team records (raw pointers into `state`)
    std::vector<HokPlayer> players;  // named player records (raw pointers into `state`)
    std::vector<HokTableRow> table;  // 48 league table rows, same index as `teams`
    HokDate date;
    int matchday[3] = {};  // next matchday: first division, second division, Oberliga
    std::array<std::array<HokPairing, kHokLeagueSize / 2>, 3> pairings{};  // next matchday per league
    std::array<std::array<int, kHokLeagueSize>, 3> table_order{};          // leader first
    int cup_round = 0;
    std::vector<HokPairing> cup_draw;  // DEB cup pairings of the current round
    std::array<std::array<int, 4>, 2> euro_groups{};                          // team indices
    std::array<std::array<std::array<HokPairing, 2>, 6>, 2> euro_rounds{};    // [group][round]
    std::array<std::array<std::array<HokPairing, 3>, 5>, 2> wm_rounds{};      // [group][round]
    HokFinance finance;
    std::vector<HokSquadPlayer> squad;  // human manager (manager 0)

    HokFile() = default;
    HokFile(const HokFile&) = delete;  // raw pointers refer to this object's `state`
    HokFile& operator=(const HokFile&) = delete;
    HokFile(HokFile&&) = default;      // vector moves keep the buffer
    HokFile& operator=(HokFile&&) = default;
};

constexpr size_t kHokHeaderSize = 0xB9;
constexpr size_t kHokCheckByte = 0x8F09;  // first check byte (file offset); the second is the last byte

HokFile decode_hok(const Bytes& data);
// Explicit validation for user loads. decode_hok also serves forensic tools
// that need to inspect damaged files without accepting them into a session.
void verify_hok_checksums(const Bytes& data);

// Explicit deep snapshot, rebasing all record pointers into its own state.
// Ordinary copying remains disabled to prevent accidentally dangling views.
HokFile clone_hok(const HokFile& source);

// Decode the explicit native player ids of any squad block (0..5). Club players
// can temporarily belong to the national team without leaving their club squad.
std::vector<HokSquadPlayer> hok_squad(const HokFile& h, int manager);

// Writes the decoded fields back into HokFile::state (teams, players, table rows, date,
// matchdays, next pairings, table order, cup round and draw, squad records of manager 0). Fields
// that are not exact in the structs stay untouched: names, the foreigner flag, the table-row
// copies at +0x09/+0x14/+0x18, squad salary (kept in DM), exhaustion (scaled), status, and all
// money of HokFinance (scaled) - write those into `state` directly. Team and player records are
// written at the position their `raw` pointer names.
void store_hok(HokFile& h);

// Builds the file: plain header, key0, the state encrypted with the BMH cipher (key0, k -= 0x29)
// and the two check bytes computed anew. decode_hok(encode_hok(h)) gives `h` again; for an unchanged
// file the bytes are identical (checked with all reference saves).
Bytes encode_hok(const HokFile& h);

// Maps a file offset of the encrypted area to its index in HokFile::state (or SIZE_MAX for the
// header, the key byte and the check bytes).
size_t hok_state_index(size_t file_offset);

}  // namespace em
