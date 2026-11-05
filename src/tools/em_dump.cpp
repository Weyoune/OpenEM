// SPDX-License-Identifier: GPL-3.0-only
// em_dump - inspect original Eishockey Manager files.
//
//   em_dump kb <KB dir>                          one line per .vga file: kind, size, RLE status
//   em_dump vga <file.vga> <out.bmp> [pal.vga]   export an image as 8-bit BMP; the palette comes
//                                                 from the image itself (320x240 screens), from
//                                                 [pal.vga], or a grey ramp
//   em_dump hok <file.hok> [out.bin]             savegame header fields; optionally write the
//                                                 decrypted state for analysis
//   em_dump hok <file.hok> teams|players [team]  team table, or player records (of one team)
//   em_dump hok <file.hok> cup                  DEB cup, European cup and WM pairings
//   em_dump hok <file.hok> squad [manager]      squad (default 0); 5 = national team with caps
//   em_dump hok <file.hok> strength [manager]   stored team values and recomputed line strengths
//   em_dump hok <file.hok> table [league]        league table and next pairings (0 = first
//                                                 division, 1 = second, 2 = Oberliga)
//   em_dump tor <TORE\file>                     goal scene script: frames, sounds, end positions
//   em_dump run <file.hok> <EM.EXE> [days] [seed] [out.hok]  run the day loop for some days
//   em_dump checkpoint <file.hok|file.oem> <EM.EXE> [days] [seed] [out.oem]
//                                                 (game/game.hpp) and optionally save the result
//   em_dump finances <file.hok> <EM.EXE>         monthly income/expenses, accrued interest and adverts
//   em_dump buy <save> <EM.EXE> <slot> <fee DM> <years> <salary DM> <seed> <new.hok|new.oem>
//                                                 [--accept-subsidy], ordinary CPU transfer purchase
//   em_dump insure <save> <EM.EXE> <slot 0..28> <on|off> <seed> <new.hok|new.oem>
//   em_dump contract <save> <EM.EXE> <slot> <years> <salary DM> <seed> <new.hok|new.oem> [--allow-shortening]
//   em_dump market <save> <EM.EXE> <list|recall> <slot> <seed> <new.hok|new.oem>
//   em_dump bank <save> <EM.EXE> <deposit|withdraw> <amount DM> <seed> <new.hok|new.oem>
//   em_dump train <save> <EM.EXE> <day 0..6> <unit 0..3> <kind 0..8> <seed> <new.hok|new.oem>
//   em_dump simulate <file.hok> <EM.EXE> [league] [matchdays] [seed]  play the next league
//                                                 matchdays with the rebuilt engine
//   em_dump rink <KB dir> <out.bmp> [236|242..245|233..235]  goal scene rink (portrait) or a
//                                                 sprite sheet, palette 247
//   em_dump db <KB\23.vga> [club index]          master database: clubs with squad sizes, or one
//                                                 club's player names
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "formats/db.hpp"
#include "formats/hok.hpp"
#include "formats/tore.hpp"
#include "formats/vga.hpp"
#include "game/game.hpp"
#include "game/scorers.hpp"
#include "game/season.hpp"

namespace fs = std::filesystem;
using namespace em;

namespace {

const char* kind_name(VgaKind k) {
    switch (k) {
        case VgaKind::Image: return "image";
        case VgaKind::ImageWithPalette: return "image+pal";
        case VgaKind::Palette: return "palette";
        case VgaKind::Voc: return "voc";
        case VgaKind::Cmf: return "cmf";
        default: return "unknown";
    }
}

void put16(std::ofstream& o, uint16_t v) { o.put(char(v)); o.put(char(v >> 8)); }
void put32(std::ofstream& o, uint32_t v) { put16(o, uint16_t(v)); put16(o, uint16_t(v >> 16)); }

// Minimal 8-bit BMP writer (bottom-up rows padded to 4 bytes).
void write_bmp(const fs::path& path, const Image& img, const Palette& pal) {
    std::ofstream o(path, std::ios::binary);
    if (!o) throw FormatError("cannot write " + path.string());
    uint32_t row = uint32_t(img.width + 3) & ~3u;
    uint32_t data = 14 + 40 + 1024;
    o.write("BM", 2); put32(o, data + row * img.height); put32(o, 0); put32(o, data);
    put32(o, 40); put32(o, img.width); put32(o, img.height); put16(o, 1); put16(o, 8);
    put32(o, 0); put32(o, row * img.height); put32(o, 2835); put32(o, 2835); put32(o, 256); put32(o, 0);
    for (auto& c : pal.rgb) { o.put(char(c[2])); o.put(char(c[1])); o.put(char(c[0])); o.put(0); }
    std::vector<char> line(row, 0);
    for (int y = img.height - 1; y >= 0; y--) {
        std::copy_n(img.pixels.begin() + size_t(y) * img.width, img.width, line.begin());
        o.write(line.data(), row);
    }
}

int cmd_kb(const fs::path& dir) {
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".vga" || e.path().extension() == ".VGA") files.push_back(e.path());
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return std::stoi(a.stem().string()) < std::stoi(b.stem().string());
    });
    int images = 0, complete = 0;
    for (auto& f : files) {
        Bytes d = read_file(f);
        VgaKind k = classify_vga(d);
        std::printf("%-8s %-10s %7zu", f.filename().string().c_str(), kind_name(k), d.size());
        if (k == VgaKind::Image || k == VgaKind::ImageWithPalette) {
            Image img = decode_vga(d);
            images++;
            complete += img.complete;
            std::printf("  %3dx%-3d %s", img.width, img.height, img.complete ? "ok" : "INCOMPLETE");
        }
        std::printf("\n");
    }
    std::printf("%zu files, %d images, %d decoded completely\n", files.size(), images, complete);
    return 0;
}

