// SPDX-License-Identifier: GPL-3.0-only
#include "formats/hok.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace em {

namespace {
constexpr char kVersionTag[] = "KRON_0.99";
constexpr size_t kManagerName = 0x12;
constexpr size_t kClubName = 0x25;
constexpr size_t kStateStart = kHokHeaderSize + 1;  // 0xBA, right after key0
constexpr size_t kTeams = 0x17E7, kTeamRecord = 0x3D, kTeamName = 23;
constexpr size_t kPlayers = 0x4FC6, kPlayerRecord = 0x21, kPlayerName = 18;
constexpr size_t kTable = 0x3BAC, kTableRecord = 0x50;
constexpr size_t kDate = 0x8E21, kSeasonYear = 0x8E31, kMatchday = 0x8F80;
constexpr size_t kEuroGroups = 0x03A4, kWmRounds = 0x03B8, kEuroRounds = 0x0448;  // groups 36 bytes apart
constexpr size_t kEuroGroupsCopy = 0x8E71;
constexpr size_t kPairings = 0x8DFD, kCupDraw = 0x8E51, kCupDrawMax = 32, kCupRound = 0x8ED1, kTableOrder = 0x8EDC;
constexpr size_t kHoldings = 0x30, kSharePrices = 0xB0, kManagerStats = 0x0F40;
constexpr long kMoneyFactor = 3;  // EM keeps every amount tripled
constexpr size_t kSquad = 0x696D, kSquadRecord = 0x34;

long money(const Bytes& s, size_t off) { return long(int32_t(le32(s, off))) / kMoneyFactor; }

std::string field(const Bytes& d, size_t off, size_t max_len) {
    std::string raw;
    for (size_t i = off; i < off + max_len && i < d.size() && d[i] != 0; i++) raw += char(d[i]);
    return iso646de_to_utf8(raw.c_str());
}
}  // namespace

HokFile clone_hok(const HokFile& source) {
    HokFile copy;
    copy.version = source.version; copy.year = source.year; copy.season_year = source.season_year;
    copy.manager = source.manager; copy.club = source.club; copy.key0 = source.key0;
    copy.header = source.header; copy.state = source.state;
    copy.teams = source.teams; copy.players = source.players; copy.table = source.table;
    for (size_t i = 0; i < copy.teams.size(); i++)
        if (source.teams[i].raw) copy.teams[i].raw = copy.state.data() + (source.teams[i].raw - source.state.data());
    for (size_t i = 0; i < copy.players.size(); i++)
        if (source.players[i].raw) copy.players[i].raw = copy.state.data() + (source.players[i].raw - source.state.data());
    copy.date = source.date;
    std::copy(std::begin(source.matchday), std::end(source.matchday), std::begin(copy.matchday));
    copy.pairings = source.pairings; copy.table_order = source.table_order;
    copy.cup_round = source.cup_round; copy.cup_draw = source.cup_draw;
    copy.euro_groups = source.euro_groups; copy.euro_rounds = source.euro_rounds;
    copy.wm_rounds = source.wm_rounds; copy.finance = source.finance; copy.squad = source.squad;
    return copy;
}

