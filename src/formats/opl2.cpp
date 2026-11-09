// SPDX-License-Identifier: GPL-3.0-only
#include "formats/opl2.hpp"

#include <algorithm>
#include <cmath>

namespace em {
namespace {

constexpr double kPi = 3.14159265358979323846;
// Register offset of the modulator of each channel; the carrier is 3 further.
constexpr int kModOffset[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
// Multiplier x2 (MULT 0 = 1/2).
constexpr int kMult2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
// Key scale level ROM (units of 0.75 dB at block 8, fnum bits 9..6), shift per KSL setting 0..3.
constexpr int kKslRom[16] = {0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64};
constexpr int kKslShift[4] = {8, 1, 2, 0};
// Data-sheet times for rate 1 (effective rate 4): full attack 2826 ms, decay/release 0 -> 96 dB 39280 ms; every
// further step of 4 in the effective rate halves them.
constexpr double kAttackSeconds = 2.826, kDecaySeconds = 39.28;

bool valid_offset(int o) { return o >= 0 && o < 22 && (o & 7) < 6; }

}  // namespace

void Opl2::reset() {
    regs_.fill(0);
    op_ = {};
    ch_ = {};
    wave_select_ = nts_ = rhythm_ = deep_am_ = deep_vib_ = false;
    am_phase_ = vib_phase_ = am_value_ = 0.0;
    vib_factor_ = 1.0;
    noise_ = 1;
}

Opl2::Op& Opl2::slot_of(int ch, int which) { return op_[size_t(kModOffset[ch] + 3 * which)]; }

void Opl2::key(Op& op, bool normal, bool on) {
    const bool before = op.key_normal || op.key_rhythm;
    (normal ? op.key_normal : op.key_rhythm) = on;
    const bool after = op.key_normal || op.key_rhythm;
    if (!before && after) {
        op.env = Env::Attack;
        op.phase = 0;
    } else if (before && !after && op.env != Env::Off) {
        op.env = Env::Release;
    }
}

void Opl2::write(int reg, int value) {
    reg &= 0xFF;
    value &= 0xFF;
    regs_[size_t(reg)] = uint8_t(value);
    const int group = reg & 0xE0, offset = reg & 0x1F;
    if (reg == 0x01) {
        wave_select_ = (value & 0x20) != 0;
        return;
    }
    if (reg == 0x08) {
        nts_ = (value & 0x40) != 0;
        return;
    }
    if (reg == 0xBD) {
        deep_am_ = (value & 0x80) != 0;
        deep_vib_ = (value & 0x40) != 0;
        rhythm_ = (value & 0x20) != 0;
        const bool r = rhythm_;
        key(slot_of(6, 0), false, r && (value & 0x10));  // bass drum: both operators of channel 6
        key(slot_of(6, 1), false, r && (value & 0x10));
        key(op_[0x14], false, r && (value & 0x08));       // snare drum: carrier of channel 7
        key(op_[0x12], false, r && (value & 0x04));       // tom-tom: modulator of channel 8
        key(op_[0x15], false, r && (value & 0x02));       // cymbal: carrier of channel 8
        key(op_[0x11], false, r && (value & 0x01));       // hi-hat: modulator of channel 7
        return;
    }
    if (group >= 0x20 && group <= 0x80 && valid_offset(offset)) {
        Op& op = op_[size_t(offset)];
        switch (group) {
        case 0x20:
            op.am = value & 0x80;
            op.vib = value & 0x40;
            op.egt = value & 0x20;
            op.ksr = value & 0x10;
            op.mult = value & 0x0F;
            break;
        case 0x40:
            op.ksl = value >> 6;
            op.tl = value & 0x3F;
            break;
        case 0x60:
            op.ar = value >> 4;
            op.dr = value & 0x0F;
            break;
        case 0x80:
            op.sl = value >> 4;
            op.rr = value & 0x0F;
            break;
        }
        return;
    }
    if (group == 0xE0 && valid_offset(offset)) {
        op_[size_t(offset)].wave = value & 3;
        return;
    }
    const int ch = reg & 0x0F;
    if (ch > 8) return;
    Channel& c = ch_[size_t(ch)];
    if ((reg & 0xF0) == 0xA0) {
        c.fnum = (c.fnum & 0x300) | value;
    } else if ((reg & 0xF0) == 0xB0) {
        c.fnum = (c.fnum & 0xFF) | (value & 3) << 8;
        c.block = (value >> 2) & 7;
        const bool on = (value & 0x20) != 0;
        c.key = on;
        key(slot_of(ch, 0), true, on);
        key(slot_of(ch, 1), true, on);
    } else if ((reg & 0xF0) == 0xC0) {
        c.fb = (value >> 1) & 7;
        c.cnt = (value & 1) != 0;
    }
}

uint32_t Opl2::phase_inc(const Op& op, const Channel& c) const {
    double inc = double((uint32_t(c.fnum) << c.block) >> 1) * kMult2[op.mult] / 2.0;
    if (op.vib) inc *= vib_factor_;
    return uint32_t(inc);
}

void Opl2::step_envelope(Op& op, const Channel& c) {
    const int keycode = c.block * 2 + ((c.fnum >> (nts_ ? 8 : 9)) & 1);
    const int rof = op.ksr ? keycode : keycode >> 2;
    auto effective = [&](int rate) { return rate == 0 ? 0 : std::min(63, rate * 4 + rof); };
    auto decay_step = [&](int rate) {
        const int eff = effective(rate);
        if (eff == 0) return 0.0;
        return 511.0 / (kDecaySeconds * std::exp2(1.0 - eff / 4.0) * kRate);
    };
    switch (op.env) {
    case Env::Off:
        op.att = 511.0;
        break;
    case Env::Attack: {
        const int eff = effective(op.ar);
        if (eff >= 60) {
            op.att = 0.0;
        } else if (eff > 0) {
            // exponential approach like the chip (att += ~att * inc >> 3): reaches 0 after the data-sheet time
            const double samples = kAttackSeconds * std::exp2(1.0 - eff / 4.0) * kRate;
            op.att -= (33.38 / samples) * (op.att / 8.0 + 1.0);
        }
        if (op.att <= 0.0) {
            op.att = 0.0;
            op.env = Env::Decay;
        }
        break;
    }
    case Env::Decay: {
        const double sustain = op.sl == 15 ? 496.0 : op.sl * 16.0;
        op.att += decay_step(op.dr);
        if (op.att >= sustain) {
            op.att = sustain;
            op.env = op.egt ? Env::Sustain : Env::Release;  // EGT 0: percussive, keeps decaying with RR
        }
        break;
    }
    case Env::Sustain:
        break;
    case Env::Release:
        op.att += decay_step(op.rr);
        if (op.att >= 511.0) {
            op.att = 511.0;
            op.env = Env::Off;
        }
        break;
    }
}

float Opl2::level(const Op& op, const Channel& c) const {
    int ksl = (kKslRom[c.fnum >> 6] << 2) - ((8 - c.block) << 5);
    if (ksl < 0) ksl = 0;
    double total = op.att + op.tl * 4.0 + (ksl >> kKslShift[op.ksl]) + (op.am ? am_value_ : 0.0);
    if (total >= 511.0) return 0.0f;
    return float(std::pow(10.0, -total * 0.1875 / 20.0));
}

float Opl2::wave(int w, int index) {
    index &= 1023;
    const float s = float(std::sin(2.0 * kPi * index / 1024.0));
    switch (w) {
    case 1: return s > 0.0f ? s : 0.0f;
    case 2: return std::fabs(s);
    case 3: return (index & 256) ? 0.0f : std::fabs(s);
    default: return s;
    }
}

double Opl2::attenuation_db(int offset) const {
    if (!valid_offset(offset)) return 96.0;
    return op_[size_t(offset)].att * 0.1875;
}

float Opl2::sample() {
    // LFOs: tremolo 3.7 Hz (1 or 4.8 dB), vibrato 6.1 Hz (7 or 14 cents)
    am_phase_ += 3.7 / kRate;
    if (am_phase_ >= 1.0) am_phase_ -= 1.0;
    am_value_ = (1.0 - std::fabs(2.0 * am_phase_ - 1.0)) * (deep_am_ ? 4.8 : 1.0) / 0.1875;
    vib_phase_ += 6.1 / kRate;
    if (vib_phase_ >= 1.0) vib_phase_ -= 1.0;
    vib_factor_ = std::exp2(std::sin(2.0 * kPi * vib_phase_) * (deep_vib_ ? 14.0 : 7.0) / 1200.0);
    const uint32_t bit = ((noise_ >> 14) ^ noise_) & 1;
    noise_ = (noise_ >> 1) | (bit << 22);

    for (int ch = 0; ch < 9; ch++) {
        for (int w = 0; w < 2; w++) {
            Op& op = slot_of(ch, w);
            step_envelope(op, ch_[size_t(ch)]);
            op.phase += phase_inc(op, ch_[size_t(ch)]);
        }
    }
    auto index = [](const Op& op) { return int(op.phase >> 9) & 1023; };
    auto wf = [&](const Op& op) { return wave_select_ ? op.wave : 0; };

    float mix = 0.0f;
    const int melodic = rhythm_ ? 6 : 9;
    for (int ch = 0; ch < 9; ch++) {
        if (ch >= melodic && ch > 6) continue;  // channels 7 and 8 are handled below in rhythm mode
        const Channel& c = ch_[size_t(ch)];
        Op& m = slot_of(ch, 0);
        Op& k = slot_of(ch, 1);
        float fbmod = 0.0f;
        if (c.fb) fbmod = (m.out + m.prev) * 1024.0f * float(std::exp2(c.fb - 7));
        m.prev = m.out;
        m.out = wave(wf(m), index(m) + int(fbmod)) * level(m, c);
        const bool bass_drum = rhythm_ && ch == 6;
        if (c.cnt && !bass_drum) {
            k.out = wave(wf(k), index(k)) * level(k, c);
            mix += m.out + k.out;
        } else {
            k.out = wave(wf(k), index(k) + int(m.out * 4096.0f)) * level(k, c);
            mix += bass_drum ? 2.0f * k.out : k.out;
        }
    }
    if (rhythm_) {
        const Channel& c7 = ch_[7];
        const Channel& c8 = ch_[8];
        Op& hh = op_[0x11];
        Op& sd = op_[0x14];
        Op& tt = op_[0x12];
        Op& tc = op_[0x15];
        const int hp = index(hh), tp = index(tc);
        const int noise = int(noise_ & 1);
        const int hh2 = (hp >> 2) & 1, hh3 = (hp >> 3) & 1, hh7 = (hp >> 7) & 1, hh8 = (hp >> 8) & 1;
        const int tc3 = (tp >> 3) & 1, tc5 = (tp >> 5) & 1;
        const int rm_xor = (hh2 ^ hh7) | (hh3 ^ tc5) | (tc3 ^ tc5);
        int hh_phase = rm_xor << 9;
        hh_phase |= (rm_xor ^ noise) ? 0xD0 : 0x34;
        const int sd_phase = (hh8 << 9) | ((hh8 ^ noise) << 8);
        const int tc_phase = (rm_xor << 9) | 0x80;
        hh.out = wave(wf(hh), hh_phase) * level(hh, c7);
        sd.out = wave(wf(sd), sd_phase) * level(sd, c7);
        tt.out = wave(wf(tt), index(tt)) * level(tt, c8);
        tc.out = wave(wf(tc), tc_phase) * level(tc, c8);
        mix += 2.0f * (hh.out + sd.out + tt.out + tc.out);
    }
    return mix * 0.2f;
}

}  // namespace em
