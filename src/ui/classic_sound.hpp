// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Sound of the classic frontend: a small software mixer for the game's VOC samples and CMF music
// (formats/sound.hpp) and its SDL audio output.
//
// Where the original plays sound:
//   - Title screen: KB\197 is shown, KB\248.vga (CMF) is loaded and started through SBFMDRV, the program waits
//     for a key or click, then stops the music. OpenEM shows the title (ClassicApplication::show_title); the
//     music starts with the window and stops at the first key or click.
//   - Goal scenes: with the CT-VOICE driver present the scene starts KB\237 (an endless loop, crowd) and restarts
//     it whenever the voice is idle; a frame whose byte +3 is s = 1..4 stops the voice and plays KB\(237 + s); the
//     end of the scene stops the voice. One frame lasts one BIOS tick. The Sound Blaster plays one voice at a time,
//     so a frame sample interrupts the loop. OpenEM starts the track with the first frame of a drawn goal scene
//     (ClassicMatchFrame::scene, classic_goal_scene.cpp) and stops it after the last one.
// The original game has no other sound.
#include "formats/sound.hpp"
#include "formats/tore.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace em::ui {

struct ClassicMatchFrame;

constexpr int kSoundFirstVoc = 237;                    // KB\237..241: crowd loop and the four scene samples
constexpr int kSoundMusic = 248;                       // KB\248: title music (CMF)
constexpr double kSceneFrameSeconds = 65536.0 / 1193182.0;  // one BIOS tick (about 54.9 ms)

// SDL-free mixer: one voice channel like the CT-VOICE driver, plus the music.
class ClassicSoundMixer {
public:
    void set_voices(std::vector<VocSound> voices) { voices_ = std::move(voices); }  // 237, 238..241
    void set_music(std::optional<CmfSong> song) { song_ = std::move(song); }
    // Starts the title music from the beginning (no effect without a song).
    void start_music();
    void stop_music() { music_.reset(); }
    bool music_playing() const { return music_ != nullptr; }
    // Goal scene: frame_sounds[i] = byte +3 of frame i. Starts the crowd loop with the first frame.
    void start_scene(std::vector<uint8_t> frame_sounds);
    // Ends the scene and stops the voice.
    void stop_scene();
    bool scene_active() const { return scene_; }
    int current_voice() const { return voice_; }  // -1 idle, else 0 = KB237 .. 4 = KB241
    // Mono output at `rate` Hz, overwritten.
    void render(float* out, size_t frames, int rate);

private:
    // Starts voice index (0 = KB237 .. 4 = KB241) from its beginning; -1 stops the voice.
    void play_voice(int index);
    std::vector<VocSound> voices_;
    std::optional<CmfSong> song_;
    std::unique_ptr<CmfSequencer> music_;  // running music, null when stopped
    std::vector<uint8_t> scene_sounds_;    // byte +3 of every scene frame
    bool scene_ = false;                   // a goal scene is running
    double scene_time_ = 0.0;              // seconds since the scene started
    long scene_frame_ = -1;                // last scene frame whose sound byte was handled
    int voice_ = -1;                       // playing voice, -1 idle
    double voice_pos_ = 0.0;               // position in the voice, in source samples
    std::vector<float> music_buffer_;      // scratch buffer for the music
};

// Goals of matches with a board between two consecutive match frames: one entry per goal, true = home side scored.
std::vector<bool> classic_board_goals(const ClassicMatchFrame& before, const ClassicMatchFrame& after);

// SDL audio output. Missing data or no audio device leave it silent; it never throws.
class ClassicSound {
public:
    explicit ClassicSound(const std::filesystem::path& em_dir);
    ~ClassicSound();
    ClassicSound(const ClassicSound&) = delete;
    ClassicSound& operator=(const ClassicSound&) = delete;
    bool available() const { return device_ != 0; }
    void play_title_music();
    void stop_title_music();
    // The sound track of a TORE\ script (ClassicMatchFrame::scene), one frame per BIOS tick; stop_scene ends it.
    void play_scene(const std::string& script);
    void stop_scene();
    static ClassicSound* instance() { return instance_; }

private:
    static void callback(void* user, uint8_t* stream, int len);
    std::filesystem::path em_dir_;
    ClassicSoundMixer mixer_;
    uint32_t device_ = 0;
    int rate_ = 44100;
    bool title_done_ = false;
    std::vector<float> buffer_;
    static ClassicSound* instance_;
};

// Hook for the match display: called when the shown frame changes. Plays the goal-scene sound for board goals when
// the Torszenen switch is on and a ClassicSound exists; does nothing otherwise.
void classic_sound_match_frame(const ClassicMatchFrame& before, const ClassicMatchFrame& after);

}  // namespace em::ui
