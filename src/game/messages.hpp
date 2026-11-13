// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Manager messages ("Nachrichten", the message field of the main menu): the fixed message table in
// the save state, the German message templates, and the routines that add, delete, sort, age and
// enumerate entries. The message field itself is drawn by src/ui/classic_messages.cpp.
//
// Layout: 24 entries of 0x71 bytes at save state offset 0xAEB7:
//   +0x00 u8  flags: bits 0..2 manager (0..3), bit 3 "general" (no player attached; the original sets
//             it when the message is added without a player reference), bits 4..7 remaining display
//             steps (new entries: 3). A flags byte of 0 marks a free entry.
//   +0x01 u8  creation day index (the running day counter at state 0x8E2D), capped at 255; this is
//             the sort key, newest first
//   +0x02 u8  player ID the entry belongs to (0 = none); the player record +0x1E holds the entry's
//             1-based index so selling or releasing the player can delete the message again
//   +0x03     0x6E text bytes, native character set: "<day>. <month> <year>^" then the template with
//             '$' (string) and '#' (number) substitutions; '^' ends a line.
// State 0xB966 + manager: "new message" flag, set by every entry that is not shown at once; the main
// menu clears it and opens the message field at the newest message.
//
// Life cycle: entries are added by the season/transfer code, sorted newest first, and "general"
// entries (bit 3) count their display steps down once per play step and disappear at zero.
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "formats/binary.hpp"

namespace em {

class BorlandRandom;

// kMessageBase: state offset of entry 0; kMessageSize: bytes per entry; kMessageText: text bytes per
// entry (from +0x03); kMessageNewFlag: state offset of the four per-manager "new message" bytes.
constexpr size_t kMessageBase = 0xAEB7, kMessageSize = 0x71, kMessageText = 0x6E, kMessageNewFlag = 0xB966;
constexpr int kMessageSlots = 24;  // number of entries in the table

// The message templates of the original game (native character set, see native_from_utf8).
enum class MessageKind {
    ContractOffer = 0,  // "$ bietet Ihnen an, $^Jahre zu verl{ngern.^"
    Interest = 1,       // "$ ist an $^interessiert.^"
    TwoLines = 2,       // "$^$^"
    Retirement = 3,     // "$ gibt das Eishockeyspielen im Alter von^# Jahren endg}ltig auf.^"
    NoRenewal = 4,      // "$ k}ndigt an, da~ er seinen Vertrag^nicht mehr verl{ngern wird.^"
    ThreeParts = 5,     // "$^$ $ ^"
    Invalid = 6,        // "$ ist Sportinvalide.^Er verl{~t Ihre Mannschaft endg}ltig.^"
};

// One substitution: a native string ('$') or a number ('#'). Arguments are consumed in template
// order; a '#' placeholder always prints the number, a '$' placeholder prints the number when the
// argument is numeric and the text otherwise.
struct MessageArg {
    std::string text;        // native character set
    long number = 0;
    bool is_number = false;  // true: substitute `number`, printed without thousands separators
    static MessageArg str(std::string native) { return {std::move(native), 0, false}; }
    static MessageArg num(long value) { return {{}, value, true}; }
};

// The current menu date used to stamp a message.
struct MessageDate {
    int day = 1, month = 1, year = 1993;  // month 1..12
    int day_index = 0;                    // running day counter (sort key of the entry)
};

// Adds a message to the first free entry and returns its 1-based index (0 when all 24 entries are in
// use). Like the original it always draws rand(0, 3) for the "days back" of the printed date, even
// when the table is full and nothing is stored. `general` sets flag bit 3 (no player attached),
// `player_id` is stored at +0x02, and unless `show_now` is set the manager's "new message" flag is
// raised. New entries start with 3 display steps. The bytes after the text's terminating NUL hold
// leftover stack data in the original; OpenEM stores zeros there.
int native_add_message(Bytes& state, BorlandRandom& rng, const MessageDate& date, MessageKind kind,
                       const std::vector<MessageArg>& args, int manager, bool general, int player_id = 0,
                       bool show_now = false);
// The formatted text alone (as stored at +0x03, without truncation); draws the date offset from `rng`.
std::string native_message_text(BorlandRandom& rng, const MessageDate& date, MessageKind kind,
                                const std::vector<MessageArg>& args);
// The same with an already drawn date offset (0..3 days back). Going back across a month start
// wraps into the previous month (and year); February always has 28 days.
std::string native_message_text(int days_back, const MessageDate& date, MessageKind kind, const std::vector<MessageArg>& args);
// Deletes the entry whose 1-based index is stored in `handle` (a player's +0x1E back reference), then
// zeroes the handle. Only the first three bytes are cleared; the old text stays in the entry.
void native_delete_message(Bytes& state, uint8_t& handle);
// A message shown at once and deleted again (season review): its text stays in the first free entry
// while the flags byte remains 0, so the entry is still free.
void native_flash_message(Bytes& state, const std::string& text);
// Sorts the table newest first (exchange sort on the day index); the player back references (+0x1E)
// of non-general entries follow the moved entries.
void native_sort_messages(Bytes& state);
// One play step: general entries lose one display step; at zero they are deleted.
void native_message_step(Bytes& state);
// The next (forward) or previous entry of `manager` after/before `from`; -1 when none.
// from = -1 with forward finds the first.
int native_next_message(const Bytes& state, int from, bool forward, int manager);

// UTF-8 to the native character set of texts ({|}~[\] for äöüßÄÖÜ); other non-ASCII
// two-byte sequences starting with 0xC3 become '?'.
std::string native_from_utf8(const std::string& utf8);

// A decoded message entry.
struct MessageView {
    int index = -1;  // 0..23
    int manager = 0;
    bool general = false;
    int steps = 0;      // remaining display steps
    int day_index = 0;  // creation day (capped at 255)
    int player_id = 0;
    std::vector<std::string> lines;  // native bytes, split at '^'; text after the last '^' is dropped
};
// Decodes entry `index` (0..23); throws std::out_of_range for other indices.
MessageView native_message(const Bytes& state, int index);
// All used entries of `manager` in table order.
std::vector<MessageView> native_messages(const Bytes& state, int manager);

}  // namespace em
