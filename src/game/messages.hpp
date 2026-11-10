// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Manager messages of EM.EXE ("Nachrichten", the message field of the main menu).
//
// 24 entries of 0x71 bytes at DS:D294 (HOK state 0xAEB7):
//   +0x00 u8  flags: bits 0..2 manager (0..3), bit 3 "general" (no player attached; set when the third
//             substitution pointer is FFFF:FFFF), bits 4..7 remaining display steps (new entries: 3)
//   +0x01 u8  creation day index DS:03B7 (state 0x8E2D), capped at 255 (sort key, newest first)
//   +0x02 u8  player ID the entry belongs to (0 = none); the player record +0x1E holds the entry's
//             1-based index so selling or releasing the player can delete it (ovr036:0180)
//   +0x03     0x6E text bytes, native character set: "<day>. <month> <year>^" then the template with
//             '$' (string) and '#' (number) substitutions; '^' ends a line.
// DS:5A50 + manager (state 0xB966): "new message" flag, set by every non-immediate entry; the main
// menu (ovr034:2120) clears it and opens the message field at the newest message.
//
// Routines (IDA listing IDA/EM.exe.lst, overlay ovr036 = 645B):
//   ovr036:0206 add (sub_57F66)   ovr036:01CA add general text (sub_57F2A)   ovr036:0180 delete
//   ovr036:0037 sort (sub_57D97)  ovr036:08DD next/previous of a manager (sub_5863D)
//   ovr038:3067 per play step: general entries count down, the last step deletes them
//   ovr036:0504 the message field (src/ui/classic_messages.cpp)
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "formats/binary.hpp"

namespace em {

class BorlandRandom;

constexpr size_t kMessageBase = 0xAEB7, kMessageSize = 0x71, kMessageText = 0x6E, kMessageNewFlag = 0xB966;
constexpr int kMessageSlots = 24;

// The template table DS:0CA0.
enum class MessageKind {
    ContractOffer = 0,  // "$ bietet Ihnen an, $^Jahre zu verl{ngern.^"
    Interest = 1,       // "$ ist an $^interessiert.^"
    TwoLines = 2,       // "$^$^"
    Retirement = 3,     // "$ gibt das Eishockeyspielen im Alter von^# Jahren endg}ltig auf.^"
    NoRenewal = 4,      // "$ k}ndigt an, da~ er seinen Vertrag^nicht mehr verl{ngern wird.^"
    ThreeParts = 5,     // "$^$ $ ^"
    Invalid = 6,        // "$ ist Sportinvalide.^Er verl{~t Ihre Mannschaft endg}ltig.^"
};

// One substitution: a native string ('$') or a number ('#').
struct MessageArg {
    std::string text;
    long number = 0;
    bool is_number = false;
    static MessageArg str(std::string native) { return {std::move(native), 0, false}; }
    static MessageArg num(long value) { return {{}, value, true}; }
};

// The menu date (DS:5A20 day, DS:5A1C month 0..11, DS:5A18 year, DS:03B7 running day index).
struct MessageDate {
    int day = 1, month = 1, year = 1993;  // month 1..12
    int day_index = 0;
};

// ovr036:0206. Draws rand(0, 3) for the display date in every case (also when all 24 entries are in
// use, then nothing is stored and 0 is returned). `general` corresponds to the FFFF:FFFF third pointer
// (ovr036:01CA). Returns the 1-based entry index. The bytes after the text's NUL are stack garbage in the
// original; OpenEM stores zeros there.
int native_add_message(Bytes& state, BorlandRandom& rng, const MessageDate& date, MessageKind kind,
                       const std::vector<MessageArg>& args, int manager, bool general, int player_id = 0,
                       bool show_now = false);
// The formatted text alone (as stored at +0x03, without truncation).
std::string native_message_text(BorlandRandom& rng, const MessageDate& date, MessageKind kind,
                                const std::vector<MessageArg>& args);
// The same with an already drawn date offset (0..3 days back).
std::string native_message_text(int days_back, const MessageDate& date, MessageKind kind, const std::vector<MessageArg>& args);
// ovr036:0180: delete the entry whose 1-based index is stored in `handle`, then zero the handle.
void native_delete_message(Bytes& state, uint8_t& handle);
// A message shown at once and deleted again (season review): its text stays in the first free entry (flag 0).
void native_flash_message(Bytes& state, const std::string& text);
// ovr036:0037: newest first; player back references (+0x1E) follow the moved entries.
void native_sort_messages(Bytes& state);
// ovr038:3067..30FB: general entries lose one display step; at zero they are deleted.
void native_message_step(Bytes& state);
// ovr036:08DD: the next (forward) or previous entry of `manager` after/before `from`; -1 when none.
// from = -1 with forward finds the first.
int native_next_message(const Bytes& state, int from, bool forward, int manager);

// UTF-8 to the native character set of texts ({|}~[\] for äöüßÄÖÜ).
std::string native_from_utf8(const std::string& utf8);

struct MessageView {
    int index = -1;  // 0..23
    int manager = 0;
    bool general = false;
    int steps = 0;
    int day_index = 0;
    int player_id = 0;
    std::vector<std::string> lines;  // native bytes, split at '^'
};
MessageView native_message(const Bytes& state, int index);
std::vector<MessageView> native_messages(const Bytes& state, int manager);

}  // namespace em
