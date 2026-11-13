// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The reload guard of the original: the game remembers the identities (save nonces, file header
// 0xB4) of the last 50 saves written in the running session and refuses to load one of them again
// ("Keinen heutigen Spielstand laden !"), which stops reloading a save of the same day.
#include "formats/binary.hpp"
#include <array>

namespace em {
// The session's save history: a FIFO of the last 50 save identities. It lives only in memory, not
// in the save file. All 50 slots take part in the check, including unused zero slots.
struct NativeSaveHistory {
    uint8_t count=0;                       // used slots, 0..50
    std::array<uint32_t,50> identities{};  // oldest first
    // True if `identity` is in any slot (an unused slot matches 0).
    bool contains(uint32_t identity) const;
    // Appends `identity`; when all 50 slots are used the oldest is dropped. Throws FormatError for a
    // corrupt counter.
    void remember(uint32_t identity);
    bool operator==(const NativeSaveHistory&) const=default;
};
// Thrown when a save of this session is loaded again; carries the original's message.
struct NativeReloadError : FormatError {
    NativeReloadError():FormatError("Keinen heutigen\nSpielstand laden !"){}
};
// The original's loader reaches the history check only after the fixed header version
// ("KRON_0.99") and the "EH0" marker at 0xB0. This check does not decode or validate the payload.
bool native_history_header(const Bytes& bytes);
} // namespace em
