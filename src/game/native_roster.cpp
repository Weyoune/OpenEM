// SPDX-License-Identifier: GPL-3.0-only
#include "game/native_roster.hpp"

#include <algorithm>
#include <cstring>

namespace em {
namespace {
constexpr size_t kSquad = 0x696D, kBlock = 0x618, kRecord = 0x34;
constexpr size_t kPlayerBase = 0x4FA5, kPlayerRecord = 0x21;
constexpr int kLastSlot = 29;

void validate_state(const HokFile& h) {
    // Includes all24 notification records, player records and all6 squad blocks.
    (void)h.state.at(0xB94E);
}
size_t player_offset(uint8_t id) {
    // ID0 is the native dummy record and can be read by331B for a roster hole.
    if (id > 199) throw FormatError("native roster: invalid player ID");
    return kPlayerBase + size_t(id) * kPlayerRecord;
}
size_t row_offset(int manager, int slot) {
    return kSquad + size_t(manager) * kBlock + size_t(slot) * kRecord;
}
int position_class(const HokFile& h, uint8_t id) {
    return std::min(2, int(h.state.at(player_offset(id) + 0x18)) / 25);
}
int occupied_count(const HokFile& h, int manager) {
    // 645B:0D27 counts all29 regular slots (pool4:12), excluding scratch29.
    const int slots = manager == 4 ? 12 : 29;
    int result = 0;
    for (int slot = 0; slot < slots; ++slot)
        result += h.state.at(row_offset(manager, slot) + 0x0F) != 0;
    return result;
}
int reserved_count(const HokFile& h,int target) {
    // 5396:331B uses the occupied count as a PREFIX LENGTH when reading other
    // managers' owner IDs. Preserve this quirk if the raw roster has holes.
    int result = 0;
    const int humans = h.state.at(0x0DC6);
    for (int manager = 0; manager <= 4; ++manager) {
        if (manager >= humans && manager != 4) continue;
        const int count = occupied_count(h, manager);
        if (manager == target) {
            result += count;
        } else {
            for (int slot = 0; slot < count; ++slot) {
                const uint8_t id = h.state.at(row_offset(manager, slot) + 0x0F);
                result += h.state.at(player_offset(id) + 0x1A) == target;
            }
        }
    }
    return result;
}
void put32(Bytes& s, size_t at, uint32_t value) {
    for (size_t byte = 0; byte < 4; ++byte)
        s.at(at + byte) = uint8_t(value >> (8 * byte));
}
void clear_notification(HokFile& h, size_t player) {
    const uint8_t index = h.state.at(player + 0x1E);
    if (index == 0 || index >= 25) return;
    // 645B:0180 clears entry's first byte and next word, not its whole text.
    const size_t entry = 0xAEB7 + size_t(index - 1) * 0x71;
    std::fill_n(h.state.begin() + long(entry), 3, uint8_t(0));
    h.state.at(player + 0x1E) = 0;
}
} // namespace

int native_reserved_players(const HokFile& h,int manager){
    validate_state(h);
    if(manager<0||manager>=4)throw FormatError("native roster: invalid club manager");
    return reserved_count(h,manager);
}
std::optional<NativeRosterLocation> native_find_roster_player(const HokFile& h,uint8_t id){
    validate_state(h);
    if(!id||id>199)throw FormatError("native roster: invalid lookup player ID");
    for(int manager=0;manager<6;++manager){
        if(manager>=h.state.at(0x0dc6)&&manager<4)continue;
        const int count=occupied_count(h,manager);
        for(int slot=0;slot<count;++slot)
            if(h.state.at(row_offset(manager,slot)+0x0f)==id)return NativeRosterLocation{manager,slot};
    }
    return std::nullopt;
}
void native_clear_player_notification(HokFile& h,uint8_t id){
    validate_state(h);if(!id)throw FormatError("native roster: empty notification player ID");
    clear_notification(h,player_offset(id));
}
NativeRosterInsertion native_insert_player(HokFile& h,const ContractTables& tables,
    const ContractRandomDraw& draw,uint8_t playerId,int manager,int years) {
    validate_state(h);
    if(manager<0||manager>=4||years<0||years>255)throw FormatError("native roster: invalid manager or contract years");
    if (playerId == 0) throw FormatError("native roster: empty incoming player ID");
    const size_t player = player_offset(playerId);
    const uint8_t club = h.state.at(0x0DCD + size_t(manager)*0x1af + 0x14);
    // 2D27..2D52 bypasses the count if the player is already at the buyer's club.
    if (h.state.at(player + 0x1D) != club && reserved_count(h,manager) > 28) return {};

    const int incomingClass = position_class(h, playerId);
    int slot = 0;
    for (;;) {
        const uint8_t id = h.state.at(row_offset(manager, slot) + 0x0F);
        if (!id || position_class(h, id) > incomingClass || slot == kLastSlot) break;
        ++slot;
    }
    const size_t row = row_offset(manager, slot);
    const bool shifted=h.state.at(row + 0x0F)&&slot!=kLastSlot;
    if (shifted)
        std::memmove(h.state.data() + row + kRecord, h.state.data() + row,
                     size_t(kLastSlot - slot) * kRecord);
    std::fill_n(h.state.begin() + long(row), kRecord, uint8_t(0));
    h.state.at(row + 0x0F) = playerId;
    h.state.at(row + 0x13) = uint8_t(draw(80, 120));
    h.state.at(row + 0x0E) = uint8_t(draw(35, 65));
    const int trend = draw(1, 13);
    const int direction = draw(1, 3);
    const int high = draw(0, 1);
    h.state.at(row + 0x14) = uint8_t(trend | (direction << 4) | (high << 7));
    h.state.at(row + 0x0B) = uint8_t(years);
    h.state.at(row + 0x03) = h.state.at(player + 0x1B);
    h.state.at(row + 0x06) = h.state.at(player + 0x1C);
    h.state.at(row + 0x10) = h.state.at(player + 0x15);
    h.state.at(row + 0x11) = h.state.at(player + 0x16);
    h.state.at(row + 0x12) = h.state.at(player + 0x17);
    // 2F31 computes salary before2FDA changes the team. Salary valuation has
    // no RNG path, so the helper's successful insertion has exactly five draws.
    const auto contract = contract_player(h, manager, slot);
    put32(h.state, row + 0x28, uint32_t(player_worth(tables, contract, Worth::Salary, draw)));
    h.state.at(player + 0x1D) = club;
    return {slot,shifted};
}
int native_insert_cpu_purchase(HokFile& h,const ContractTables& tables,BorlandRandom& rng,uint8_t playerId){
    return native_insert_player(h,tables,[&](int lo,int hi){return rng.range(lo,hi);},playerId,0,1).slot;
}

void native_remove_storage_row(HokFile& h,int manager,int slot,int last) {
    validate_state(h);
    if(manager<0||manager>=6||last<0||last>kLastSlot||slot<0||slot>last)throw FormatError("native roster: invalid removal location");
    const size_t row=row_offset(manager,slot);const uint8_t id=h.state.at(row+0x0f);
    if(!id)return;
    clear_notification(h,player_offset(id));
    if(slot!=last)std::memmove(h.state.data()+row,h.state.data()+row+kRecord,size_t(last-slot)*kRecord);
    h.state.at(row_offset(manager,last)+0x0f)=0;
}
void native_remove_player(HokFile& h,int manager,int slot){
    if(manager<0||manager>=4)throw FormatError("native roster: invalid club manager");
    native_remove_storage_row(h,manager,slot,kLastSlot);
}
void native_remove_purchase_row(HokFile& h,int slot){native_remove_player(h,0,slot);}

} // namespace em
