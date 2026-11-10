// SPDX-License-Identifier: GPL-3.0-only
#include "game/new_game.hpp"
#include "game/world_generator.hpp"

#include <algorithm>
#include <system_error>
#include <vector>

namespace em {
namespace {
constexpr size_t kManagers = 0x0DCD, kFaces = 0x01EE, kManagerCount = 0x0DC6;
constexpr size_t kTeams = 0x17E7, kTeamSize = 0x3D;
constexpr size_t kPlayerBase = 0x4FA5, kPlayerSize = 0x21;  // ID n at base + n * 0x21
constexpr size_t kSquad = 0x696D, kSquadSize = 0x34;
constexpr int kSquadSlots = 30, kGermanClubs = 48;
constexpr size_t kPreviewManager = 0x12, kPreviewClub = 0x25, kPreviewText = 19;

void put_text(Bytes& b, size_t at, const std::string& text, size_t field) {
    std::fill_n(b.begin() + long(at), field, uint8_t(0));
    std::copy_n(text.begin(), std::min(text.size(), field - 1), b.begin() + long(at));
}
std::string native_text(const Bytes& b, size_t at, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && b.at(at + i); i++) s.push_back(char(b.at(at + i)));
    return s;
}
}  // namespace

bool is_new_game_base(const HokFile& h) {
    return h.state.size() > kSquad + kSquadSlots * kSquadSize && h.date.year == 1993 && h.date.day_index == 1 &&
           h.state.at(kManagerCount) == 1;
}

std::optional<std::filesystem::path> find_new_game_base(const std::filesystem::path& dir) {
    for (const char* name : {"BACKUP.HOK", "AUTOSAVE.HOK"}) {
        const auto file = dir / name;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) continue;
        try {
            const Bytes bytes = read_file(file);
            verify_hok_checksums(bytes);
            if (is_new_game_base(decode_hok(bytes))) return file;
        } catch (const std::exception&) {
        }
    }
    return std::nullopt;
}

HokFile start_new_game(const HokFile& base, const Database& db, const NewGameSetup& setup) {
    if (!is_new_game_base(base)) throw FormatError("Neues Spiel: kein frischer Startstand");
    if (setup.name.size() < 2 || setup.name.size() > 19) throw FormatError("Neues Spiel: Name mit 2 bis 19 Buchstaben");
    if (setup.face < 0 || setup.face > 11) throw FormatError("Neues Spiel: ungültiges Gesicht");
    if (setup.team < 0 || setup.team >= kGermanClubs) throw FormatError("Neues Spiel: ungültiger Verein");
    HokFile h = clone_hok(base);
    Bytes& s = h.state;
    const int old_team = s.at(kManagers + 0x14);
    const int team = setup.team;
    // club records (ovr027:169B..16BC: club +0x16 = manager + 1; +0x34 bit 7 marks the human club)
    if (old_team != team) {
        uint8_t* was = s.data() + kTeams + size_t(old_team) * kTeamSize;
        uint8_t* now = s.data() + kTeams + size_t(team) * kTeamSize;
        was[0x16] = 0;
        was[0x34] &= 0x7F;
        now[0x16] = 1;
        now[0x34] |= 0x80;
    }
    // manager block 0 and the face table DS:0178
    put_text(s, kManagers, setup.name, 0x13);
    s.at(kManagers + 0x13) = uint8_t(setup.face);
    s.at(kManagers + 0x14) = uint8_t(team);
    s.at(kFaces) = uint8_t(setup.face);
    // ovl16_0730: the squad of manager 0 takes the club's database names in slot order (= ascending IDs)
    std::vector<int> ids;
    for (int slot = 0; slot < kSquadSlots; slot++) {
        const int id = s.at(kSquad + size_t(slot) * kSquadSize + 0x0F);
        if (id > 0 && id < 200) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    const std::string club = native_text(s, kTeams + size_t(team) * kTeamSize, 22);
    const auto entry = std::find_if(db.clubs.begin(), db.clubs.end(), [&](const DbClub& c) { return c.name == h.teams.at(size_t(team)).name; });
    for (size_t k = 0; k < ids.size(); k++) {
        uint8_t* p = s.data() + kPlayerBase + size_t(ids[k]) * kPlayerSize;
        if (entry != db.clubs.end() && k < entry->players.size() && !entry->players[k].name.empty()) {
            std::copy(entry->players[k].copy.begin(), entry->players[k].copy.end(), p);  // sub_19A30(name, 0x14)
            p[0x11] = 0;
        }
        p[0x1D] = uint8_t(team);
    }
    // header preview of manager 0 (file 0x10: league, WM flag, manager[19], club[19])
    put_text(h.header, kPreviewManager, setup.name, kPreviewText);
    put_text(h.header, kPreviewClub, club, kPreviewText);
    return decode_hok(encode_hok(h));
}

HokFile start_new_world(const Bytes& exe, const Bytes& db_raw, const HokFile& base, const std::vector<NewGameSetup>& managers,
                        uint16_t seed) {
    if (!is_new_game_base(base)) throw FormatError("Neues Spiel: kein frischer Startstand");
    if (managers.size() < 2 || managers.size() > 4) throw FormatError("Neues Spiel: zwei bis vier Manager");
    const Bytes& s = base.state;
    WorldSettings settings;
    settings.level = std::clamp(int(s.at(0x28)), 0, 5);       // DS:0118
    settings.end_choice = std::clamp(int(s.at(0x29)), 0, 3);  // DS:0119
    settings.squad_size = std::clamp(int(s.at(0x2A)), 0, 2);  // DS:011A
    const WorldSeeds seeds{seed, seed, seed};
    // The league shuffle runs before the start menu (ovl06_099d), so it does not depend on the managers:
    // a first run with a placeholder manager gives the club order the generated world will have.
    settings.managers = {WorldManager{"x", 0, 0}};
    const HokFile order = generate_world(exe, db_raw, settings, seeds).save;
    settings.managers.clear();
    for (const NewGameSetup& m : managers) {
        if (m.name.size() < 2 || m.name.size() > 19) throw FormatError("Neues Spiel: Name mit 2 bis 19 Buchstaben");
        if (m.team < 0 || m.team >= kGermanClubs) throw FormatError("Neues Spiel: ungültiger Verein");
        const std::string club = native_text(s, kTeams + size_t(m.team) * kTeamSize, 22);
        int team = -1;
        for (int t = 0; t < kGermanClubs && team < 0; t++)
            if (native_text(order.state, kTeams + size_t(t) * kTeamSize, 22) == club) team = t;
        if (team < 0) throw FormatError("Neues Spiel: Verein nicht gefunden");
        // the generator's manager name field holds 18 letters (ovr027:1666 strcpy into 0x13 bytes)
        settings.managers.push_back(WorldManager{m.name.substr(0, 18), m.face, team});
    }
    // youth teams, balances and reporting modes of the managers: generate_world (ovr027:1B27..1DE8)
    return generate_world(exe, db_raw, settings, seeds).save;
}

}  // namespace em
