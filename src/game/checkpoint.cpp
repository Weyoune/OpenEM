// SPDX-License-Identifier: GPL-3.0-only
#include "game/checkpoint.hpp"

#include <algorithm>
#include <limits>

namespace em {
namespace {
constexpr uint8_t kMagic[8] = {'O', 'P', 'E', 'N', 'E', 'M', 0x1A, 0};
constexpr uint32_t kVersion = 7;
constexpr size_t kHeader = 20, kMaxSize = 1024 * 1024;

uint32_t crc32(const Bytes& bytes, size_t first) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = first; i < bytes.size(); i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void put32(Bytes& bytes, uint32_t value) {
    for (int b = 0; b < 4; b++) bytes.push_back(uint8_t(value >> (8 * b)));
}

void put_signed(Bytes& bytes, long value) {
    if (value < INT32_MIN || value > INT32_MAX) throw FormatError("OpenEM checkpoint: value outside i32");
    put32(bytes, uint32_t(int32_t(value)));
}

class Reader {
public:
    explicit Reader(const Bytes& bytes) : bytes_(bytes) {}
    uint32_t u32() {
        if (bytes_.size() - at_ < 4) throw FormatError("OpenEM checkpoint: truncated field");
        const uint32_t v = le32(bytes_, at_);
        at_ += 4;
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
    int bounded(int lo, int hi) {
        const int32_t v = i32();
        if (v < lo || v > hi) throw FormatError("OpenEM checkpoint: invalid field range");
        return v;
    }
    uint8_t byte() {
        if (at_ == bytes_.size()) throw FormatError("OpenEM checkpoint: truncated byte");
        return bytes_[at_++];
    }
    Bytes blob(size_t count) {
        if (count > bytes_.size() - at_) throw FormatError("OpenEM checkpoint: truncated HOK");
        Bytes b(bytes_.begin() + long(at_), bytes_.begin() + long(at_ + count));
        at_ += count;
        return b;
    }
    void end() const {
        if (at_ != bytes_.size()) throw FormatError("OpenEM checkpoint: trailing fields");
    }
private:
    const Bytes& bytes_;
    size_t at_ = kHeader;
};
}  // namespace

bool is_game_checkpoint(const Bytes& data) {
    return data.size() >= sizeof kMagic && std::equal(std::begin(kMagic), std::end(kMagic), data.begin());
}

uint64_t game_data_fingerprint(const Bytes& exe) {
    uint64_t hash = 14695981039346656037ull;
    for (uint8_t b : exe) hash = (hash ^ b) * 1099511628211ull;
    return hash;  // identity check for local game data, not a cryptographic signature
}

GameCheckpoint decode_game_checkpoint(const Bytes& data) {
    if (!is_game_checkpoint(data) || data.size() < kHeader || data.size() > kMaxSize)
        throw FormatError("OpenEM checkpoint: invalid header or size");
    const uint32_t version=le32(data,8);
    if (version<1||version>kVersion) throw FormatError("OpenEM checkpoint: unsupported version");
    if (le32(data, 12) != data.size() - kHeader || le32(data, 16) != crc32(data, kHeader))
        throw FormatError("OpenEM checkpoint: damaged payload");
    Reader read(data);
    GameCheckpoint c;
    const uint64_t lo = read.u32(), hi = read.u32();
    c.exe_fingerprint = lo | (hi << 32);
    c.hok = read.blob(read.u32());
    c.working_state = read.blob(read.u32());
    if (!c.working_state.empty() && c.working_state.size() != 47761)
        throw FormatError("OpenEM checkpoint: invalid working-state size");
    c.game_random = read.u32();
    if (c.game_random >= 4000000) throw FormatError("OpenEM checkpoint: invalid game RNG");
    c.borland_random = read.u32();
    for (LineRotation* rotation : {&c.club_rotation, &c.national_rotation})
        for (auto& kind : rotation->used) for (uint8_t& used : kind) used = read.byte();
    for (int& role : c.injured_roles) role = read.bounded(0, 255);
    for (uint8_t& status : c.squad_status) {
        status = read.byte();
        if (status > uint8_t(SquadStatus::Team)) throw FormatError("OpenEM checkpoint: invalid squad status");
    }
    c.seasons = read.bounded(0, 1000000);
    c.team_strength = read.bounded(0, 1020);
    c.players = read.bounded(0, 30);
    c.squad_costs = read.i32(); c.pool_costs = read.i32();
    const int offers = read.bounded(0, 30);
    for (int i = 0; i < offers; i++) {
        CheckpointOffer offer;
        offer.player = read.bounded(0, kHokPlayers - 1);
        offer.bidder = read.bounded(0, kHokTeams - 1);
        offer.listed = read.bounded(0, 1) != 0;
        offer.amount = read.bounded(0, INT32_MAX);
        c.offers.push_back(offer);
    }
    if(version>=2)for(auto& manager:c.line_counts)for(auto& count:manager)count=read.byte();
    if(version>=3)for(auto& manager:c.stadium_refusals)for(auto& gate:manager){
        gate=read.byte();if(gate>1)throw FormatError("OpenEM checkpoint: invalid stadium refusal gate");
    }
    if(version>=4){
        c.save_history.count=read.byte();
        if(c.save_history.count>50)throw FormatError("OpenEM checkpoint: invalid save-history count");
        for(auto& identity:c.save_history.identities)identity=read.u32();
    }
    if(version>=5)for(auto& cursor:c.annual_distribution_cursors){
        cursor=read.byte();if(cursor>=17)throw FormatError("OpenEM checkpoint: invalid annual distribution cursor");
    }
    if(version>=6){c.last_scorer_slot=read.byte();if(c.last_scorer_slot>=30)throw FormatError("OpenEM checkpoint: invalid scorer slot");}
    if(version>=7){const int p=read.byte();if(p>1)throw FormatError("OpenEM checkpoint: invalid message step flag");c.message_step_pending=p!=0;}
    read.end();
    return c;
}

Bytes encode_game_checkpoint(const GameCheckpoint& c) {
    Bytes bytes(std::begin(kMagic), std::end(kMagic));
    put32(bytes, kVersion); put32(bytes, 0); put32(bytes, 0);
    put32(bytes, uint32_t(c.exe_fingerprint)); put32(bytes, uint32_t(c.exe_fingerprint >> 32));
    put32(bytes, uint32_t(c.hok.size()));
    bytes.insert(bytes.end(), c.hok.begin(), c.hok.end());
    put32(bytes, uint32_t(c.working_state.size()));
    bytes.insert(bytes.end(), c.working_state.begin(), c.working_state.end());
    put32(bytes, c.game_random); put32(bytes, c.borland_random);
    for (const LineRotation* rotation : {&c.club_rotation, &c.national_rotation})
        for (const auto& kind : rotation->used) for (uint8_t used : kind) bytes.push_back(used);
    for (int role : c.injured_roles) put_signed(bytes, role);
    bytes.insert(bytes.end(), c.squad_status.begin(), c.squad_status.end());
    for (int value : {c.seasons, c.team_strength, c.players}) put_signed(bytes, value);
    put_signed(bytes, c.squad_costs); put_signed(bytes, c.pool_costs);
    put32(bytes, uint32_t(c.offers.size()));
    for (const auto& offer : c.offers) {
        put_signed(bytes, offer.player); put_signed(bytes, offer.bidder);
        put_signed(bytes, int(offer.listed)); put_signed(bytes, offer.amount);
    }
    for(const auto& manager:c.line_counts)for(auto count:manager)bytes.push_back(count);
    for(const auto& manager:c.stadium_refusals)for(auto gate:manager)bytes.push_back(gate);
    if(c.save_history.count>50)throw FormatError("OpenEM checkpoint: invalid save-history count");
    bytes.push_back(c.save_history.count);
    for(auto identity:c.save_history.identities)put32(bytes,identity);
    for(auto cursor:c.annual_distribution_cursors)bytes.push_back(cursor);
    bytes.push_back(c.last_scorer_slot);
    bytes.push_back(c.message_step_pending ? 1 : 0);
    const uint32_t size = uint32_t(bytes.size() - kHeader), crc = crc32(bytes, kHeader);
    for (int b = 0; b < 4; b++) {
        bytes[12 + size_t(b)] = uint8_t(size >> (8 * b));
        bytes[16 + size_t(b)] = uint8_t(crc >> (8 * b));
    }
    (void)decode_game_checkpoint(bytes);  // same bounds for producers and consumers
    return bytes;
}

Bytes game_save_hok(const Bytes& exe, const Bytes& data) {
    if (!is_game_checkpoint(data)) return data;
    GameCheckpoint c = decode_game_checkpoint(data);
    if (c.exe_fingerprint != game_data_fingerprint(exe))
        throw FormatError("OpenEM checkpoint: EM.EXE differs from the saved game data");
    HokFile working = decode_hok(c.hok);
    if (c.working_state.size() != working.state.size())
        throw FormatError("OpenEM checkpoint: missing working state");
    // Export changes stats and cached values, never record identity. Reject a
    // forged working-state ID before its raw view can reach game routines.
    for (size_t manager = 0; manager < kHokManagers; manager++)
        for (size_t slot = 0; slot < kHokSquadSlots; slot++) {
            const size_t at = 0x696D + manager * 0x618 + slot * 52 + 0x0F;
            if (working.state.at(at) != c.working_state.at(at))
                throw FormatError("OpenEM checkpoint: inconsistent squad identity");
        }
    for (size_t team = 0; team < kHokTeams; team++) {
        const size_t at = 0x17E7 + team * 61 + 0x16;
        if (working.state.at(at) != c.working_state.at(at))
            throw FormatError("OpenEM checkpoint: inconsistent team owner");
    }
    std::copy(c.working_state.begin(), c.working_state.end(), working.state.begin());
    HokFile raw = decode_hok(encode_hok(working));
    for (int manager = 1; manager < kHokManagers; manager++) (void)hok_squad(raw, manager);
    return std::move(c.hok);
}
}  // namespace em
