// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "formats/binary.hpp"
#include <array>

namespace em {
// Native process context: count DS0177, FIFO slots DSDD4E..DE15.
// All50 slots participate in the guard, including unused zero slots.
struct NativeSaveHistory {
    uint8_t count=0;
    std::array<uint32_t,50> identities{};
    bool contains(uint32_t identity) const;
    void remember(uint32_t identity);
    bool operator==(const NativeSaveHistory&) const=default;
};
struct NativeReloadError : FormatError {
    NativeReloadError():FormatError("Keinen heutigen\nSpielstand laden !"){}
};
// The native loader reaches its history phase only after the fixed header
// version and EH0 marker. This check does not decode or validate the payload.
bool native_history_header(const Bytes& bytes);
} // namespace em