int cmd_vga(const fs::path& in, const fs::path& out, const fs::path* pal_file) {
    Bytes d = read_file(in);
    Image img = decode_vga(d);
    Palette pal;
    if (classify_vga(d) == VgaKind::ImageWithPalette) {
        pal = decode_palette(d, 6 + le16(d, 4));
    } else if (pal_file) {
        Bytes p = read_file(*pal_file);
        pal = classify_vga(p) == VgaKind::Palette ? decode_palette(p) : decode_palette(p, 6 + le16(p, 4));
    } else {
        for (int i = 0; i < 256; i++) pal.rgb[i] = {uint8_t(i), uint8_t(i), uint8_t(i)};
    }
    write_bmp(out, img, pal);
    std::printf("%s: %dx%d%s -> %s\n", in.filename().string().c_str(), img.width, img.height,
                img.complete ? "" : " (incomplete RLE)", out.string().c_str());
    return 0;
}

int cmd_finances(const fs::path& in, const fs::path& exe) {
    const HokFile h = decode_hok(read_file(in));
    const FinanceTables tables = load_finance_tables(read_file(exe), h);
    const ManagerBudget b = budget_from_save(h, 0);
    std::printf("%02d.%02d.%d %s (raw money = DM x 3)\n", h.date.day, h.date.month, h.date.year, h.club.c_str());
    std::printf("balance %ld raw, bank %ld raw, accrued %ld raw\n", b.balance, b.bank, b.accrued);
    std::printf("income: %ld raw without interest, %ld raw with interest, stored %ld raw\n",
                monthly_income(b, false), monthly_income(b, true), b.stored_income);
    std::printf("expenses: %ld raw without interest/recovery, %ld raw full\n",
                monthly_expenses(tables, b, false, false), monthly_expenses(tables, b, true, true));
    std::printf("team strength sum %d, players %d, squad costs %ld raw, weekly recovery %ld raw\n",
                b.team_strength, b.players, b.squad_costs, b.open_97ee);
    std::printf("event cursor %u, season day %d\n", unsigned(le16(h.state, 0xB953)), h.date.day_index);
    for (size_t i = 0; i < b.adverts.size(); i++) {
        const AdvertContract& a = b.adverts[i];
        if (a.amount || a.months_left || a.flags)
            std::printf("advert %zu: %ld raw / month, %d months left, option/flags 0x%02x\n",
                        i, a.amount, a.months_left, unsigned(a.flags));
    }
    return 0;
}

