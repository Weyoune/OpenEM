// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Small YM3812 (OPL2) model for the CMF music of the original (KB\248, played through SBFMDRV on the AdLib part of the
// Sound Blaster). It follows the register map and the documented behaviour of the chip: 9 two-operator channels,
// phase generator (fnum << block, multiplier table), envelope generator in 0.1875 dB steps with the rate and key-scale
// rules, KSL, tremolo/vibrato, four waveforms, feedback, FM/AM connection and the rhythm mode (bass drum, snare, tom,
// cymbal, hi-hat with the noise generator). Timing of the envelope rates uses the data-sheet times; it is not a
// cycle-exact emulation, which the music does not need.
#include <array>
#include <cstdint>

namespace em {

class Opl2 {
public:
    static constexpr int kRate = 49716;  // 3.579545 MHz / 72

    Opl2() { reset(); }
    void reset();
    void write(int reg, int value);
    uint8_t reg(int r) const { return regs_[size_t(r & 0xFF)]; }
    // One output sample at kRate, roughly -1..1 for a single full-scale voice.
    float sample();
    // Envelope attenuation of an operator (register offset 0x00..0x15) in dB; 96 = silent.
    double attenuation_db(int offset) const;
    bool channel_keyed(int ch) const { return ch_[size_t(ch)].key; }

private:
    enum class Env : uint8_t { Off, Attack, Decay, Sustain, Release };
    struct Op {
        bool am = false, vib = false, egt = false, ksr = false;
        int mult = 0, ksl = 0, tl = 0, ar = 0, dr = 0, sl = 0, rr = 0, wave = 0;
        uint32_t phase = 0;
        Env env = Env::Off;
        double att = 511.0;       // 0 = loudest .. 511 = silent, 0.1875 dB per unit
        float out = 0.0f, prev = 0.0f;
        bool key_normal = false, key_rhythm = false;
    };
    struct Channel {
        int fnum = 0, block = 0, fb = 0;
        bool key = false, cnt = false;
    };
    void key(Op& op, bool normal, bool on);
    void step_envelope(Op& op, const Channel& c);
    uint32_t phase_inc(const Op& op, const Channel& c) const;
    float level(const Op& op, const Channel& c) const;  // total attenuation -> linear gain
    static float wave(int w, int index);
    Op& slot_of(int ch, int which);  // which 0 = modulator, 1 = carrier

    std::array<uint8_t, 256> regs_{};
    std::array<Op, 22> op_{};        // indexed by register offset 0x00..0x15
    std::array<Channel, 9> ch_{};
    bool wave_select_ = false, nts_ = false, rhythm_ = false, deep_am_ = false, deep_vib_ = false;
    double am_phase_ = 0.0, vib_phase_ = 0.0, am_value_ = 0.0, vib_factor_ = 1.0;
    uint32_t noise_ = 1;
};

}  // namespace em
