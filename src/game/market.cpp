// SPDX-License-Identifier: GPL-3.0-only
#include "game/market.hpp"

#include "game/scorers.hpp"

namespace em {
namespace {
constexpr size_t kTarget = 0x1D0, kDays = 0x1E6, kTrend = 0x1EA, kHistory = 0x0B0, kHolding = 0x034;
constexpr int kCompanies = 4, kEntries = 36;

uint32_t get32(const Bytes& s, size_t o) { return uint32_t(s.at(o)) | uint32_t(s.at(o + 1)) << 8 | uint32_t(s.at(o + 2)) << 16 | uint32_t(s.at(o + 3)) << 24; }
void put32(Bytes& s, size_t o, uint32_t v) { for (int i = 0; i < 4; i++) s.at(o + size_t(i)) = uint8_t(v >> (8 * i)); }
uint16_t get16(const Bytes& s, size_t o) { return uint16_t(s.at(o) | s.at(o + 1) << 8); }
void put16(Bytes& s, size_t o, uint16_t v) { s.at(o) = uint8_t(v); s.at(o + 1) = uint8_t(v >> 8); }
}  // namespace

void market_day(Bytes& s, BorlandRandom& rng) {
    for (int i = 0; i < kCompanies; i++) {  // the targets grow by 0.03 % (32-bit product, unsigned division)
        const uint32_t t = get32(s, kTarget + size_t(i) * 4);
        put32(s, kTarget + size_t(i) * 4, t + uint32_t(t * 3u) / 10000u);
    }
    auto sign = [&] { return rng.range(0, 1) * 2 - 1; };
    for (int i = 0; i < kCompanies; i++) {
        const size_t hist = kHistory + size_t(i) * kEntries * 2;
        int32_t p = int32_t(get16(s, hist + (kEntries - 1) * 2)) / 3;
        const uint32_t target = get32(s, kTarget + size_t(i) * 4) / 100u;
        uint8_t& trend = s.at(kTrend + size_t(i));
        uint8_t& days = s.at(kDays + size_t(i));
        p += int(trend) - 1;
        if (p < 10) p = 10;
        if (days) days--;
        if (days == 0) {
            do trend = uint8_t(rng.range(0, 2)); while (trend == 1);
            days = uint8_t(rng.range(2, 8 + (trend > 0 ? 1 : 0)));
        }
        if (rng.range(0, 400 - 50 * i) == 0) {  // rare jump of the price, sometimes also of the target
            const int a = rng.range(int16_t(p / 5), int16_t(p / 2));
            p += int16_t(a * sign());
            if (rng.range(0, 1) == 0) {
                const int s2 = sign();
                put32(s, kTarget + size_t(i) * 4, uint32_t(p) * 100u + uint32_t(int32_t(s2 * 15)));
            }
        }
        if (rng.range(0, 22 - 2 * i) == 0) {
            const int a = rng.range(2, 10);
            p += int16_t(a * sign());
            if (rng.range(0, 3) == 0) {
                const int b = rng.range(2, 10);
                put32(s, kTarget + size_t(i) * 4, get32(s, kTarget + size_t(i) * 4) + uint32_t(int32_t(int16_t(b * sign()))));
            }
        }
        if (rng.range(0, 1) != 0) p += rng.range(0, 2) - 1;
        bool step_down = uint32_t(p) > target;
        if (!step_down) step_down = rng.range(0, 1) != 0;
        if (step_down && trend == 2) p -= 1;
        bool step_up = uint32_t(p) < target;
        if (!step_up) step_up = rng.range(0, 1) != 0;
        if (step_up && trend == 0) p += 1;
        for (int e = 0; e + 1 < kEntries; e++) put16(s, hist + size_t(e) * 2, get16(s, hist + size_t(e + 1) * 2));
        if (p > 2000) {  // 2:1 split: price 1000, every manager's holding doubles
            p = 1000;
            for (int k = 0; k < 4; k++) {
                const size_t q = kHolding + size_t(k) * 32 + size_t(i) * 8;
                put32(s, q, get32(s, q) * 2u);
            }
        }
        put16(s, hist + (kEntries - 1) * 2, uint16_t(uint32_t(p) * 3u));
    }
}

}  // namespace em