int cmd_hok_list(const fs::path& in, const std::string& what, int team) {
    HokFile h = decode_hok(read_file(in));
    if (what == "strength") {
        const int manager = team < 0 ? 0 : team;
        if (manager >= kHokManagers) throw FormatError("HOK: invalid strength manager");
        const int club = h.state.at(0x0DCD + size_t(manager) * 0x1AF + 0x14);
        if (club >= int(h.teams.size())) throw FormatError("HOK: manager has no assigned team");
        const HokTeam& stored = h.teams[size_t(club)];
        const auto squad = hok_squad(h, manager);
        const HumanSquad human{&squad, &h.players};
        int computed_total = 0, stored_total = 0;
        std::printf("manager %d, team %d %s, %zu squad slots\n", manager, club, stored.name.c_str(), squad.size());
        for (int kind = 0; kind < 2; kind++) {
            int sum[3] = {};
            for (int line = 0; line < 3; line++) {
                const LineValues v = line_strength(stored, human, line, kind, false, 0);
                std::printf("  %s line %d: %d/%d/%d\n", kind == 0 ? "defence" : "attack", line + 1, v.skill1, v.skill2, v.form);
                sum[0] += v.skill1; sum[1] += v.skill2; sum[2] += v.form;
            }
            std::printf("  stored %d/%d/%d; computed mean %d/%d/%d\n", stored.skill1[kind], stored.skill2[kind], stored.form[kind], sum[0]/3, sum[1]/3, sum[2]/3);
            stored_total += stored.skill1[kind] + stored.skill2[kind];
            computed_total += sum[0]/3 + sum[1]/3;
        }
        std::printf("income strength: stored %d, computed %d, delta %+d\n", stored_total, computed_total, computed_total - stored_total);
        return 0;
    }
    if (what == "table") {
        // Sorted by the stored position, columns as on the in-game "TABELLE Gesamt" screen.
        int league = team < 0 ? 0 : team;
        std::vector<int> order;
        for (int t = league * kHokLeagueSize; t < (league + 1) * kHokLeagueSize; t++) order.push_back(t);
        std::sort(order.begin(), order.end(),
                  [&](int a, int b) { return h.table[size_t(a)].position < h.table[size_t(b)].position; });
        std::printf("next matchday %d:", h.matchday[league]);
        for (const HokPairing& p : h.pairings[size_t(league)])
            std::printf("  %s - %s;", h.teams[size_t(p.home)].name.c_str(), h.teams[size_t(p.away)].name.c_str());
        std::printf("\n");
        for (int t : order) {
            const HokTableRow& r = h.table[size_t(t)];
            std::printf("%2d. %-22s %-8s %2d %2d %2d  %2d:%2d  %3d:%3d\n", r.position + 1,
                        h.teams[size_t(t)].name.c_str(), r.form.c_str(), r.total(r.wins), r.draws(0) + r.draws(1),
                        r.total(r.losses), r.points_plus(), r.points_minus(), r.total(r.goals_for),
                        r.total(r.goals_against));
        }
        return 0;
    }
    if (what == "squad") {
        // Columns of the in-game "Verträge" screen.
        const int manager = team < 0 ? 0 : team;
        const auto squad = hok_squad(h, manager);
        if (manager == 5) {
            const int nation = h.state.at(0x164C);
            std::printf("national team %s, owner %u, phase %u/%u, budget %ld DM, next appointment grant %ld DM\n",
                        nation >= 52 && nation < 64 ? h.teams[size_t(nation)].name.c_str() : "(vacant)",
                        unsigned(h.state.at(0x171C)), unsigned(h.state.at(0x0788)), unsigned(h.state.at(0x0789)),
                        long(int32_t(le32(h.state, 0x17C7))) / 3, national_team_grant(h) / 3);
        }
        static const char* const kStatus[] = {"", "RESERVE", "IM TEAM"};
        std::printf("%-3s %-18s %3s %3s %3s %-8s %3s %5s %8s\n", "ART", "NAME", "SP", "ST", "TO", "STATUS", "E",
                    "JAHRE", "GEHALT");
        for (const HokSquadPlayer& s : squad) {
            if (s.player < 0) continue;  // empty native slots retain their position in hok_squad
            const HokPlayer& p = h.players[size_t(s.player)];
            const char* art = p.position == 0 ? "TOR" : p.position < 60 ? "ABW" : "STU";
            std::printf("%-3s %-18s %3d %3d %3d %-8s %3d %5d %8ld\n", art, p.name.c_str(),
                        manager == 5 ? p.raw[0x19] : s.league_games + s.cup_games,
                        s.strength(), s.league_goals + s.cup_goals, kStatus[int(s.status)], s.exhaustion,
                        s.contract_years, s.salary);
            if (manager == 5)
                std::printf("    native id %d, caps %u, role %d, line 0x%02x\n",
                            int((p.raw - h.state.data() - 0x4FC6) / 33) + 1,
                            unsigned(p.raw[0x19]), s.role, unsigned(s.line));
        }
        return 0;
    }
    if (what == "cup") {
        std::printf("DEB cup round %d, %zu pairings\n", h.cup_round, h.cup_draw.size());
        for (const HokPairing& p : h.cup_draw)
            std::printf("  %-22s - %s\n", h.teams[size_t(p.home)].name.c_str(), h.teams[size_t(p.away)].name.c_str());
        for (size_t g = 0; g < 2; g++) {
            std::printf("European cup group %zu:", g + 1);
            for (const auto& round : h.euro_rounds[g])
                for (const HokPairing& p : round)
                    std::printf(" %s-%s;", h.teams[size_t(p.home)].name.c_str(), h.teams[size_t(p.away)].name.c_str());
            std::printf("\n");
        }
        for (size_t g = 0; g < 2; g++) {
            std::printf("World championship group %zu:", g + 1);
            for (const auto& round : h.wm_rounds[g])
                for (const HokPairing& p : round)
                    std::printf(" %s-%s;", h.teams[size_t(p.home)].name.c_str(), h.teams[size_t(p.away)].name.c_str());
            std::printf("\n");
        }
        return 0;
    }
    if (what == "teams") {
        for (size_t t = 0; t < h.teams.size(); t++) {
            int n = 0;
            for (auto& p : h.players) n += p.team == int(t);
            if (!h.teams[t].name.empty())
                std::printf("%3zu  %-24s %2d player records  strength %3d %3d %3d  effort %2d  offence %d\n", t,
                            h.teams[t].name.c_str(), n, h.teams[t].skill1_mean(), h.teams[t].skill2_mean(),
                            h.teams[t].form_mean(), h.teams[t].effort_base, h.teams[t].offence_base);
        }
        return 0;
    }
    for (auto& p : h.players) {
        if (team >= 0 && p.team != team) continue;
        const char* tn = p.team >= 0 && p.team < int(h.teams.size()) ? h.teams[size_t(p.team)].name.c_str() : "-";
        std::printf("%-18s age %2d  skills %2d/%2d  form %2d  team %3d %s\n", p.name.c_str(), p.age, p.skill_a,
                    p.skill_b, p.form, p.team, tn);
    }
    return 0;
}

