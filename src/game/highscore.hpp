// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Highscore list (Manager -> Highscore): the score of a manager, the highscore file and how a
// manager's record is merged into it.
//
// File HIGH\H.<n>, n = state 0x06AE (i16; 0 = this game setting keeps no highscore).
// 464 bytes = 8 records of 0x3A bytes:
//   +0x00 manager name (26), +0x1A club (23), +0x31..+0x33 championships / DEB cups / European cups,
//   +0x34 i16 score, +0x36 u32 game identity (the HOK header identity at 0xB4).
// Opening the page loads the file (a missing file is created empty), merges every human
// manager, writes the file back and draws it.
//
// Score of manager m: base(m) + 8 * championships + 6 * cups + 10 * European cups, at least 1.
// In the WM-only mode (state 0x025A = 1) the club is the national team (state 0x068C) and the score is
// the sum of 9 - place over its WM results (team record bytes +0x34.., until 0).
// base(m), with M = manager block (state 0x0DCD + m * 0x1AF):
//   v  = image(m) * 100                                   (computing the image draws random numbers)
//      + sum_i<9 (stadium_value[i] / 1000) * stadium level M+0x116+2i
//      + sum_i<2 place_value[i] * places M+0x133+4i / 1000000
//   w  = balance M+0x18F + sum_k<4 shares(m,k) * price(k)
//      - sum_j!=m (negative claims of manager j on m, M_j+0x19B+4m)
//      - sum_j!=m, j<managers or j=4 (negative claims of m on j, M+0x19B+4j)
//      + sum of market value / 2 over the occupied squad slots (the value draws random numbers)
//   v += min(w, 50000000) / 2000
//      + 25 * max(0, 36 - (table place, team record +0x31, + league_offset[league] + 1))
//      + 150 * each of M+0xE6..+0xE9 that is 1..6
//      + history: over the first min(career year, 50) four-byte rows at M+0x18:
//          (36 - row[0]) * 7 unless row[0] = 0xFF; row[2] bit 7: 80, else (row[2] & 7) * 10;
//          with bit 7 also row[3] bit 7: 130, else (row[3] & 7) * 20;  summed and divided by rows + 1
//   base = v / 20
// Merge: a record with the same name and club is the manager's slot. At the first record whose
// score is below the new one, that slot (even if it holds a higher score) or else the inserted record (the tail
// moves down) takes the new record and the current game identity; then the eight records are sorted by score.
// A manager whose score beats nobody is not entered at all.
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "formats/hok.hpp"

namespace em {

constexpr int kHighscoreRows = 8;  // records in a highscore file
constexpr size_t kHighscoreRecord = 0x3A, kHighscoreFile = kHighscoreRows * kHighscoreRecord;  // 464

// One decoded highscore record.
struct HighscoreRecord {
    std::string name, club;  // original game bytes (ISO 646-DE / CP437), not UTF-8
    std::array<uint8_t, 3> titles{};  // championships, DEB cups, European cups
    int score = 0;                    // i16 in the file
    uint32_t game = 0;                // identity of the game that wrote the record
};

// The raw highscore file; rows are decoded and encoded on access.
struct HighscoreTable {
    std::array<uint8_t, kHighscoreFile> bytes{};
    HighscoreRecord row(int i) const;               // decodes record i (0..7)
    void set_row(int i, const HighscoreRecord& r);  // encodes record i; name/club are cut to 25/22 bytes
};

// EM.EXE constants the score reads.
struct HighscoreTables {
    std::array<uint32_t, 9> stadium_value{};  // value per level of each stadium item
    std::array<uint32_t, 2> place_value{};    // value per spectator place of each kind
    std::array<int8_t, 3> league_offset{};    // per league, added to the table place
};
HighscoreTables load_highscore_tables(const Bytes& exe);

int highscore_file_number(const HokFile& h);  // state 0x06AE; 0 = no list for this setting
uint32_t highscore_game_identity(const HokFile& h);  // HOK header 0xB4
std::filesystem::path highscore_file_name(int number);  // HIGH/H.<n>, extension cut to three characters as by DOS

// base(m) of the file comment without its two random parts: `image` is the freshly computed
// image, `squad_values` the market values of the occupied squad slots. `managers` = number of
// human managers.
long highscore_base(const HighscoreTables& t, const HokFile& h, int manager, int image,
                    const std::vector<long>& squad_values, int managers);
// The manager's record (name, club, titles, score, at least 1); the game identity is set by the merge.
HighscoreRecord highscore_record(const HighscoreTables& t, const HokFile& h, int manager, int image,
                                 const std::vector<long>& squad_values, int managers);
// Merges `record` into `table` as described in the file comment and stamps it with `game`.
void merge_highscore(HighscoreTable& table, const HighscoreRecord& record, uint32_t game);

HighscoreTable read_highscore_table(const Bytes& bytes);  // short files are zero-filled, as the original's buffer is

}  // namespace em
