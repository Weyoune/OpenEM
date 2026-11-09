// SPDX-License-Identifier: GPL-3.0-only
#include "ui/classic_sound.hpp"

#include "ui/classic_match_view.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace fs = std::filesystem;

namespace em::ui {

// ---------------------------------------------------------------- mixer

void ClassicSoundMixer::start_music() {
    music_.reset();
    if (song_) music_ = std::make_unique<CmfSequencer>(*song_);
}

void ClassicSoundMixer::play_voice(int index) {
    voice_ = index >= 0 && size_t(index) < voices_.size() ? index : -1;
    voice_pos_ = 0.0;
}

void ClassicSoundMixer::start_scene(std::vector<uint8_t> frame_sounds) {
    scene_sounds_ = std::move(frame_sounds);
    scene_ = !voices_.empty();
    scene_time_ = 0.0;
    scene_frame_ = -1;
    play_voice(scene_ ? 0 : -1);  // ovr038:4C25: the loop starts with the scene
}

void ClassicSoundMixer::stop_scene() {
    scene_ = false;
    scene_sounds_.clear();
    play_voice(-1);  // ovr038:4FB2
}

void ClassicSoundMixer::render(float* out, size_t frames, int rate) {
    for (size_t n = 0; n < frames; n++) {
        if (scene_) {
            const long frame = long(scene_time_ / kSceneFrameSeconds);
            if (frame != scene_frame_) {
                scene_frame_ = frame;
                if (size_t(frame) >= scene_sounds_.size()) {
                    stop_scene();
                } else if (const int s = scene_sounds_[size_t(frame)]; s >= 1 && s <= 4) {
                    play_voice(s);  // ovr038:4EC2..4EF1: stop, then KB237+s
                }
            }
            scene_time_ += 1.0 / rate;
            if (scene_ && voice_ < 0) play_voice(0);  // idle voice: the loop is started again
        }
        float v = 0.0f;
        if (voice_ >= 0) {
            const VocSound& s = voices_[size_t(voice_)];
            const size_t end = s.loops ? s.loop_end : s.pcm.size();
            const size_t i = size_t(voice_pos_);
            if (i >= end) {
                if (s.loops && s.loop_end > s.loop_begin) {
                    voice_pos_ = double(s.loop_begin) + (voice_pos_ - double(end));
                } else {
                    play_voice(-1);
                }
            }
            if (voice_ >= 0) {
                const size_t k = size_t(voice_pos_);
                const float a = (int(s.pcm[k]) - 128) / 128.0f;
                const float b = k + 1 < s.pcm.size() ? (int(s.pcm[k + 1]) - 128) / 128.0f : a;
                v = a + (b - a) * float(voice_pos_ - double(k));
                voice_pos_ += double(s.sample_rate) / rate;
            }
        }
        out[n] = v * 0.8f;
    }
    if (music_) {
        music_buffer_.resize(frames);
        const bool more = music_->render(music_buffer_.data(), frames, rate);
        for (size_t n = 0; n < frames; n++) out[n] += music_buffer_[n] * 0.7f;
        if (!more) music_.reset();  // SBFMDRV stops at the end of the song
    }
    for (size_t n = 0; n < frames; n++) out[n] = std::clamp(out[n], -1.0f, 1.0f);
}

std::vector<bool> classic_board_goals(const ClassicMatchFrame& before, const ClassicMatchFrame& after) {
    std::vector<bool> goals;
    if (before.boards.size() != after.boards.size()) return goals;
    for (size_t b = 0; b < after.boards.size(); b++) {
        const ClassicBoard& x = before.boards[b];
        const ClassicBoard& y = after.boards[b];
        if (x.team[0].name != y.team[0].name || x.team[1].name != y.team[1].name) continue;
        for (int side = 0; side < 2; side++)
            for (int g = x.goals[side]; g < y.goals[side]; g++) goals.push_back(side == 0);
    }
    return goals;
}

// ---------------------------------------------------------------- SDL device

ClassicSound* ClassicSound::instance_ = nullptr;

namespace {
template <class F>
auto load(F&& f) -> decltype(f()) {
    try {
        return f();
    } catch (const std::exception&) {
        return {};
    }
}
}  // namespace

ClassicSound::ClassicSound(const fs::path& em_dir) : em_dir_(em_dir) {
    std::vector<VocSound> voices;
    for (int n = kSoundFirstVoc; n < kSoundFirstVoc + 5; n++) {
        auto v = load([&] { return std::optional<VocSound>(decode_voc(read_file(em_dir / "KB" / (std::to_string(n) + ".vga")))); });
        if (!v) {
            voices.clear();
            break;
        }
        voices.push_back(std::move(*v));
    }
    mixer_.set_voices(std::move(voices));
    mixer_.set_music(load([&] { return std::optional<CmfSong>(decode_cmf(read_file(em_dir / "KB" / (std::to_string(kSoundMusic) + ".vga")))); }));

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    SDL_AudioSpec want{}, have{};
    want.freq = rate_;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = &ClassicSound::callback;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!device_) return;
    rate_ = have.freq;
    instance_ = this;
    SDL_PauseAudioDevice(device_, 0);
}

ClassicSound::~ClassicSound() {
    if (instance_ == this) instance_ = nullptr;
    if (device_ && SDL_WasInit(SDL_INIT_AUDIO)) SDL_CloseAudioDevice(device_);
}

void ClassicSound::callback(void* user, uint8_t* stream, int len) {
    auto& self = *static_cast<ClassicSound*>(user);
    const size_t frames = size_t(len) / sizeof(int16_t);
    self.buffer_.resize(frames);
    self.mixer_.render(self.buffer_.data(), frames, self.rate_);
    auto* out = reinterpret_cast<int16_t*>(stream);
    for (size_t n = 0; n < frames; n++) out[n] = int16_t(std::lround(self.buffer_[n] * 32767.0f));
}

void ClassicSound::play_title_music() {
    if (!device_) return;
    SDL_LockAudioDevice(device_);
    mixer_.start_music();
    SDL_UnlockAudioDevice(device_);
}

void ClassicSound::stop_title_music() {
    if (!device_ || title_done_) return;
    title_done_ = true;
    SDL_LockAudioDevice(device_);
    mixer_.stop_music();
    SDL_UnlockAudioDevice(device_);
}

void ClassicSound::play_scene(const std::string& script) {
    if (!device_) return;
    auto scene = load([&] { return std::optional<TorScene>(decode_tor(read_file(em_dir_ / "TORE" / script))); });
    if (!scene) return;
    std::vector<uint8_t> sounds;
    for (const TorFrame& f : scene->frames) sounds.push_back(f.sound);
    SDL_LockAudioDevice(device_);
    mixer_.start_scene(std::move(sounds));
    SDL_UnlockAudioDevice(device_);
}

void ClassicSound::stop_scene() {
    if (!device_) return;
    SDL_LockAudioDevice(device_);
    mixer_.stop_scene();
    SDL_UnlockAudioDevice(device_);
}

void classic_sound_match_frame(const ClassicMatchFrame& before, const ClassicMatchFrame& after) {
    ClassicSound* sound = ClassicSound::instance();
    if (!sound) return;
    // ovr038:4C25 starts the crowd with the first frame of a scene; ovr038:4FB2 stops the voice at its end mark
    if (after.scene && after.scene->frame == 1 && !after.scene->final) sound->play_scene(after.scene->script);
    else if (before.scene && !before.scene->final && (!after.scene || after.scene->final)) sound->stop_scene();
}
}  // namespace em::ui