int cmd_hok(const fs::path& in, const fs::path* out) {
    HokFile h = decode_hok(read_file(in));
    size_t zeros = 0;
    for (uint8_t b : h.state) zeros += b == 0;
    std::printf("%s: %s, year %d, manager \"%s\", club \"%s\", key0 %02x, state %zu bytes (%zu zero)\n",
                in.filename().string().c_str(), h.version.c_str(), h.year, h.manager.c_str(), h.club.c_str(),
                h.key0, h.state.size(), zeros);
    static const char* const kWeekdays[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                                            "Donnerstag", "Freitag", "Samstag"};
    std::printf("date %s %02d.%02d.%d (season day %d), next matchday %d/%d/%d\n", kWeekdays[h.date.weekday()],
                h.date.day, h.date.month, h.date.year, h.date.day_index, h.matchday[0], h.matchday[1],
                h.matchday[2]);
    const HokFinance& f = h.finance;
    std::printf("balance %ld DM, bank %ld DM (savings > 0, loan < 0), income this month %ld DM\n", f.balance,
                f.bank, f.month_income);
    for (int c = 0; c < kHokShares; c++)
        std::printf("share %d: price %d DM, %u held (bought for %ld DM)\n", c,
                    f.share_price[c][kHokShareHistory - 1], f.shares[c], f.share_cost[c]);
    std::printf("attendance total %ld, record %ld, lowest %ld; power play %d:%d of %d, short-handed %d:%d of %d\n",
                f.attendance_total, f.attendance_record, f.attendance_min, f.pp_goals, f.pp_against,
                f.power_plays, f.sh_goals, f.sh_against, f.penalty_kills);
    if (out) {
        std::ofstream o(*out, std::ios::binary);
        o.write(reinterpret_cast<const char*>(h.state.data()), std::streamsize(h.state.size()));
        std::printf("state -> %s\n", out->string().c_str());
    }
    return 0;
}

int cmd_db(const fs::path& in, int club) {
    Database db = decode_db(read_file(in));
    if (club < 0) {
        for (size_t c = 0; c < db.clubs.size(); c++) {
            int n = 0;
            for (auto& p : db.clubs[c].players) n += !p.name.empty();
            std::printf("%2zu  %-24s %2d players\n", c, db.clubs[c].name.c_str(), n);
        }
        std::printf("%zu European clubs:", db.europe_clubs.size());
        for (auto& e : db.europe_clubs) std::printf(" %s;", e.c_str());
        std::printf("\n");
        return 0;
    }
    if (club >= int(db.clubs.size())) throw FormatError("club index out of range");
    auto& c = db.clubs[size_t(club)];
    std::printf("%s\n", c.name.c_str());
    for (size_t p = 0; p < c.players.size(); p++)
        std::printf("  %2zu  flag %u  %s\n", p, c.players[p].flag, c.players[p].name.c_str());
    return 0;
}

// Goal scene script: header, frame count, sounds and the area the objects cover.
int cmd_tor(const fs::path& in) {
    TorScene s = decode_tor(read_file(in));
    std::printf("%s: %zu frames, camera %d,%d, puck %d,%d\n", in.filename().string().c_str(), s.frames.size(),
                s.camera_x, s.camera_y, s.puck_x, s.puck_y);
    for (size_t i = 0; i < s.frames.size(); i++)
        if (s.frames[i].sound) std::printf("  frame %3zu: sound %d\n", i, s.frames[i].sound);
    TorState end = play_tor(s, s.frames.size());
    std::printf("end: camera %d,%d, puck %d,%d\n", end.camera_x, end.camera_y, end.puck_x, end.puck_y);
    return 0;
}

