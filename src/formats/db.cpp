// SPDX-License-Identifier: GPL-3.0-only
#include "formats/db.hpp"

#include <algorithm>

namespace em {

namespace {
constexpr size_t kClubs = 0x4AD;
constexpr size_t kClubRecord = 0x20B;
constexpr size_t kClubName = 23;     // flag + 22 characters
constexpr size_t kPlayerRecord = 20;  // flag + 19 characters
constexpr size_t kEuropeRecord = 23;

std::string text(const Bytes& d, size_t off, size_t len) {
    std::string raw;
    for (size_t i = off; i < off + len && d[i] != 0; i++) raw += char(d[i]);
    return iso646de_to_utf8(raw.c_str());
}

// The European club names follow the 64 entries, one byte later (0x876E).
constexpr size_t kEurope = kClubs + kDbClubs * kClubRecord + 1;
}  // namespace

Database decode_db(const Bytes& d) {
    if (d.size() != kDbSize) throw FormatError("DB: KB\\23.vga must be 36648 bytes");
    Database db;
    for (int c = 0; c < kDbClubs; c++) {
        size_t base = kClubs + size_t(c) * kClubRecord;
        DbClub club;
        club.flag = d[base];
        club.name = text(d, base + 1, kClubName - 1);
        for (int p = 0; p < kDbSquad; p++) {
            size_t o = base + kClubName + size_t(p) * kPlayerRecord;
            DbPlayer pl;
            pl.flag = d[o];
            pl.name = text(d, o + 1, kPlayerRecord - 1);
            std::copy(d.begin() + long(o + 1), d.begin() + long(o + 1 + pl.copy.size()), pl.copy.begin());
            club.players.push_back(pl);
        }
        db.clubs.push_back(std::move(club));
    }
    for (size_t o = kEurope; o + kEuropeRecord <= d.size(); o += kEuropeRecord) {
        std::string n = text(d, o, kEuropeRecord);
        if (!n.empty()) db.europe_clubs.push_back(n);
    }
    return db;
}

}  // namespace em