std::vector<HokSquadPlayer> hok_squad(const HokFile& h, int manager) {
    if (manager < 0 || manager >= kHokManagers) throw FormatError("HOK: invalid squad manager");
    const size_t base = kSquad + size_t(manager) * 0x618;
    size_t slots = 0;
    for (size_t slot = 0; slot < kHokSquadSlots; slot++)
        if (h.state.at(base + slot * kSquadRecord + 0x0F)) slots = slot + 1;
    std::vector<HokSquadPlayer> squad;
    for (size_t slot = 0; slot < slots; slot++) {
        const uint8_t* r = h.state.data() + base + slot * kSquadRecord;
        HokSquadPlayer sp;
        const int id = r[0x0F];
        if (id == 0) { squad.push_back(sp); continue; }
        if (id > 0)
            for (size_t p = 0; p < h.players.size(); p++)
                if (h.players[p].raw == h.state.data() + kPlayers + size_t(id - 1) * kPlayerRecord) {
                    sp.player = int(p);
                    break;
                }
        if (sp.player < 0) throw FormatError("HOK: squad references a missing player");
        sp.major_penalties = r[0x00];
        sp.minor_penalties = r[0x01];
        sp.status = r[0x0A] == 0 ? SquadStatus::None : (r[0x1A] & 0x80) ? SquadStatus::Team : SquadStatus::Reserve;
        sp.line = r[0x15];
        sp.skill_a = r[0x10];
        sp.skill_b = r[0x11];
        sp.form = r[0x12];
        sp.exhaustion = r[0x13] * 2 / 3;
        sp.raw_exhaustion = r[0x13];
        sp.contract_years = r[0x0B];
        sp.salary = money(h.state, base + slot * kSquadRecord + 0x28);
        sp.league_games = r[0x06];
        sp.cup_games = r[0x07];
        sp.league_goals = r[0x22];
        sp.cup_goals = r[0x24];
        sp.other_goals = r[0x26];
        sp.goals_against = r[0x03];
        sp.league_goals_byte = r[0x03];
        sp.cup_goals_byte = r[0x04];
        sp.other_goals_byte = r[0x05];
        sp.home_line = r[0x02];
        sp.nibbles = r[0x19];
        sp.fitness = r[0x1B];
        sp.role = r[0x0A];
        squad.push_back(sp);
    }
    return squad;
}
void verify_hok_checksums(const Bytes& d){
    if(d.size()<=kHokCheckByte+1)throw FormatError("HOK: state too short for native check bytes");
    uint8_t key=d[kHokHeaderSize],sum=key;
    for(size_t at=kStateStart;at<d.size();++at){
        if(at==kHokCheckByte||at+1==d.size()){
            if(d[at]!=sum)throw FormatError("Beschädigter Spielstand: Prüfsumme stimmt nicht.");
        }else{
            const uint8_t plain=uint8_t(uint8_t(d[at]-key)^key);
            sum=uint8_t(sum+plain);key=uint8_t(key-0x29);
        }
    }
}
HokFile decode_hok(const Bytes& d) {
    if (d.size() <= kHokCheckByte + 1 || std::memcmp(d.data(), kVersionTag, sizeof kVersionTag) != 0)
        throw FormatError("HOK: no KRON_0.99 savegame");
    HokFile h;
    h.version = kVersionTag;
    h.year = le16(d, 0x0A);
    h.manager = field(d, kManagerName, kClubName - kManagerName);
    h.club = field(d, kClubName, 19);
    h.header.assign(d.begin(), d.begin() + kHokHeaderSize);
    h.key0 = d[kHokHeaderSize];

    h.state.reserve(d.size() - kStateStart - 2);
    uint8_t k = h.key0;
    for (size_t o = kStateStart; o + 1 < d.size(); o++) {  // the last byte is the second check byte
        if (o == kHokCheckByte) continue;  // raw check byte, outside the key stream (hok.hpp)
        uint8_t c = d[o];
        h.state.push_back(uint8_t(uint8_t(c - k) ^ k));
        k = uint8_t(k - 0x29);
    }

    if (h.state.size() < kPlayers + kHokPlayers * kPlayerRecord) throw FormatError("HOK: state too short");
    for (int t = 0; t < kHokTeams; t++) {
        size_t o = kTeams + size_t(t) * kTeamRecord;
        const uint8_t* r = h.state.data() + o;
        HokTeam team{};
        team.name = field(h.state, o, kTeamName);
        for (int kind = 0; kind < 2; kind++) {
            team.skill1[kind] = r[0x18 + kind];
            team.skill2[kind] = r[0x1A + kind];
            team.form[kind] = r[0x1C + kind];
            for (int line = 0; line < 4; line++) team.energy[kind][line] = r[0x1E + 4 * kind + line];
        }
        team.bonus = r[0x17];
        team.effort_current = r[0x26];
        team.flags = r[0x2C];
        for (int kind = 0; kind < 2; kind++) {
            team.current_line[kind] = r[0x2E + 2 * kind];
            team.shift[kind] = r[0x2F + 2 * kind];
        }
        team.skaters = r[0x32];
        team.penalty_code = r[0x33];
        team.effort_base = r[0x2D];
        team.offence_base = r[0x35];
        for (int s = 0; s < kTacticSituations; s++) {
            team.effort[s] = r[0x27 + s];
            team.offence[s] = r[0x36 + s];
        }
        team.raw = r;
        h.teams.push_back(std::move(team));
    }
    for (int p = 0; p < kHokPlayers; p++) {
        size_t o = kPlayers + size_t(p) * kPlayerRecord;
        const uint8_t* r = h.state.data() + o;
        HokPlayer pl;
        pl.name = field(h.state, o, kPlayerName);
        if (pl.name.empty()) continue;  // vacant record
        pl.age = r[0x14];
        pl.skill_a = r[0x15];
        pl.skill_b = r[0x16];
        pl.form = r[0x17];
        pl.position = r[0x18];
        pl.foreigner = r[0x13] != 0;
        pl.league_goals = r[0x1B];
        pl.league_games = r[0x1C];
        pl.team = r[0x1D] == 0xFF ? -1 : r[0x1D];
        pl.raw = r;
        h.players.push_back(std::move(pl));
    }
    for (int t = 0; t < kHokLeagueTeams; t++) {
        const uint8_t* r = h.state.data() + kTable + size_t(t) * kTableRecord;
        HokTableRow row;
        row.penalty_minutes = le16(h.state, kTable + size_t(t) * kTableRecord + 0x43);
        row.form = field(h.state, kTable + size_t(t) * kTableRecord, 8);
        for (int s = 0; s < 2; s++) {
            row.goals_for[s] = r[0x12 + s];
            row.goals_against[s] = r[0x16 + s];
            row.games[s] = r[0x1A + s];
            row.wins[s] = r[0x22 + s];
            row.losses[s] = r[0x26 + s];
        }
        row.position = r[0x2A];
        for (int i = 0; i < 44; i++) row.position_history[i] = (r[0x2D + i / 2] >> (i % 2 * 4)) & 0x0F;
        h.table.push_back(std::move(row));
    }
    h.date.day = int(le32(h.state, kDate));
    h.date.month = int(le32(h.state, kDate + 4)) + 1;
    h.date.year = int(le32(h.state, kDate + 8));
    h.date.day_index = int(le32(h.state, kDate + 12));
    h.season_year = le16(h.state, kSeasonYear);
    for (int l = 0; l < 3; l++) h.matchday[l] = h.state[kMatchday + l];
    for (size_t l = 0; l < 3; l++)
        for (size_t i = 0; i < kHokLeagueSize; i++) {
            size_t o = l * kHokLeagueSize + i;
            if (i % 2 == 0) h.pairings[l][i / 2] = {h.state[kPairings + o], h.state[kPairings + o + 1]};
            h.table_order[l][i] = h.state[kTableOrder + o];
        }
    for (size_t g = 0; g < 2; g++) {
        for (size_t t = 0; t < 4; t++) h.euro_groups[g][t] = h.state[kEuroGroups + g * 4 + t];
        for (size_t r = 0; r < 6; r++)
            for (size_t p = 0; p < 2; p++) {
                size_t o = kEuroRounds + g * 36 + r * 6 + p * 2;
                h.euro_rounds[g][r][p] = {h.state[o], h.state[o + 1]};
            }
        for (size_t r = 0; r < 5; r++)
            for (size_t p = 0; p < 3; p++) {
                size_t o = kWmRounds + g * 36 + r * 6 + p * 2;
                h.wm_rounds[g][r][p] = {h.state[o], h.state[o + 1]};
            }
    }
    h.cup_round = h.state[kCupRound];
    for (size_t i = 0; i + 1 < kCupDrawMax && h.state[kCupDraw + i] != 0xFF; i += 2)
        h.cup_draw.push_back({h.state[kCupDraw + i], h.state[kCupDraw + i + 1]});
    h.squad = hok_squad(h, 0);  // explicit native player ids also retain called club players
    HokFinance& f = h.finance;
    for (int c = 0; c < kHokShares; c++) {
        f.share_cost[c] = money(h.state, kHoldings + size_t(c) * 8);
        f.shares[c] = le32(h.state, kHoldings + size_t(c) * 8 + 4);
        for (int i = 0; i < kHokShareHistory; i++)
            f.share_price[c][i] = le16(h.state, kSharePrices + size_t(c * kHokShareHistory + i) * 2) / kMoneyFactor;
    }
    const size_t m = kManagerStats;
    f.pp_goals = le16(h.state, m + 0x04);
    f.pp_against = le16(h.state, m + 0x06);
    f.sh_goals = le16(h.state, m + 0x08);
    f.sh_against = le16(h.state, m + 0x0A);
    f.power_plays = le16(h.state, m + 0x0C);
    f.penalty_kills = le16(h.state, m + 0x0E);
    f.attendance_total = long(le32(h.state, m + 0x10));
    f.attendance_record = long(le32(h.state, m + 0x14));
    f.attendance_min = long(le32(h.state, m + 0x18));
    f.balance = money(h.state, m + 0x1C);
    f.month_income = money(h.state, m + 0x24);
    f.bank = money(h.state, m + 0x38);
    return h;
}