// Goal scene graphics: the rink (portrait, map 236 or 242..245) or a sprite sheet (233..235)
// side by side, both with palette 247.
int cmd_rink(const fs::path& kb, const fs::path& out, int map) {
    auto f = [&](int n) { return read_file(kb / (std::to_string(n) + ".vga")); };
    Palette pal = decode_palette(f(247));
    Image img;
    if (map >= 233 && map <= 235) {
        std::vector<Image> sp = decode_tor_sprites(f(map));
        img = Image(int(sp.size()) * (kTorSprite + 1), kTorSprite);
        for (size_t s = 0; s < sp.size(); s++)
            for (int y = 0; y < kTorSprite; y++)
                for (int x = 0; x < kTorSprite; x++) img.at(int(s) * (kTorSprite + 1) + x, y) = sp[s].at(x, y);
    } else {
        img = decode_rink(f(231), f(232), f(map));
    }
    write_bmp(out, img, pal);
    std::printf("%dx%d -> %s\n", img.width, img.height, out.string().c_str());
    return 0;
}

// Plays the next league matchdays of a savegame with the rebuilt match engine (the human club with
// the line strengths of its squad, first lines only) and prints results and the new table.
// em_dump run: plays `days` days with the day loop (game/game.hpp) and prints what happened to
// the human club, the month ends and the first division table at the end.
bool action_checkpoint_output(const fs::path& output);

int cmd_run(const fs::path& hok, const fs::path& exe, int days, uint32_t seed, const fs::path& out,
            bool checkpoint = false) {
    if (!out.empty() && action_checkpoint_output(out) != checkpoint)
        throw FormatError(checkpoint ? "Checkpoint output must be .oem" : "Native run output must be .hok");
    Game g(read_file(exe), read_file(hok), seed);
    const fs::path db = exe.parent_path() / "KB" / "23.vga";  // names for the season change
    if (fs::exists(db)) g.set_database(decode_db(read_file(db)));
    for (int i = 0; i < days; i++) {
        g.advance_day();
        if (g.log().empty()) continue;
        const GameDate d = g.date();
        for (const std::string& line : g.log()) std::printf("%02d.%02d.%d  %s\n", d.day, d.month, d.year, line.c_str());
        g.clear_log();
    }
    const League& l = g.league(0);
    std::printf("\nfirst division after day %d:\n", g.season_day());
    for (size_t place = 0; place < l.ranking.size(); place++) {
        const int s = l.ranking[place];
        const HokTableRow& r = l.rows[size_t(s)];
        std::printf("%2d. %-22s %2d games %3d:%-3d %2d:%-2d\n", int(place) + 1, g.save().teams[size_t(l.first_team + s)].name.c_str(),
                    r.total(r.games), r.total(r.goals_for), r.total(r.goals_against), r.points_plus(), r.points_minus());
    }
    std::printf("balance %ld DM, image %d\n", g.budget().balance / 3, g.image());
    if (!out.empty()) {
        const Bytes data = checkpoint ? g.checkpoint_bytes() : g.native_save_bytes();
        std::ofstream o(out, std::ios::binary);
        o.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!o) throw FormatError("Unable to write game save: " + out.string());
        std::printf("saved %s (%zu bytes)\n", out.string().c_str(), data.size());
    }
    return 0;
}

int64_t action_number(const char* text, int64_t low, int64_t high) {
    size_t end = 0;
    const int64_t amount = std::stoll(text, &end);
    if (end != std::string(text).size() || amount < low || amount > high)
        throw FormatError("Invalid numeric action argument: " + std::string(text));
    return amount;
}

long dm_raw(const char* text) {
    const int64_t amount = action_number(text, 1, std::numeric_limits<int32_t>::max() / 3);
    return long(amount * 3);
}

bool action_checkpoint_output(const fs::path& output) {
    if (fs::exists(output)) throw FormatError("Action output already exists: " + output.string());
    std::string extension = output.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (extension != ".hok" && extension != ".oem") throw FormatError("Action output must be .hok or .oem");
    return extension == ".oem";
}

