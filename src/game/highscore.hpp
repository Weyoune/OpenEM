// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Highscore list (Manager -> Highscore, ovr040:282F..33C4 of EM.EXE).
//
// File HIGH\H.<n> ("high\h." DS:561F + itoa(n)), n = DS:0256 (state 0x06AE; 0 = this game setting keeps no
// highscore). 464 bytes = 8 records of 0x3A bytes (buffer DS:A9A7):
//   +0x00 manager name (26), +0x1A club (23), +0x31..+0x33 championships / DEB cups / European cups,
//   +0x34 i16 score, +0x36 u32 game identity (DS:02C4 = the HOK header identity at 0xB4).
// Opening the page (ovr040:30ED, argument 0) loads the file (a missing file is created empty), merges every
// human manager (ovr040:2CA2 + 2F1D), writes the file back and draws it.
//
// Score of manager m (ovr040:2CA2): base(m) + 8 * championships + 6 * cups + 10 * European cups, at least 1.
// In the WM-only mode (state 0x025A = DS:D0B9 = 1) the club is the national team (state 0x068C) and the score is
// the sum of 9 - place over its WM results (team record DS:8257 + team * 0x50 + 0x34.., until 0).
// base(m) (ovr040:282F), with M = manager block (state 0x0DCD + m * 0x1AF):
//   v  = image(m) * 100                                   (ovl08_2893, stores the image, draws random numbers)
//      + sum_i<9 (DS:00C0[i] / 1000) * stadium level M+0x116+2i
//      + sum_i<2 DS:00E4[i] * places M+0x133+4i / 1000000
//   w  = balance M+0x18F + sum_k<4 shares(m,k) * price(k)
//      - sum_j!=m (negative claims of manager j on m, M_j+0x19B+4m)
//      - sum_j!=m, j<managers or j=4 (negative claims of m on j, M+0x19B+4j)   (ovr039:02E8)
//      + sum of market value / 2 over the first count(m) squad slots           (ovl11_3d36, ovl12_0d27)
//   v += min(w, 50000000) / 2000
//      + 25 * max(0, 36 - (table byte DS:8257+team*0x50+0x31 + DS:07C0[league] + 1))
//      + 150 * each of M+0xE6..+0xE9 that is 1..6
//      + history: over the first min(career year, 50) four-byte rows at M+0x18:
//          (36 - row[0]) * 7 unless row[0] = 0xFF; row[2] bit 7: 80, else (row[2] & 7) * 10;
//          with bit 7 also row[3] bit 7: 130, else (row[3] & 7) * 20;  summed and divided by rows + 1
//   base = v / 20
// Merge (ovr040:2F1D): a record with the same name and club is the manager's slot. At the first record whose
// score is below the new one, that slot (even if it holds a higher score) or else the inserted record (the tail
// moves down) takes the new record and the current game identity; then the eight records are sorted by score.
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "formats/hok.hpp"

namespace em {

constexpr int kHighscoreRows = 8;
constexpr size_t kHighscoreRecord = 0x3A, kHighscoreFile = kHighscoreRows * kHighscoreRecord;  // 464

struct HighscoreRecord {
    std::string name, club;  // native (ISO 646-DE / CP437) bytes
    std::array<uint8_t, 3> titles{};
    int score = 0;
    uint32_t game = 0;
};

struct HighscoreTable {
    std::array<uint8_t, kHighscoreFile> bytes{};
    HighscoreRecord row(int i) const;
    void set_row(int i, const HighscoreRecord& r);
};

// EXE constants the score reads.
struct HighscoreTables {
    std::array<uint32_t, 9> stadium_value{};  // DS:00C0
    std::array<uint32_t, 2> place_value{};    // DS:00E4
    std::array<int8_t, 3> league_offset{};    // DS:07C0
};
HighscoreTables load_highscore_tables(const Bytes& exe);

int highscore_file_number(const HokFile& h);  // DS:0256, 0 = no list for this setting
uint32_t highscore_game_identity(const HokFile& h);  // DS:02C4
std::filesystem::path highscore_file_name(int number);  // HIGH/H.<n>

// ovr040:282F without its two random parts: `image` is the freshly computed image, `squad_values` the market
// values of the first count(m) squad slots. `managers` = number of human managers (DS:0386).
long highscore_base(const HighscoreTables& t, const HokFile& h, int manager, int image,
                    const std::vector<long>& squad_values, int managers);
// ovr040:2CA2: the manager's record (name, club, titles, score); game identity is set by the merge.
HighscoreRecord highscore_record(const HighscoreTables& t, const HokFile& h, int manager, int image,
                                 const std::vector<long>& squad_values, int managers);
// ovr040:2F1D
void merge_highscore(HighscoreTable& table, const HighscoreRecord& record, uint32_t game);

HighscoreTable read_highscore_table(const Bytes& bytes);  // short files are zero-filled like the native buffer

}  // namespace em
