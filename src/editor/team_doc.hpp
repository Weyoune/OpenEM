// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Document logic of the team editor for KB\23.vga: club and player names, foreigner flags, crest, league place,
// jersey colour and strength values of the 64 teams (48 clubs, LEER1-4, 12 national teams).
// The file layout is described in src/formats/db.hpp (names, club records) and src/formats/teamdata.hpp (crest,
// place, jersey and strength tables in front of the club records). TeamDoc keeps the raw file and rewrites only the
// fields it edits, so every other byte of KB\23.vga survives a save unchanged.
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/db.hpp"
#include "formats/teamdata.hpp"

namespace em::editor {

// The editable team data of KB\23.vga.
class TeamDoc {
public:
    static constexpr int kTeams = 64, kSquad = 25;  // teams with a player list, players per team
    static constexpr int kEuropeFirst = 64, kAllTeams = 150;  // teams 64..149: European club names, with jersey and strengths only
    static constexpr int kNameLimit = 19, kClubLimit = 22;    // maximum characters of a player / club name
    // `data` is the game directory; the file is <data>/KB/23.vga.
    explicit TeamDoc(const std::filesystem::path& data) : file_(resolve_case(data / "KB" / "23.vga")) {}

    bool load();    // reads and decodes KB\23.vga; false (message in status()) on error
    bool save();    // writes KB\23.vga, first time with a .bak copy
    void revert();  // reload from disk
    bool dirty() const { return dirty_; }                 // unsaved changes exist
    unsigned revision() const { return revision_; }       // bumped on every change
    const std::string& status() const { return status_; }  // last status or error message (German, shown in the UI)

    // Teams 48..51 are the empty LEER1..LEER4 entries; the editor does not offer them
    static bool editable(int team) { return team >= 0 && team < kAllTeams && (team < 48 || team > 51); }
    static bool has_players(int team) { return team >= 0 && team < kTeams; }  // teams 0..63 have a player list

    const std::string& club_name(int t) const { return clubs_[size_t(t)]; }          // UTF-8
    const std::string& player_name(int t, int p) const { return names_[size_t(t)][size_t(p)]; }  // UTF-8
    bool foreign(int t, int p) const { return foreign_[size_t(t)][size_t(p)] != 0; }  // player counts as a foreigner
    const TeamData& data(int t) const { return td_[size_t(t)]; }  // crest, place, jersey, strengths

    // Setters return false (and set the status) when the value cannot be stored; names are upper-cased.
    // Names must fit the length limits and the game's character set (ASCII plus German umlauts and sharp s).
    bool set_club_name(int t, const std::string& utf8);
    bool set_player_name(int t, int p, const std::string& utf8);
    void set_foreign(int t, int p, bool on);
    // The numeric setters clamp to the given range and ignore teams without that field.
    void set_crest(int t, int crest);             // 0..63
    void set_place(int t, int place);             // 0..12
    void set_jersey(int t, int r, int g, int b);  // 0..15 per channel
    void set_strength(int t, int k, int value);   // k 0..2, 1..99
    // The whole team (names, players, flags, crest, place, jersey, strengths) changes position. Only two editable
    // teams of the same kind (both with or both without player list) can be swapped.
    void swap_teams(int a, int b);

    // Names used more than once (players and clubs), for the warning before saving
    std::vector<std::string> duplicate_names() const;

    // Splits the 12-bit jersey colour (0x0RGB) into its 4-bit channels.
    static std::array<int, 3> jersey_rgb(const TeamData& d) { return {(d.jersey >> 8) & 15, (d.jersey >> 4) & 15, d.jersey & 15}; }

private:
    std::filesystem::path file_;
    Bytes raw_;                  // the file as last loaded or saved
    Database db_;                // decoded names of raw_
    std::vector<TeamData> td_;   // per-team tables (edited copy)
    std::array<std::string, kAllTeams> clubs_;                   // edited club names
    std::array<std::array<std::string, kSquad>, kTeams> names_;  // edited player names
    std::array<std::array<uint8_t, kSquad>, kTeams> foreign_{};  // edited foreigner bytes (0 = German)
    bool dirty_ = false;
    unsigned revision_ = 0;
    std::string status_;

    void touch() {  // marks a change
        dirty_ = true;
        revision_++;
    }
};

}  // namespace em::editor
