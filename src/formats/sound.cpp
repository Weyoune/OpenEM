// SPDX-License-Identifier: GPL-3.0-only
#include "formats/sound.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace em {

// ---------------------------------------------------------------- VOC

VocSound decode_voc(const Bytes& d) {
    static const char kSig[] = "Creative Voice File\x1A";
    if (d.size() < 0x1A || std::memcmp(d.data(), kSig, 20) != 0) throw FormatError("VOC: missing signature");
    const uint16_t version = le16(d, 0x16), check = le16(d, 0x18);
    if (uint16_t(~version + 0x1234) != check) throw FormatError("VOC: bad version check word");
    VocSound v;
    auto set_rate = [&](int rate) {
        if (rate <= 0) throw FormatError("VOC: bad sample rate");
        if (v.sample_rate && v.sample_rate != rate) throw FormatError("VOC: sample rate changes inside the file");
        v.sample_rate = rate;
    };
    size_t pos = le16(d, 0x14);
    size_t repeat_start = 0;
    int repeat_count = -1;  // -1 = no open repeat block
    while (pos < d.size()) {
        const uint8_t type = d[pos];
        if (type == 0) break;
        if (pos + 4 > d.size()) throw FormatError("VOC: truncated block header");
        const size_t len = size_t(d[pos + 1]) | size_t(d[pos + 2]) << 8 | size_t(d[pos + 3]) << 16;
        const size_t body = pos + 4;
        if (body + len > d.size()) throw FormatError("VOC: block runs past the end");
        switch (type) {
        case 1: {  // sound data: time constant, codec, samples
            if (len < 2) throw FormatError("VOC: short sound block");
            if (d[body + 1] != 0) throw FormatError("VOC: only 8-bit PCM is supported");
            set_rate(1000000 / (256 - d[body]));
            v.pcm.insert(v.pcm.end(), d.begin() + long(body + 2), d.begin() + long(body + len));
            break;
        }
        case 2:  // continuation
            v.pcm.insert(v.pcm.end(), d.begin() + long(body), d.begin() + long(body + len));
            break;
        case 3: {  // silence: u16 length - 1, time constant
            if (len < 3) throw FormatError("VOC: short silence block");
            set_rate(1000000 / (256 - d[body + 2]));
            v.pcm.insert(v.pcm.end(), size_t(le16(d, body)) + 1, uint8_t(0x80));
            break;
        }
        case 6:  // repeat start: u16 count (number of further plays, 0xFFFF endless)
            if (len < 2) throw FormatError("VOC: short repeat block");
            repeat_start = v.pcm.size();
            repeat_count = le16(d, body);
            break;
        case 7: {  // repeat end
            if (repeat_count < 0) throw FormatError("VOC: repeat end without start");
            if (repeat_count == 0xFFFF) {
                v.loops = true;
                v.loop_begin = repeat_start;
                v.loop_end = v.pcm.size();
            } else {
                const std::vector<uint8_t> body_pcm(v.pcm.begin() + long(repeat_start), v.pcm.end());
                for (int n = 0; n < repeat_count; n++) v.pcm.insert(v.pcm.end(), body_pcm.begin(), body_pcm.end());
            }
            repeat_count = -1;
            break;
        }
        case 9: {  // new format: u32 rate, u8 bits, u8 channels, u16 codec
            if (len < 12) throw FormatError("VOC: short block 9");
            if (d[body + 4] != 8 || d[body + 5] != 1 || le16(d, body + 6) != 0)
                throw FormatError("VOC: only 8-bit mono PCM is supported");
            set_rate(int(le32(d, body)));
            v.pcm.insert(v.pcm.end(), d.begin() + long(body + 12), d.begin() + long(body + len));
            break;
        }
        case 4:  // marker
        case 5:  // text
            break;
        default:
            throw FormatError("VOC: unsupported block type " + std::to_string(type));
        }
        pos = body + len;
    }
    if (!v.sample_rate) throw FormatError("VOC: no sound data");
    return v;
}

// ---------------------------------------------------------------- CMF