namespace {
void put32(Bytes& b, size_t o, uint32_t v) {
    for (int i = 0; i < 4; i++) b.at(o + size_t(i)) = uint8_t(v >> (8 * i));
}
}  // namespace

void store_hok(HokFile& h) {
    Bytes& s = h.state;
    auto at = [&](const uint8_t* raw) { return size_t(raw - s.data()); };
    for (const HokTeam& team : h.teams) {
        if (!team.raw) continue;
        uint8_t* r = s.data() + at(team.raw);
        for (int kind = 0; kind < 2; kind++) {
            r[0x18 + kind] = uint8_t(team.skill1[kind]);
            r[0x1A + kind] = uint8_t(team.skill2[kind]);
            r[0x1C + kind] = uint8_t(team.form[kind]);
            for (int line = 0; line < 4; line++) r[0x1E + 4 * kind + line] = uint8_t(team.energy[kind][line]);
            r[0x2E + 2 * kind] = uint8_t(team.current_line[kind]);
            r[0x2F + 2 * kind] = uint8_t(team.shift[kind]);
        }
        r[0x17] = uint8_t(team.bonus);
        r[0x26] = uint8_t(team.effort_current);
        r[0x2C] = uint8_t(team.flags);
        r[0x32] = uint8_t(team.skaters);
        r[0x33] = uint8_t(team.penalty_code);
        r[0x2D] = uint8_t(team.effort_base);
        r[0x35] = uint8_t(team.offence_base);
        for (int i = 0; i < kTacticSituations; i++) {
            r[0x27 + i] = uint8_t(team.effort[i]);
            r[0x36 + i] = uint8_t(team.offence[i]);
        }
    }
    for (const HokPlayer& pl : h.players) {
        if (!pl.raw) continue;
        uint8_t* r = s.data() + at(pl.raw);
        r[0x14] = uint8_t(pl.age);
        r[0x15] = uint8_t(pl.skill_a);
        r[0x16] = uint8_t(pl.skill_b);
        r[0x17] = uint8_t(pl.form);
        r[0x18] = uint8_t(pl.position);
        r[0x1B] = uint8_t(pl.league_goals);
        r[0x1C] = uint8_t(pl.league_games);
        r[0x1D] = pl.team < 0 ? 0xFF : uint8_t(pl.team);
    }
    for (size_t t = 0; t < h.table.size() && t < size_t(kHokLeagueTeams); t++) {
        const HokTableRow& row = h.table[t];
        uint8_t* r = s.data() + kTable + t * kTableRecord;
        r[0x43] = uint8_t(row.penalty_minutes);
        r[0x44] = uint8_t(row.penalty_minutes >> 8);
        // a changed row gets its points and the league-season copies as well (hok.hpp)
        bool changed = false;
        for (size_t i = 0; i < 8 && i < row.form.size(); i++) changed |= r[i] != uint8_t(row.form[i]);
        for (int side = 0; side < 2; side++)
            changed |= r[0x12 + side] != row.goals_for[side] || r[0x16 + side] != row.goals_against[side] ||
                       r[0x1A + side] != row.games[side] || r[0x22 + side] != row.wins[side] ||
                       r[0x26 + side] != row.losses[side];
        if (changed) {
            for (int side = 0; side < 2; side++) {
                const int points = 2 * row.wins[side] + row.draws(side);
                r[-4 + side] = r[-2 + side] = uint8_t(points);
                r[0x14 + side] = uint8_t(row.goals_for[side]);
                r[0x18 + side] = uint8_t(row.goals_against[side]);
            }
            r[0x24] = uint8_t(row.wins[0]);
            r[0x25] = 0;
            r[0x28] = uint8_t(row.losses[0]);
            r[0x29] = 0;
            for (size_t i = 0; i < 8 && i < row.form.size(); i++) r[0x09 + i] = uint8_t(row.form[i]);
        }
        for (size_t i = 0; i < 8 && i < row.form.size(); i++) r[i] = uint8_t(row.form[i]);
        for (int side = 0; side < 2; side++) {
            r[0x12 + side] = uint8_t(row.goals_for[side]);
            r[0x16 + side] = uint8_t(row.goals_against[side]);
            r[0x1A + side] = uint8_t(row.games[side]);
            r[0x22 + side] = uint8_t(row.wins[side]);
            r[0x26 + side] = uint8_t(row.losses[side]);
        }
        r[0x2A] = uint8_t(row.position);
        for (int i = 0; i < 44; i += 2)
            r[0x2D + i / 2] = uint8_t((row.position_history[i] & 0x0F) | (row.position_history[i + 1] & 0x0F) << 4);
    }
    put32(s, kDate, uint32_t(h.date.day));
    put32(s, kDate + 4, uint32_t(h.date.month - 1));
    put32(s, kDate + 8, uint32_t(h.date.year));
    put32(s, kDate + 12, uint32_t(h.date.day_index));
    s[kSeasonYear] = uint8_t(h.season_year);  // DS:03BB
    s[kSeasonYear + 1] = uint8_t(h.season_year >> 8);
    for (int l = 0; l < 3; l++) s[kMatchday + size_t(l)] = uint8_t(h.matchday[l]);
    for (size_t l = 0; l < 3; l++)
        for (size_t i = 0; i < kHokLeagueSize; i++) {
            const size_t o = l * kHokLeagueSize + i;
            if (i % 2 == 0) {
                s[kPairings + o] = uint8_t(h.pairings[l][i / 2].home);
                s[kPairings + o + 1] = uint8_t(h.pairings[l][i / 2].away);
            }
            s[kTableOrder + o] = uint8_t(h.table_order[l][i]);
        }
    s[kCupRound] = uint8_t(h.cup_round);
    for (size_t i = 0; i + 1 < kCupDrawMax; i += 2) {
        if (i / 2 < h.cup_draw.size()) {
            s[kCupDraw + i] = uint8_t(h.cup_draw[i / 2].home);
            s[kCupDraw + i + 1] = uint8_t(h.cup_draw[i / 2].away);
        } else if (i / 2 == h.cup_draw.size()) {
            s[kCupDraw + i] = 0xFF;  // end of a shorter list
        }
    }
    // European cup groups (also at 0x8E71 = DS:5D1B, the draw slots of ovl05_4c7b(1)) and their
    // pairings; the two bytes after the pairs of each round are kept
    for (size_t g = 0; g < 2; g++) {
        for (size_t t = 0; t < 4; t++)
            s[kEuroGroups + g * 4 + t] = s[kEuroGroupsCopy + g * 4 + t] = uint8_t(h.euro_groups[g][t]);
        for (size_t r = 0; r < 6; r++)
            for (size_t p = 0; p < 2; p++) {
                const size_t o = kEuroRounds + g * 36 + r * 6 + p * 2;
                s[o] = uint8_t(h.euro_rounds[g][r][p].home);
                s[o + 1] = uint8_t(h.euro_rounds[g][r][p].away);
            }
    }
    for (size_t slot = 0; slot < h.squad.size() && slot < size_t(kHokSquadSlots); slot++) {
        const HokSquadPlayer& sp = h.squad[slot];
        if (sp.player < 0) continue;  // retain opaque bytes of an unoccupied native slot
        uint8_t* r = s.data() + kSquad + slot * kSquadRecord;
        r[0x00] = uint8_t(sp.major_penalties);
        r[0x01] = uint8_t(sp.minor_penalties);
        r[0x02] = uint8_t(sp.home_line);
        if (r[0x06] != sp.league_games || r[0x07] != sp.cup_games) {  // the screens read the u16 copies
            r[0x1C] = uint8_t(sp.league_games);
            r[0x1D] = uint8_t(sp.league_games >> 8);
            r[0x1E] = uint8_t(sp.cup_games);
            r[0x1F] = uint8_t(sp.cup_games >> 8);
        }
        r[0x06] = uint8_t(sp.league_games);
        r[0x07] = uint8_t(sp.cup_games);
        if (sp.status == SquadStatus::Team) r[0x1A] |= 0x80;
        r[0x03] = uint8_t(sp.league_goals_byte);
        r[0x04] = uint8_t(sp.cup_goals_byte);
        r[0x05] = uint8_t(sp.other_goals_byte);
        r[0x0B] = uint8_t(sp.contract_years);
        r[0x10] = uint8_t(sp.skill_a);
        r[0x11] = uint8_t(sp.skill_b);
        r[0x12] = uint8_t(sp.form);
        r[0x15] = uint8_t(sp.line);
        r[0x19] = uint8_t(sp.nibbles);
        r[0x1B] = uint8_t(sp.fitness);
        r[0x22] = uint8_t(sp.league_goals);
        r[0x24] = uint8_t(sp.cup_goals);
        r[0x26] = uint8_t(sp.other_goals);
    }
}

Bytes encode_hok(const HokFile& h) {
    if (h.header.size() != kHokHeaderSize) throw FormatError("HOK: header missing");
    Bytes d(h.header);
    d.push_back(h.key0);
    uint8_t k = h.key0;
    uint8_t sum = h.key0;  // running check sum (ovl16_0074)
    for (uint8_t p : h.state) {
        if (d.size() == kHokCheckByte) d.push_back(sum);
        d.push_back(uint8_t(uint8_t(p ^ k) + k));
        sum = uint8_t(sum + p);
        k = uint8_t(k - 0x29);
    }
    d.push_back(sum);
    return d;
}

size_t hok_state_index(size_t file_offset) {
    if (file_offset < kStateStart || file_offset == kHokCheckByte) return std::numeric_limits<size_t>::max();
    return file_offset - kStateStart - (file_offset > kHokCheckByte ? 1 : 0);
}

}  // namespace em