void write_action_save(Game& game, const fs::path& output, bool checkpoint) {
    for (const auto& line : game.log()) std::printf("%s\n", line.c_str());
    const Bytes data = checkpoint ? game.checkpoint_bytes() : game.native_save_bytes();
    std::ofstream file(output, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (!file) throw FormatError("Unable to write action save: " + output.string());
    std::printf("balance %ld DM; saved %s (%zu bytes)\n", game.budget().balance / 3, output.string().c_str(), data.size());
}

int cmd_train(const fs::path& input, const fs::path& exe, int day, int unit, int kind,
              uint32_t seed, const fs::path& output) {
    const bool checkpoint = action_checkpoint_output(output);
    Game game(read_file(exe), read_file(input), seed);
    TrainingPlan plan = game.training_plan();
    plan[size_t(day)][size_t(unit)] = uint8_t(kind);
    if (!game.set_training_plan(plan)) {
        std::printf("training: unavailable phase\n");
        return 2;
    }
    write_action_save(game, output, checkpoint);
    return 0;
}

int cmd_buy(const fs::path& input, const fs::path& exe, int slot, long fee, int years, long salary,
            uint32_t seed, const fs::path& output, bool accept_subsidy) {
    const bool checkpoint = action_checkpoint_output(output);
    Game game(read_file(exe), read_file(input), seed);
    const PurchaseResult result = game.buy_transfer_player(size_t(slot), fee, years, salary, accept_subsidy);
    constexpr const char* names[] = {"invalid request", "unavailable player or phase", "saved negotiation block",
                                    "seller refused", "insufficient account funds", "squad full",
                                    "player refused", "purchased"};
    std::printf("purchase: %s\n", names[size_t(result)]);
    if (result == PurchaseResult::Invalid || result == PurchaseResult::Unavailable || result == PurchaseResult::Blocked)
        return 2;
    write_action_save(game, output, checkpoint);
    return 0;  // a refusal is a valid game action; its ban and RNG belong in the save too
}

int cmd_insure(const fs::path& input, const fs::path& exe, int slot, const std::string& state,
               uint32_t seed, const fs::path& output) {
    if (state != "on" && state != "off") throw FormatError("Insurance state must be on or off");
    const bool checkpoint = action_checkpoint_output(output);
    Game game(read_file(exe), read_file(input), seed);
    if (!game.set_player_insurance(size_t(slot), state == "on")) {
        std::printf("insurance: unavailable or locked\n");
        return 2;
    }
    std::printf("insurance: %s\n", state == "on" ? "enabled" : "cancelled");
    write_action_save(game, output, checkpoint);
    return 0;
}

int cmd_contract(const fs::path& input, const fs::path& exe, int slot, int years, long salary,
                 uint32_t seed, const fs::path& output, bool allow_shortening) {
    const bool checkpoint = action_checkpoint_output(output);
    Game game(read_file(exe), read_file(input), seed);
    const ContractResult result = game.negotiate_contract(size_t(slot), years, salary, allow_shortening);
    constexpr const char* names[] = {"invalid request", "unavailable player or phase", "saved negotiation block",
                                    "shortening needs confirmation", "player refused", "signed"};
    std::printf("contract: %s\n", names[size_t(result)]);
    if (result != ContractResult::Signed && result != ContractResult::PlayerRefused) return 2;
    write_action_save(game, output, checkpoint);
    return 0;
}

int cmd_market(const fs::path& input, const fs::path& exe, const std::string& operation, int slot,
               uint32_t seed, const fs::path& output) {
    if (operation != "list" && operation != "recall") throw FormatError("Market action must be list or recall");
    if (slot > (operation == "list" ? 28 : 10)) throw FormatError("Market slot out of range");
    const bool checkpoint = action_checkpoint_output(output);
    Game game(read_file(exe), read_file(input), seed);
    const auto result = operation == "list" ? game.list_transfer_player(size_t(slot)) : game.recall_transfer_player(size_t(slot));
    constexpr const char* names[] = {"invalid request", "unavailable player or phase", "saved block or pending offer",
                                    "three own players already listed", "market full", "squad full", "moved"};
    std::printf("market: %s\n", names[size_t(result)]);
    if (result != TransferMoveResult::Moved) return 2;
    write_action_save(game, output, checkpoint);
    return 0;
}

int cmd_bank(const fs::path& input, const fs::path& exe, const std::string& operation, long amount,
             uint32_t seed, const fs::path& output) {
    if (operation != "deposit" && operation != "withdraw") throw FormatError("Bank action must be deposit or withdraw");
    const bool checkpoint = action_checkpoint_output(output);
    Game game(read_file(exe), read_file(input), seed);
    const auto result = game.bank_transfer(operation == "deposit" ? amount : -amount);
    constexpr const char* names[] = {"invalid amount", "unavailable phase", "insufficient account funds", "credit limit", "transferred"};
    std::printf("bank: %s\n", names[size_t(result)]);
    if (result != BankResult::Transferred) return 2;
    write_action_save(game, output, checkpoint);
    return 0;
}

int cmd_shares(const fs::path& input,const fs::path& exe,const std::string& operation,int company,
               uint32_t quantity,uint32_t seed,const fs::path& output){
    if(operation!="buy"&&operation!="sell")throw FormatError("Share action must be buy or sell");
    const bool checkpoint=action_checkpoint_output(output);
    Game game(read_file(exe),read_file(input),seed);
    const auto result=game.trade_shares(size_t(company),quantity,operation=="sell");
    constexpr const char* names[]={"invalid","unavailable","insufficient cash","insufficient shares","traded"};
    std::printf("shares: %s\n",names[size_t(result)]);
    if(result!=StockResult::Traded)return 2;
    write_action_save(game,output,checkpoint);return 0;
}

int cmd_tactics_exit(const fs::path& input,const fs::path& exe,uint32_t seed,const fs::path& output){
    const bool checkpoint=action_checkpoint_output(output);
    Game game(read_file(exe),read_file(input),seed);
    if(!game.set_tactics(load_tactics(game.save())))return 2;
    write_action_save(game,output,checkpoint);return 0;
}

int cmd_simulate(const fs::path& hok, const fs::path& exe, int league, int matchdays, uint32_t seed) {
    HokFile h = decode_hok(read_file(hok));
    ScheduleTables tables = load_schedule_tables(read_file(exe));
    League l = league_from_save(h, league);
    GameRandom rng(seed);
    HumanSquad human{&h.squad, &h.players};  // the manager's club plays with its squad
    configure_human_tactics(human, h, 0);
    int human_team = -1;
    for (size_t t = 0; t < h.teams.size(); t++)
        if (h.teams[t].name == h.club) human_team = int(t);
    BorlandRandom scorer_rng(seed);
    std::vector<int> assists(h.squad.size(), 0);
    for (int md = h.matchday[league] - 1; md < h.matchday[league] - 1 + matchdays; md++) {
        auto pairs = round_robin_pairings(tables.teams12, md);
        auto results = play_matchday(l, h.teams, tables, md, rng, &human, human_team);
        std::printf("matchday %d:", md + 1);
        for (size_t k = 0; k < pairs.size(); k++) {
            const int hi = l.first_team + pairs[k].home, ai = l.first_team + pairs[k].away;
            std::printf("  %s - %s %d:%d;", h.teams[size_t(hi)].name.c_str(), h.teams[size_t(ai)].name.c_str(),
                        results[k].home, results[k].away);
            // goals of the human club get a scorer and an assist (ovl14_2349)
            const int own = hi == human_team ? results[k].home : ai == human_team ? results[k].away : 0;
            for (int g = 0; g < own; g++) {
                GoalCredit c = credit_goal(h.squad, h.players, scorer_rng);
                if (c.assist >= 0) assists[size_t(c.assist)]++;
            }
        }
        std::printf("\n");
    }
    if (human_team >= 0) {
        // league scorers of the human club, best first
        std::vector<size_t> idx(h.squad.size());
        for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            return h.squad[a].league_goals + assists[a] > h.squad[b].league_goals + assists[b];
        });
        std::printf("scorers %s (league goals + assists of this run):\n", h.club.c_str());
        for (size_t i : idx)
            if (h.squad[i].league_goals + assists[i] > 0)
                std::printf("  %-18s %2d goals %2d assists\n", h.players[size_t(h.squad[i].player)].name.c_str(),
                            h.squad[i].league_goals, assists[i]);
    }
    std::vector<int> order(kHokLeagueSize);
    for (int s = 0; s < kHokLeagueSize; s++) order[size_t(l.rows[size_t(s)].position)] = s;
    for (int s : order) {
        const HokTableRow& r = l.rows[size_t(s)];
        std::printf("%2d. %-22s %-8s %2d %2d %2d  %2d:%2d  %3d:%3d\n", r.position + 1,
                    h.teams[size_t(l.first_team + s)].name.c_str(), r.form.c_str(), r.total(r.wins),
                    r.draws(0) + r.draws(1), r.total(r.losses), r.points_plus(), r.points_minus(),
                    r.total(r.goals_for), r.total(r.goals_against));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "kb" && argc == 3) return cmd_kb(argv[2]);
        if (cmd == "vga" && (argc == 4 || argc == 5)) {
            fs::path pal = argc == 5 ? fs::path(argv[4]) : fs::path();
            return cmd_vga(argv[2], argv[3], argc == 5 ? &pal : nullptr);
        }
        if (cmd == "hok" && argc >= 4 && (std::string(argv[3]) == "teams" || std::string(argv[3]) == "players" ||
                                       std::string(argv[3]) == "table" || std::string(argv[3]) == "cup" ||
                                       std::string(argv[3]) == "squad" || std::string(argv[3]) == "strength"))
            return cmd_hok_list(argv[2], argv[3], argc == 5 ? std::stoi(argv[4]) : -1);
        if (cmd == "hok" && (argc == 3 || argc == 4)) {
            fs::path out = argc == 4 ? fs::path(argv[3]) : fs::path();
            return cmd_hok(argv[2], argc == 4 ? &out : nullptr);
        }
        if (cmd == "db" && (argc == 3 || argc == 4)) return cmd_db(argv[2], argc == 4 ? std::stoi(argv[3]) : -1);
        if (cmd == "tor" && argc == 3) return cmd_tor(argv[2]);
        if ((cmd == "run" || cmd == "checkpoint") && argc >= 4 && argc <= 7)
            return cmd_run(argv[2], argv[3], argc > 4 ? int(action_number(argv[4], 0, std::numeric_limits<int>::max())) : 30,
                           argc > 5 ? uint32_t(action_number(argv[5], 0, std::numeric_limits<uint32_t>::max())) : 1,
                           argc > 6 ? fs::path(argv[6]) : fs::path(), cmd == "checkpoint");
        if (cmd == "finances" && argc == 4) return cmd_finances(argv[2], argv[3]);
        if (cmd == "buy" && (argc == 10 || (argc == 11 && std::string(argv[10]) == "--accept-subsidy")))
            return cmd_buy(argv[2], argv[3], int(action_number(argv[4], 0, 10)), dm_raw(argv[5]),
                            int(action_number(argv[6], 1, 4)), dm_raw(argv[7]),
                            uint32_t(action_number(argv[8], 0, std::numeric_limits<uint32_t>::max())), argv[9], argc == 11);
        if (cmd == "insure" && argc == 8)
            return cmd_insure(argv[2], argv[3], int(action_number(argv[4], 0, 28)), argv[5],
                              uint32_t(action_number(argv[6], 0, std::numeric_limits<uint32_t>::max())), argv[7]);
        if (cmd == "market" && argc == 8)
            return cmd_market(argv[2], argv[3], argv[4], int(action_number(argv[5], 0, 28)),
                              uint32_t(action_number(argv[6], 0, std::numeric_limits<uint32_t>::max())), argv[7]);
        if (cmd == "bank" && argc == 8)
            return cmd_bank(argv[2], argv[3], argv[4], dm_raw(argv[5]),
                            uint32_t(action_number(argv[6], 0, std::numeric_limits<uint32_t>::max())), argv[7]);
        if(cmd=="shares"&&argc==9)
            return cmd_shares(argv[2],argv[3],argv[4],int(action_number(argv[5],0,3)),
                              uint32_t(action_number(argv[6],1,std::numeric_limits<uint32_t>::max()-1)),
                              uint32_t(action_number(argv[7],0,std::numeric_limits<uint32_t>::max())),argv[8]);
        if(cmd=="tactics-exit"&&argc==6)
            return cmd_tactics_exit(argv[2],argv[3],uint32_t(action_number(argv[4],0,std::numeric_limits<uint32_t>::max())),argv[5]);
        if (cmd == "train" && argc == 9)
            return cmd_train(argv[2], argv[3], int(action_number(argv[4], 0, 6)), int(action_number(argv[5], 0, 3)),
                             int(action_number(argv[6], 0, 8)),
                             uint32_t(action_number(argv[7], 0, std::numeric_limits<uint32_t>::max())), argv[8]);
        if (cmd == "contract" && (argc == 9 || (argc == 10 && std::string(argv[9]) == "--allow-shortening")))
            return cmd_contract(argv[2], argv[3], int(action_number(argv[4], 0, 28)), int(action_number(argv[5], 1, 4)),
                                 dm_raw(argv[6]), uint32_t(action_number(argv[7], 0, std::numeric_limits<uint32_t>::max())),
                                 argv[8], argc == 10);
        if (cmd == "simulate" && argc >= 4)
            return cmd_simulate(argv[2], argv[3], argc > 4 ? std::stoi(argv[4]) : 0, argc > 5 ? std::stoi(argv[5]) : 1,
                                argc > 6 ? uint32_t(std::stoul(argv[6])) : 1);
        if (cmd == "rink" && (argc == 4 || argc == 5)) return cmd_rink(argv[2], argv[3], argc == 5 ? std::stoi(argv[4]) : 236);
        std::fprintf(stderr,
                     "usage: em_dump kb <KB dir> | em_dump vga <file.vga> <out.bmp> [pal.vga] | "
                     "em_dump hok <file.hok> [out.bin] | em_dump finances <file.hok> <EM.EXE> | "
                     "em_dump run|checkpoint <file.hok|file.oem> <EM.EXE> [days] [seed] [out.hok|out.oem] | "
                     "em_dump buy <save> <EM.EXE> <slot 0..10> <fee DM> <years 1..4> <salary DM> <seed> <new.hok|new.oem> [--accept-subsidy] | "
                     "em_dump market <save> <EM.EXE> <list|recall> <slot> <seed> <new.hok|new.oem> | "
                     "em_dump bank <save> <EM.EXE> <deposit|withdraw> <amount DM> <seed> <new.hok|new.oem> | "
                     "em_dump shares <save> <EM.EXE> <buy|sell> <company 0..3> <quantity> <seed> <new.hok|new.oem> | "
                     "em_dump tactics-exit <save> <EM.EXE> <seed> <new.hok|new.oem> | "
                     "em_dump train <save> <EM.EXE> <day 0..6> <unit 0..3> <kind 0..8> <seed> <new.hok|new.oem> | "
                     "em_dump insure <save> <EM.EXE> <slot 0..28> <on|off> <seed> <new.hok|new.oem> | "
                     "em_dump contract <save> <EM.EXE> <slot 0..28> <years 1..4> <salary DM> <seed> <new.hok|new.oem> [--allow-shortening] | "
                     "em_dump db <KB\\23.vga> [club]\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
