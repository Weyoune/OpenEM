// SPDX-License-Identifier: GPL-3.0-only
// Sound tests (docs/formats/SOUND.md). Without arguments: VOC/CMF decoders, OPL2 model and the goal-scene mixer on
// synthetic data. With the original EM directory: KB\237..241 and KB\248 decode and the music renders to its end.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "formats/sound.hpp"
#include "ui/classic_match_view.hpp"
#include "ui/classic_sound.hpp"

using namespace em;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                                   \
        }                                                                                 \
    } while (0)

static Bytes voc_header() {
    Bytes b(0x1A, 0);
    std::memcpy(b.data(), "Creative Voice File\x1A", 20);
    b[0x14] = 0x1A;
    b[0x16] = 0x0A;
    b[0x17] = 0x01;
    const int check = ((0xFFFF & ~0x010A) + 0x1234) & 0xFFFF;  // ~version + 0x1234
    b[0x18] = uint8_t(check & 0xFF);
    b[0x19] = uint8_t(check >> 8);
    return b;
}
static void block(Bytes& b, uint8_t type, const Bytes& body) {
    b.push_back(type);
    b.push_back(uint8_t(body.size()));
    b.push_back(uint8_t(body.size() >> 8));
    b.push_back(uint8_t(body.size() >> 16));
    b.insert(b.end(), body.begin(), body.end());
}