CmfSong decode_cmf(const Bytes& d) {
    if (d.size() < 0x28 || std::memcmp(d.data(), "CTMF", 4) != 0) throw FormatError("CMF: missing signature");
    CmfSong s;
    s.version = le16(d, 4);
    const size_t instruments = le16(d, 6), music = le16(d, 8);
    s.ticks_per_quarter = le16(d, 0x0A);
    s.clock_rate = le16(d, 0x0C);
    if (s.clock_rate <= 0) throw FormatError("CMF: clock rate 0");
    for (int c = 0; c < 16; c++) s.channel_used.push_back(d.at(0x14 + size_t(c)) != 0);
    const size_t count = le16(d, 0x24);
    s.basic_tempo = le16(d, 0x26);
    for (size_t i = 0; i < count; i++) {
        const size_t o = instruments + 16 * i;
        if (o + 16 > d.size()) throw FormatError("CMF: instrument table runs past the end");
        CmfInstrument ins;
        ins.mod_char = d[o];
        ins.car_char = d[o + 1];
        ins.mod_level = d[o + 2];
        ins.car_level = d[o + 3];
        ins.mod_ad = d[o + 4];
        ins.car_ad = d[o + 5];
        ins.mod_sr = d[o + 6];
        ins.car_sr = d[o + 7];
        ins.mod_wave = d[o + 8];
        ins.car_wave = d[o + 9];
        ins.feedback = d[o + 10];
        s.instruments.push_back(ins);
    }
    if (s.instruments.empty()) throw FormatError("CMF: no instruments");

    size_t pos = music;
    auto varlen = [&]() {
        uint32_t v = 0;
        for (int n = 0; n < 4; n++) {
            const uint8_t b = d.at(pos++);
            v = v << 7 | (b & 0x7F);
            if (!(b & 0x80)) return v;
        }
        throw FormatError("CMF: variable length value too long");
    };
    uint32_t tick = 0;
    uint8_t running = 0;
    for (;;) {
        if (pos >= d.size()) throw FormatError("CMF: music ends without end-of-track");
        tick += varlen();
        uint8_t status = d.at(pos);
        if (status & 0x80) {
            pos++;
        } else {
            if (!running) throw FormatError("CMF: running status without a previous status");
            status = running;
        }
        if (status == 0xFF) {
            const uint8_t type = d.at(pos++);
            const uint32_t len = varlen();
            pos += len;
            if (type == 0x2F) {
                s.events.push_back({tick, 0xFF, 0x2F, 0});
                break;
            }
            continue;
        }
        if (status == 0xF0 || status == 0xF7) {  // system exclusive
            pos += varlen();
            continue;
        }
        if (status >= 0xF0) throw FormatError("CMF: unsupported system message");
        running = status;
        CmfEvent e{tick, status, d.at(pos++), 0};
        const uint8_t kind = status & 0xF0;
        if (kind != 0xC0 && kind != 0xD0) e.data2 = d.at(pos++);
        s.events.push_back(e);
    }
    return s;
}

// ---------------------------------------------------------------- sequencer

