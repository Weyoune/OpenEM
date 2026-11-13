// SPDX-License-Identifier: GPL-3.0-only
// Save history (reload guard) of a session; see save_history.hpp.
#include "game/save_history.hpp"
#include <algorithm>

namespace em {
bool NativeSaveHistory::contains(uint32_t identity) const {
    return std::find(identities.begin(),identities.end(),identity)!=identities.end();
}
void NativeSaveHistory::remember(uint32_t identity){
    if(count>50)throw FormatError("native save history: invalid counter");
    if(count<50)identities[size_t(count++)]=identity;
    else{
        std::move(identities.begin()+1,identities.end(),identities.begin());
        identities.back()=identity;
    }
}
bool native_history_header(const Bytes& bytes){
    constexpr std::array<uint8_t,10> version{'K','R','O','N','_','0','.','9','9',0};
    constexpr std::array<uint8_t,4> marker{'E','H','0',0};
    return bytes.size()>=0xb8&&std::equal(version.begin(),version.end(),bytes.begin())&&
        std::equal(marker.begin(),marker.end(),bytes.begin()+0xb0);
}
} // namespace em
