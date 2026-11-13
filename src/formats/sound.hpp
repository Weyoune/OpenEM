// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Sound files of the original:
//   KB\237..241.vga  Creative Voice files (VOC 1.10), 8-bit unsigned mono PCM, played by the CT-VOICE driver built into
//                    EM.EXE, only inside the goal scenes: 237 = background loop (repeat block 0xFFFF),
//                    238..241 = frame samples 1..4 (TORE frame byte +3).
//   KB\248.vga       Creative Music File (CMF 1.01), FM music for the SBFMDRV driver built into EM.EXE, played on the
//                    title screen: instruments at +6, music at +8, timer rate at +0x0C.
// The decoders only parse; Opl2 (formats/opl2.hpp) and CmfSequencer turn the music into samples without any SDL.
#include <cstdint>
#include <string>
#include <vector>

#include "formats/binary.hpp"
#include "formats/opl2.hpp"

namespace em {

// One VOC file flattened to PCM. Finite repeat blocks (type 6/7) are expanded; an endless repeat (count 0xFFFF)
// keeps its body once and marks it with loop_begin/loop_end (sample indices into pcm).
struct VocSound {
    int sample_rate = 0;         // from the time constant: 1000000 / (256 - tc)
    std::vector<uint8_t> pcm;    // unsigned 8-bit, 0x80 = silence
    bool loops = false;          // endless repeat present
    size_t loop_begin = 0, loop_end = 0;
};

// Parses a VOC file (block types 1-7 and 9). Throws FormatError for a bad signature or check word, truncated blocks,
// anything but 8-bit mono PCM, a sample rate that changes inside the file, or a file without sound data.
VocSound decode_voc(const Bytes& data);

// CMF instrument: the 11 OPL register bytes in file order.
struct CmfInstrument {
    uint8_t mod_char = 0, car_char = 0;      // registers 0x20 (AM/VIB/EG/KSR/MULT)
    uint8_t mod_level = 0, car_level = 0;    // 0x40 (KSL/TL)
    uint8_t mod_ad = 0, car_ad = 0;          // 0x60
    uint8_t mod_sr = 0, car_sr = 0;          // 0x80
    uint8_t mod_wave = 0, car_wave = 0;      // 0xE0
    uint8_t feedback = 0;                    // 0xC0 (feedback << 1 | connection)
};

// MIDI-style event of the music block; tick is absolute in timer ticks (clock_rate per second).
struct CmfEvent {
    uint32_t tick = 0;
    uint8_t status = 0;          // 0x80..0xEF channel messages, 0xFF meta (only end of track is kept)
    uint8_t data1 = 0, data2 = 0;  // data2 = 0 for program change and channel pressure
};

// A parsed CMF file (header fields at the given offsets).
struct CmfSong {
    int version = 0;              // 0x0101
    int ticks_per_quarter = 0;    // +0x0A (not used for timing by SBFMDRV)
    int clock_rate = 0;           // +0x0C ticks per second (the game sets 1193180 / rate as timer divisor)
    std::vector<bool> channel_used;   // +0x14, 16 bytes
    int basic_tempo = 0;          // +0x26
    std::vector<CmfInstrument> instruments;
    std::vector<CmfEvent> events;  // ends with the end-of-track meta event (status 0xFF, data1 0x2F)
    double seconds() const { return events.empty() || clock_rate <= 0 ? 0.0 : double(events.back().tick) / clock_rate; }  // length
};

// Parses header, instrument table (16 bytes per instrument) and the MIDI-style music block up to the end-of-track
// event (running status supported, other meta and system-exclusive events skipped). Throws FormatError on malformed data.
CmfSong decode_cmf(const Bytes& data);

// Plays a CmfSong on an Opl2 like SBFMDRV: 9 melodic voices (6 plus the five rhythm instruments once controller
// 0x67 switches rhythm mode on; MIDI channels 11..15 = bass drum, snare, tom, cymbal, hi-hat), voices allocated to
// notes, program changes choose instruments, controllers 0x68/0x69 transpose. The song stops at its end like the
// driver (no loop).
class CmfSequencer {
public:
    explicit CmfSequencer(const CmfSong& song);  // keeps a reference: `song` must outlive the sequencer
    // Renders mono samples at `rate` Hz into out (adds nothing, overwrites). Returns false once the song has ended
    // and the output is silent.
    bool render(float* out, size_t frames, int rate);
    bool finished() const { return done_; }
    const Opl2& chip() const { return opl_; }
    // Applies all events up to (and including) `tick` without rendering (tests).
    void run_to(uint32_t tick);

private:
    // One melodic OPL channel as allocated to a note.
    struct Voice {
        int channel = -1, note = -1;  // MIDI channel and note it plays
        bool on = false;              // key held
        uint64_t stamp = 0;           // allocation order (last note-on or note-off)
    };
    void event(const CmfEvent& e);  // applies one event (note on/off, program change, controllers 0x67..0x69, end)
    // Allocates a voice: the one already playing this note, else the free voice released longest ago, else the
    // oldest sounding one. Velocity 0 is a note-off.
    void note_on(int ch, int note, int velocity);
    void note_off(int ch, int note);
    // Writes the 11 instrument bytes to the voice's operators; the velocity attenuation is added to the carrier level.
    void set_instrument(int voice, const CmfInstrument& ins, int velocity);
    // Frequency registers of a voice for a MIDI note plus the channel transposition, with or without key-on.
    void set_frequency(int voice, int note, int ch, bool key);
    void rhythm_note(int ch, int note, int velocity, bool on);  // MIDI channels 11..15 in rhythm mode
    void write(int reg, int value);

    const CmfSong& song_;
    Opl2 opl_;
    size_t next_ = 0;       // next event to apply
    uint32_t tick_ = 0;     // current tick
    double tick_frac_ = 0.0;       // fraction of a tick already rendered
    double opl_frac_ = 0.0;        // resampler position between chip samples
    float prev_ = 0.0f, cur_ = 0.0f;  // the two chip samples the output interpolates between
    bool done_ = false, rhythm_ = false;
    size_t tail_ = 0;              // samples rendered after the end (release tails, at most one second)
    uint8_t bd_ = 0;               // register 0xBD
    int program_[16] = {};         // instrument per MIDI channel
    int transpose_[16] = {};       // in 1/128 semitones
    Voice voices_[9];
    uint64_t stamp_ = 0;
};

}  // namespace em