namespace {
constexpr int kModOffset[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
// SBFMDRV fnum table (seg011:0305): 768 entries for one octave, 64 per semitone, from 0x157 (C) up to 0x2AD.
int driver_fnum(int fine) { return int(343.4 * std::exp2(fine / 768.0)); }
// SBFMDRV level for a note-on velocity (added to the carrier total level).
int velocity_attenuation(int velocity) {
    const int a = int(0x25 - std::sqrt(16.0 * velocity));
    return std::clamp(a, 0, 0x3F);
}
}  // namespace

CmfSequencer::CmfSequencer(const CmfSong& song) : song_(song) {
    opl_.reset();
    write(0x01, 0x20);  // waveform select on
    write(0x08, 0x00);
    write(0xBD, 0x00);
    for (int c = 0; c < 16; c++) program_[c] = c % int(song_.instruments.size());
}

void CmfSequencer::write(int reg, int value) { opl_.write(reg, value); }

void CmfSequencer::set_instrument(int voice, const CmfInstrument& ins, int velocity) {
    const int m = kModOffset[voice], k = m + 3;
    const int car_tl = std::min(0x3F, (ins.car_level & 0x3F) + velocity_attenuation(velocity));
    write(0x20 + m, ins.mod_char);
    write(0x20 + k, ins.car_char);
    write(0x40 + m, ins.mod_level);
    write(0x40 + k, (ins.car_level & 0xC0) | car_tl);
    write(0x60 + m, ins.mod_ad);
    write(0x60 + k, ins.car_ad);
    write(0x80 + m, ins.mod_sr);
    write(0x80 + k, ins.car_sr);
    write(0xE0 + m, ins.mod_wave);
    write(0xE0 + k, ins.car_wave);
    write(0xC0 + voice, ins.feedback);
}

void CmfSequencer::set_frequency(int voice, int note, int ch, bool key) {
    // SBFMDRV: block from the note table at seg011:0285 (block = note / 12 - 1, 0..7), fine pitch with transposition
    int fine = note * 64 + transpose_[ch] / 2;  // transpose in 1/128 semitone
    fine = std::clamp(fine, 0, 127 * 64);
    const int semitone_fine = fine % 768;
    const int block = std::clamp(fine / 768 - 1, 0, 7);
    const int fnum = driver_fnum(semitone_fine);
    write(0xA0 + voice, fnum & 0xFF);
    write(0xB0 + voice, (key ? 0x20 : 0) | block << 2 | (fnum >> 8 & 3));
}

void CmfSequencer::rhythm_note(int ch, int note, int velocity, bool on) {
    static constexpr int kBit[5] = {0x10, 0x08, 0x04, 0x02, 0x01};    // BD SD TT CY HH
    static constexpr int kChannel[5] = {6, 7, 8, 8, 7};
    static constexpr int kSlot[5] = {-1, 0x14, 0x12, 0x15, 0x11};       // single operator of the instrument
    const int r = ch - 11;
    if (!on) {
        bd_ &= uint8_t(~kBit[r]);
        write(0xBD, bd_);
        return;
    }
    const CmfInstrument& ins = song_.instruments[size_t(program_[ch])];
    if (r == 0) {
        set_instrument(6, ins, velocity);
    } else {
        // one operator: the instrument's modulator bytes (the carrier halves of the original's percussion instruments
        // hold the same default values)
        const int s = kSlot[r];
        const int tl = std::min(0x3F, (ins.mod_level & 0x3F) + velocity_attenuation(velocity));
        write(0x20 + s, ins.mod_char);
        write(0x40 + s, (ins.mod_level & 0xC0) | tl);
        write(0x60 + s, ins.mod_ad);
        write(0x80 + s, ins.mod_sr);
        write(0xE0 + s, ins.mod_wave);
    }
    set_frequency(kChannel[r], note, ch, false);
    bd_ &= uint8_t(~kBit[r]);
    write(0xBD, bd_);  // retrigger
    bd_ |= uint8_t(kBit[r]);
    write(0xBD, bd_);
}

void CmfSequencer::note_on(int ch, int note, int velocity) {
    if (velocity == 0) return note_off(ch, note);
    if (rhythm_ && ch >= 11) return rhythm_note(ch, note, velocity, true);
    const int count = rhythm_ ? 6 : 9;
    int pick = -1;
    for (int v = 0; v < count; v++)
        if (voices_[v].on && voices_[v].channel == ch && voices_[v].note == note) pick = v;
    if (pick < 0) {  // the free voice released longest ago, else the oldest sounding one
        for (int v = 0; v < count; v++)
            if (!voices_[v].on && (pick < 0 || voices_[v].stamp < voices_[pick].stamp)) pick = v;
    }
    if (pick < 0) {
        for (int v = 0; v < count; v++)
            if (pick < 0 || voices_[v].stamp < voices_[pick].stamp) pick = v;
    }
    Voice& v = voices_[pick];
    if (v.on) write(0xB0 + pick, opl_.reg(0xB0 + pick) & ~0x20);
    set_instrument(pick, song_.instruments[size_t(program_[ch])], velocity);
    set_frequency(pick, note, ch, true);
    v = {ch, note, true, ++stamp_};
}

void CmfSequencer::note_off(int ch, int note) {
    if (rhythm_ && ch >= 11) return rhythm_note(ch, note, 0, false);
    for (int v = 0; v < 9; v++) {
        if (voices_[v].on && voices_[v].channel == ch && voices_[v].note == note) {
            write(0xB0 + v, opl_.reg(0xB0 + v) & ~0x20);
            voices_[v].on = false;
            voices_[v].stamp = ++stamp_;
        }
    }
}

void CmfSequencer::event(const CmfEvent& e) {
    const int ch = e.status & 0x0F;
    switch (e.status & 0xF0) {
    case 0x80: note_off(ch, e.data1); break;
    case 0x90: note_on(ch, e.data1, e.data2); break;
    case 0xC0: program_[ch] = e.data1 % int(song_.instruments.size()); break;
    case 0xB0:
        if (e.data1 == 0x67) {  // rhythm mode
            rhythm_ = e.data2 != 0;
            bd_ = rhythm_ ? 0x20 : 0;
            write(0xBD, bd_);
        } else if (e.data1 == 0x68) {
            transpose_[ch] = e.data2;
        } else if (e.data1 == 0x69) {
            transpose_[ch] = -int(e.data2);
        }
        break;
    case 0xF0:
        if (e.status == 0xFF && e.data1 == 0x2F) {
            done_ = true;
            for (int v = 0; v < 9; v++) write(0xB0 + v, opl_.reg(0xB0 + v) & ~0x20);
            bd_ &= 0x20;
            write(0xBD, bd_);
        }
        break;
    default: break;  // aftertouch, pitch bend: not handled by SBFMDRV's CMF player
    }
}

void CmfSequencer::run_to(uint32_t tick) {
    while (next_ < song_.events.size() && song_.events[next_].tick <= tick) event(song_.events[next_++]);
    tick_ = tick;
}

bool CmfSequencer::render(float* out, size_t frames, int rate) {
    const double ticks_per_sample = double(song_.clock_rate) / rate;
    const double chip_per_sample = double(Opl2::kRate) / rate;
    for (size_t n = 0; n < frames; n++) {
        if (!done_) {
            tick_frac_ += ticks_per_sample;
            while (tick_frac_ >= 1.0) {
                tick_frac_ -= 1.0;
                run_to(tick_ + 1);
            }
        }
        opl_frac_ += chip_per_sample;
        while (opl_frac_ >= 1.0) {
            opl_frac_ -= 1.0;
            prev_ = cur_;
            cur_ = opl_.sample();
        }
        out[n] = prev_ + (cur_ - prev_) * float(opl_frac_);
    }
    if (!done_) return true;
    // after the end: keep going while release tails are audible, for at most a second
    tail_ += frames;
    if (tail_ > size_t(rate)) return false;
    for (size_t n = 0; n < frames; n++)
        if (std::fabs(out[n]) > 1e-4f) return true;
    return false;
}

}  // namespace em