static void voc_units() {
    // sound block, finite repeat (2 further plays), silence, end
    Bytes b = voc_header();
    block(b, 1, {0x9C, 0x00, 1, 2});  // tc 0x9C -> 10000 Hz
    block(b, 6, {2, 0});
    block(b, 2, {3, 4});
    block(b, 7, {});
    block(b, 3, {1, 0, 0x9C});
    b.push_back(0);
    const VocSound v = decode_voc(b);
    CHECK(v.sample_rate == 10000);
    CHECK((v.pcm == std::vector<uint8_t>{1, 2, 3, 4, 3, 4, 3, 4, 0x80, 0x80}));
    CHECK(!v.loops);

    // endless repeat like KB\237
    Bytes e = voc_header();
    block(e, 6, {0xFF, 0xFF});
    block(e, 1, {0x2A, 0x00, 10, 20, 30});
    block(e, 7, {});
    e.push_back(0);
    const VocSound l = decode_voc(e);
    CHECK(l.sample_rate == 1000000 / (256 - 0x2A));
    CHECK(l.loops && l.loop_begin == 0 && l.loop_end == 3);

    Bytes bad = voc_header();
    bad[0x18] ^= 1;
    bool threw = false;
    try {
        decode_voc(bad);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

static Bytes cmf_file() {
    Bytes b(0x28, 0);
    std::memcpy(b.data(), "CTMF", 4);
    b[4] = 1;
    b[5] = 1;
    b[6] = 0x28;            // instruments
    b[8] = 0x28 + 16;       // music
    b[0x0A] = 30;
    b[0x0C] = 96;           // ticks per second
    b[0x14] = 1;            // channel 0 used
    b[0x24] = 1;            // one instrument
    const Bytes ins = {0x21, 0x21, 0x10, 0x00, 0xF0, 0xF0, 0x00, 0x0F, 0, 0, 0x06, 0, 0, 0, 0, 0};  // sustained
    b.insert(b.end(), ins.begin(), ins.end());
    const Bytes music = {0x00, 0xC0, 0x00,         // program 0
                         0x00, 0x90, 60, 0x7F,     // note on C4
                         0x60, 60, 0x00,           // 96 ticks later: running status, velocity 0 = off
                         0x81, 0x40, 0xFF, 0x2F, 0x00};  // 192 ticks later: end of track
    b.insert(b.end(), music.begin(), music.end());
    return b;
}

static void cmf_units() {
    const CmfSong s = decode_cmf(cmf_file());
    CHECK(s.version == 0x0101 && s.clock_rate == 96 && s.ticks_per_quarter == 30);
    CHECK(s.instruments.size() == 1 && s.instruments[0].feedback == 0x06 && s.instruments[0].car_sr == 0x0F);
    CHECK(s.channel_used.size() == 16 && s.channel_used[0] && !s.channel_used[1]);
    CHECK(s.events.size() == 4);
    CHECK(s.events[1].status == 0x90 && s.events[1].data1 == 60 && s.events[1].tick == 0);
    CHECK(s.events[2].status == 0x90 && s.events[2].data2 == 0 && s.events[2].tick == 96);
    CHECK(s.events.back().status == 0xFF && s.events.back().tick == 96 + 192);
    CHECK(std::fabs(s.seconds() - 3.0) < 1e-9);

    // the driver's pitch: note 60 -> block 4, fnum 0x157 (seg011:0285 / 0305), keyed on the first voice
    CmfSequencer seq(s);
    seq.run_to(0);
    CHECK(seq.chip().channel_keyed(0));
    CHECK(seq.chip().reg(0xA0) == 0x57);
    CHECK((seq.chip().reg(0xB0) & 0x1F) == (4 << 2 | 1));
    CHECK((seq.chip().reg(0x43) & 0x3F) == 0);  // velocity 127 -> no extra attenuation
    seq.run_to(96);
    CHECK(!seq.chip().channel_keyed(0));

    // rendering: sound while the note is held, silence after the end
    CmfSequencer play(s);
    std::vector<float> buf(44100);
    CHECK(play.render(buf.data(), buf.size(), 44100));
    double energy = 0;
    for (float f : buf) energy += f * f;
    CHECK(energy > 1.0);
    bool more = true;
    for (int n = 0; n < 10 && more; n++) more = play.render(buf.data(), buf.size(), 44100);
    CHECK(!more && play.finished());
}

static void opl_units() {
    Opl2 opl;
    opl.write(0x20, 0x01);
    opl.write(0x23, 0x21);  // EGT: sustained
    opl.write(0x40, 0x3F);  // modulator silent
    opl.write(0x43, 0x00);
    opl.write(0x60, 0xF4);
    opl.write(0x63, 0xF4);
    opl.write(0x80, 0x0F);
    opl.write(0x83, 0x2F);  // carrier sustain level 2 (6 dB), fast release
    opl.write(0xA0, 0x57);
    opl.write(0xB0, 0x20 | 4 << 2 | 1);  // ~260 Hz, key on
    // count zero crossings over one second: pure carrier sine at fnum 0x157 block 4
    int crossings = 0;
    float last = 0;
    for (int n = 0; n < Opl2::kRate; n++) {
        const float s = opl.sample();
        if ((last < 0) != (s < 0)) crossings++;
        last = s;
    }
    const double hz = crossings / 2.0;
    CHECK(std::fabs(hz - 0x157 * double(Opl2::kRate) / (1 << 16)) < 3.0);
    CHECK(std::fabs(opl.attenuation_db(0x03) - 6.0) < 0.01);  // EGT 1: holds at the sustain level
    opl.write(0xB0, 4 << 2 | 1);  // key off
    for (int n = 0; n < Opl2::kRate / 2; n++) opl.sample();
    CHECK(opl.attenuation_db(0x03) >= 95.8);  // 511 x 0.1875 dB
}

static void mixer_units() {
    using namespace em::ui;
    ClassicSoundMixer m;
    std::vector<VocSound> voices(5);
    for (int i = 0; i < 5; i++) {
        voices[i].sample_rate = 1000;
        voices[i].pcm.assign(i == 0 ? 50 : 20, uint8_t(0x90 + i));
    }
    voices[0].loops = true;
    voices[0].loop_end = 50;
    m.set_voices(voices);
    // scene: frame 2 plays sample 3 (KB240); the loop resumes after it; the scene ends after 4 frames
    m.start_scene({0, 0, 3, 0});
    CHECK(m.scene_active() && m.current_voice() == 0);
    const int rate = 10000;
    const size_t frame = size_t(kSceneFrameSeconds * rate);
    std::vector<float> out(frame * 2 + 10);
    m.render(out.data(), out.size(), rate);
    CHECK(m.current_voice() == 3);
    CHECK(out[0] > 0.0f);
    out.resize(size_t(rate * 0.025));  // the 20 ms sample has ended: loop again
    m.render(out.data(), out.size(), rate);
    CHECK(m.current_voice() == 0);
    out.resize(frame * 2);
    m.render(out.data(), out.size(), rate);
    CHECK(!m.scene_active() && m.current_voice() == -1);

    ClassicMatchFrame a, b;
    a.boards.resize(1);
    a.boards[0].team[0].name = "A";
    a.boards[0].team[1].name = "B";
    b = a;
    b.boards[0].goals[1] = 1;
    const auto goals = classic_board_goals(a, b);
    CHECK(goals.size() == 1 && !goals[0]);
    CHECK(classic_board_goals(a, a).empty());
}

static void original(const fs::path& em) {
    const int rates[5] = {1000000 / (256 - 0x2A), 1000000 / (256 - 0x2A), 1000000 / (256 - 0xAC), 1000000 / (256 - 0x65),
                          1000000 / (256 - 0x65)};
    const size_t sizes[5] = {9148 - 2, 0x1330 - 2, 0x23EC - 2, 0x038C - 2, 0x0A10 - 2};
    for (int i = 0; i < 5; i++) {
        const VocSound v = decode_voc(read_file(em / "KB" / (std::to_string(237 + i) + ".vga")));
        CHECK(v.sample_rate == rates[i]);
        CHECK(v.pcm.size() == sizes[i]);
        CHECK(v.loops == (i == 0));
    }
    const CmfSong s = decode_cmf(read_file(em / "KB" / "248.vga"));
    CHECK(s.clock_rate == 96 && s.ticks_per_quarter == 30 && s.instruments.size() == 14);
    CHECK(s.events.size() > 1000);
    CHECK(s.events[0].status == 0xB0 && s.events[0].data1 == 0x67 && s.events[0].data2 == 1);  // rhythm mode
    const double secs = s.seconds();
    std::printf("KB248: %zu events, %.1f s\n", s.events.size(), secs);
    CHECK(secs > 10.0 && secs < 600.0);
    CmfSequencer seq(s);
    std::vector<float> buf(22050);
    double energy = 0, peak = 0;
    size_t blocks = 0;
    while (seq.render(buf.data(), buf.size(), 22050) && blocks < 600) {
        for (float f : buf) {
            energy += f * f;
            peak = std::max(peak, double(std::fabs(f)));
        }
        blocks++;
    }
    std::printf("KB248 render: %zu s, rms %.3f, peak %.3f\n", blocks, std::sqrt(energy / (blocks * 22050.0)), peak);
    CHECK(seq.finished());
    CHECK(double(blocks) >= secs - 1.0 && double(blocks) <= secs + 2.0);  // one block = 1 s; at most 1 s of tail
    CHECK(energy > 100.0);
    CHECK(peak < 1.5);
}

int main(int argc, char** argv) {
    try {
        voc_units();
        cmf_units();
        opl_units();
        mixer_units();
        if (argc > 1) original(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        return 1;
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
    return failures ? 1 : 0;
}
