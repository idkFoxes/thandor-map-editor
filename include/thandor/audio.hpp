#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace thandor {

struct SamPcmData {
    static constexpr uint32_t kSampleRate = 22050U;
    static constexpr uint32_t kChannels = 2U;
    std::vector<int16_t> interleaved_stereo;

    std::size_t frame_count() const noexcept { return interleaved_stereo.size() / kChannels; }
    bool empty() const noexcept { return interleaved_stereo.empty(); }
};

// Retail level sound-table identity is the trailing decimal number immediately before .sam on an
// entry matched by LEV LevelPaths::sounds (stock: sound/sound??, with the loader forcing .sam).
// There is deliberately no fallback from this value to SOUND.PCK entry order or the separate
// DirectSound voice-set registry.
std::optional<uint32_t> sam_trailing_decimal_index(std::string_view path) noexcept;

// Portable transcription of the live sample-voice SAM path. game.exe binds g_SoundCreateSampleVoiceSet
// to 0x00583490; that owner configures PCM stereo 22050 Hz / 16-bit / block-align 4 and calls the
// stereo MMX synthesis owner at 0x00418560. A separate mono MMX sibling exists but is not selected
// by this live dispatch path and must not override it merely because an older note called it SFX.
SamPcmData decode_sam_to_pcm(std::span<const std::byte> data);

// Retail projected audio is mixed in Q15 after transforming the source into the current camera
// listener basis. Distance uses the source's serialized maximum-distance Q12; the radial envelope
// is the first quarter of the fixed cosine table and stereo crossfeed is cos^2(azimuth). Sources
// directly in front/behind are centered, sources at +/-90 degrees isolate to the corresponding
// channel, and the optional reverse-stereo flag swaps the final channels.
struct SpatialAudioListener {
    std::array<float, 3> position{};
    std::array<float, 3> right{1.0F, 0.0F, 0.0F};
    std::array<float, 3> forward{0.0F, 1.0F, 0.0F};
};

// FrontendModelPointerContext_RenderWorldViewQueuesClipped uses target XY and
// target Z + (committed camera distance Q12 >> 2), not the visual camera eye.
std::array<float,3> spatial_sound_listener_position(std::array<float,3> target,
    int32_t camera_distance_q12) noexcept;

struct SpatialSoundGainsQ15 {
    uint32_t left_q15{};
    uint32_t right_q15{};
    bool audible() const noexcept { return left_q15 != 0U || right_q15 != 0U; }
};

SpatialSoundGainsQ15 spatial_sound_gains_q15(
    const SpatialAudioListener& listener, std::array<float, 3> source_position,
    uint32_t source_gain_q15, uint32_t maximum_distance_q12,
    uint32_t effects_gain_q15 = 0x8000U, bool reverse_stereo = false) noexcept;

struct AudioPlaybackDiagnostics {
    uint64_t one_shots_queued{},one_shots_completed{},one_shots_failed{};
};
// Completion/failure is recorded by the actual host thread, not inferred from dlopen success.
AudioPlaybackDiagnostics audio_playback_diagnostics() noexcept;

// Fire-and-forget host playback for editor previews. Linux shares one bounded software mixer
// and PCM device across loops/one-shots, with five-second device-failure backoff.
// The gain overload applies independent Q15
// channels in software before the host API sees PCM, so simultaneous projected sources do not
// share a device-global volume control. True means queued to a host thread, not confirmed audible.
// audio_playback_diagnostics() records actual host completion/failure. Failure is non-fatal.
bool play_pcm_one_shot(const SamPcmData& pcm, uint32_t left_gain_q15,
                       uint32_t right_gain_q15);
bool play_pcm_one_shot(const SamPcmData& pcm);

// Persistent projected channels in game.exe are looping voices. The retail spatial-sound pool
// clears desired gains each frame, lets ArmyRuntime audio callbacks reassert wanted slots, then
// starts/updates/stops one looping voice per world sound-table slot. The editor owns these host
// voices through this RAII handle; stop() is idempotent and backend failure remains non-fatal.
class LoopingPcmVoice {
public:
    ~LoopingPcmVoice();
    LoopingPcmVoice(LoopingPcmVoice&&) noexcept;
    LoopingPcmVoice& operator=(LoopingPcmVoice&&) noexcept;
    LoopingPcmVoice(const LoopingPcmVoice&) = delete;
    LoopingPcmVoice& operator=(const LoopingPcmVoice&) = delete;

    void stop() noexcept;
    bool active() const noexcept;
    void set_gains(uint32_t left_gain_q15, uint32_t right_gain_q15) noexcept;

private:
    struct State;
    explicit LoopingPcmVoice(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
    friend std::unique_ptr<LoopingPcmVoice> play_pcm_loop(
        const SamPcmData& pcm, uint32_t left_gain_q15, uint32_t right_gain_q15);
};

// Starts one host loop with independent software-scaled left/right gains. set_gains() updates the
// projected channel without restarting the voice, mirroring the retail clear/reassert/update pool.
// nullptr means no backend/device accepted it.
std::unique_ptr<LoopingPcmVoice> play_pcm_loop(
    const SamPcmData& pcm, uint32_t left_gain_q15, uint32_t right_gain_q15);
std::unique_ptr<LoopingPcmVoice> play_pcm_loop(const SamPcmData& pcm);

} // namespace thandor
