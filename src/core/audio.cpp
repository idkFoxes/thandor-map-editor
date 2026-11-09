#include "thandor/audio.hpp"

#include "thandor/binary.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <mmsystem.h>
#elif defined(__linux__)
#  include <dlfcn.h>
#endif

namespace thandor {
namespace { std::atomic<uint64_t> audio_queued{},audio_completed{},audio_failed{}; }
AudioPlaybackDiagnostics audio_playback_diagnostics() noexcept {
    return {audio_queued.load(),audio_completed.load(),audio_failed.load()};
}

namespace {

constexpr uint32_t kSamMagic = 0x006D6173U;
constexpr uint32_t kSamFlags = 0x00010000U;
constexpr std::size_t kSamHeaderBytes = 0x200U;
constexpr std::size_t kCoefficientCount = 256U;
constexpr std::size_t kPcmSamplesPerBlock = 512U;

uint32_t load_le_u32_zero(std::span<const std::byte> data, std::size_t offset) noexcept {
    uint32_t value = 0U;
    for (std::size_t i = 0; i < 4U; ++i) {
        if (offset + i < data.size()) value |= std::to_integer<uint32_t>(data[offset + i]) << (i * 8U);
    }
    return value;
}

uint16_t load_le_u16_zero(std::span<const std::byte> data, std::size_t offset) noexcept {
    uint16_t value = 0U;
    for (std::size_t i = 0; i < 2U; ++i) {
        if (offset + i < data.size()) value |= static_cast<uint16_t>(std::to_integer<uint16_t>(data[offset + i]) << (i * 8U));
    }
    return value;
}

int16_t sign_extend_low(uint32_t value, unsigned bits) noexcept {
    const uint32_t sign = 1U << (bits - 1U);
    const uint32_t mask = (1U << bits) - 1U;
    const uint32_t v = value & mask;
    return static_cast<int16_t>(static_cast<int32_t>((v ^ sign) - sign));
}

std::size_t decode_coefficient_block(std::span<const std::byte> encoded,
                                     std::array<int16_t, kCoefficientCount>& out) {
    uint32_t bit_buffer = load_le_u32_zero(encoded, 0U);
    uint32_t bits_available = 32U;
    std::size_t refill = 4U;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if ((bit_buffer & 1U) == 0U) {
            out[i] = 0;
            bit_buffer >>= 1U;
            bits_available -= 1U;
        } else if ((bit_buffer & 2U) == 0U) {
            out[i] = sign_extend_low(bit_buffer >> 2U, 3U);
            bit_buffer >>= 5U;
            bits_available -= 5U;
        } else if ((bit_buffer & 4U) == 0U) {
            out[i] = sign_extend_low(bit_buffer >> 3U, 6U);
            bit_buffer >>= 9U;
            bits_available -= 9U;
        } else {
            out[i] = sign_extend_low(bit_buffer >> 3U, 12U);
            bit_buffer >>= 15U;
            bits_available -= 15U;
        }

        const unsigned shift = static_cast<unsigned>(bits_available & 31U);
        if (bits_available < 9U) {
            const uint32_t refill24 = load_le_u32_zero(encoded, refill) & 0x00FFFFFFU;
            refill += 3U;
            bit_buffer |= refill24 << shift;
            bits_available += 24U;
        } else if (bits_available < 17U) {
            const uint32_t refill16 = load_le_u16_zero(encoded, refill);
            refill += 2U;
            bit_buffer |= refill16 << shift;
            bits_available += 16U;
        } else if (bits_available < 25U) {
            const uint32_t refill8 = refill < encoded.size() ? std::to_integer<uint32_t>(encoded[refill]) : 0U;
            refill += 1U;
            bit_buffer |= refill8 << shift;
            bits_available += 8U;
        }
    }
    if (bits_available == 32U) {
        if (refill != 0U) --refill;
    } else if (bits_available < 24U) {
        ++refill;
    }
    return refill & ~std::size_t{3U};
}

const std::array<int16_t, 256U * 256U>& synthesis_table() {
    static const auto table = [] {
        std::array<int16_t, 256U * 256U> result{};
        std::array<int32_t, 65536U> fixed_cos{};
        constexpr double scale = 268435456.0; // 2^28
        for (std::size_t k = 0; k < fixed_cos.size(); ++k) {
            const double angle = (2.0 * std::numbers::pi * static_cast<double>(k)) / 65536.0;
            fixed_cos[k] = static_cast<int32_t>(std::llround(std::cos(angle) * scale));
        }
        std::size_t write = 0U;
        uint32_t phase_step = 0x40U;
        while (phase_step < 0x8000U) {
            uint32_t phase = 0U;
            for (uint32_t i = 0; i < 0x100U; ++i) {
                if (i == 0U) result[write++] = static_cast<int16_t>(0x2D41);
                else result[write++] = static_cast<int16_t>(fixed_cos[phase] >> 14);
                phase = (phase + phase_step) & 0xFFFFU;
            }
            phase_step += 0x80U;
        }
        return result;
    }();
    return table;
}

int16_t saturating_double(int16_t value) noexcept {
    const int32_t doubled = static_cast<int32_t>(value) * 2;
    return static_cast<int16_t>(std::clamp(doubled, -32768, 32767));
}

void synthesize_block(const std::array<int16_t, kCoefficientCount>& coefficients,
                      std::span<int16_t, kPcmSamplesPerBlock> output) {
    const auto& table = synthesis_table();
    for (std::size_t frame = 0; frame < 256U; ++frame) {
        uint32_t wrapped = 0U;
        const std::size_t row = frame * 256U;
        for (std::size_t k = 0; k < 256U; ++k) {
            const int32_t product = static_cast<int32_t>(coefficients[k]) * static_cast<int32_t>(table[row + k]);
            wrapped += static_cast<uint32_t>(product);
        }
        wrapped <<= 5U;
        const int16_t high_word = std::bit_cast<int16_t>(static_cast<uint16_t>(wrapped >> 16U));
        const int16_t sample = saturating_double(high_word);
        output[frame * 2U] = sample;
        output[frame * 2U + 1U] = sample;
    }
}


uint32_t clamp_q15(uint32_t gain) noexcept {
    return std::min(gain, 0x8000U);
}

int16_t scale_pcm_q15(int16_t sample, uint32_t gain_q15) noexcept {
    const int64_t scaled = static_cast<int64_t>(sample) * static_cast<int64_t>(clamp_q15(gain_q15));
    return static_cast<int16_t>(std::clamp<int64_t>(scaled >> 15, -32768, 32767));
}

[[maybe_unused]] void scale_stereo_samples(std::span<int16_t> samples, uint32_t left_gain_q15,
                          uint32_t right_gain_q15) noexcept {
    left_gain_q15 = clamp_q15(left_gain_q15);
    right_gain_q15 = clamp_q15(right_gain_q15);
    for (std::size_t i = 0; i + 1U < samples.size(); i += 2U) {
        samples[i] = scale_pcm_q15(samples[i], left_gain_q15);
        samples[i + 1U] = scale_pcm_q15(samples[i + 1U], right_gain_q15);
    }
}

#if defined(__linux__)
template <typename Fn>
Fn load_symbol(void* library, const char* name) noexcept {
    Fn fn{};
    void* raw = dlsym(library, name);
    static_assert(sizeof(fn) == sizeof(raw));
    std::memcpy(&fn, &raw, sizeof(fn));
    return fn;
}

// All Linux sources share one PCM stream. Opening dmix once per unit/shot
// exhausted the host's PCM/IPC resources, then retried every GUI frame.
struct LinuxPcmVoice {
    std::atomic<bool> running{true};
    std::atomic<uint32_t> left_gain_q15{0x8000U}, right_gain_q15{0x8000U};
    std::vector<int16_t> samples;
    std::size_t frame_offset{};
    bool looping{};
};
class LinuxPcmMixer {
    using Open = int (*)(void**, const char*, int, int);
    using SetParams = int (*)(void*, int, int, unsigned, unsigned, int, unsigned);
    using Write = long (*)(void*, const void*, unsigned long);
    using PcmAction = int (*)(void*);
    std::mutex mutex_;
    std::condition_variable wake_;
    void* library_{};
    void* pcm_{};
    Write write_{};
    PcmAction prepare_{}, drop_{}, close_{};
    std::vector<std::shared_ptr<LinuxPcmVoice>> voices_;
    std::chrono::steady_clock::time_point retry_after_{};
    bool stopping_{};
    std::jthread worker_;

    bool open_device() {
        if (pcm_) return true;
        if (std::chrono::steady_clock::now() < retry_after_) return false;
        retry_after_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        if (!library_) library_ = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
        if (!library_) return false;
        const auto open = load_symbol<Open>(library_, "snd_pcm_open");
        const auto set_params = load_symbol<SetParams>(library_, "snd_pcm_set_params");
        write_ = load_symbol<Write>(library_, "snd_pcm_writei");
        prepare_ = load_symbol<PcmAction>(library_, "snd_pcm_prepare");
        drop_ = load_symbol<PcmAction>(library_, "snd_pcm_drop");
        close_ = load_symbol<PcmAction>(library_, "snd_pcm_close");
        if (!open || !set_params || !write_ || !prepare_ || !drop_ || !close_) return false;
        // ALSA stable ABI: PLAYBACK=0, S16_LE=2, RW_INTERLEAVED=3.
        if (open(&pcm_, "default", 0, 0) < 0 || !pcm_) { pcm_ = nullptr; return false; }
        if (set_params(pcm_, 2, 3, 2U, SamPcmData::kSampleRate, 1, 100000U) < 0) {
            close_(pcm_); pcm_ = nullptr; return false;
        }
        return true;
    }
    void fail_device() {
        for (const auto& voice : voices_) {
            if (voice->running.exchange(false) && !voice->looping) ++audio_failed;
        }
        voices_.clear();
        if (pcm_) { drop_(pcm_); close_(pcm_); pcm_ = nullptr; }
        retry_after_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    }
    void run() {
        constexpr std::size_t kFrames = 512U;
        std::array<int64_t, kFrames * 2U> sum{};
        std::array<int16_t, kFrames * 2U> output{};
        std::unique_lock lock(mutex_);
        while (!stopping_) {
            std::erase_if(voices_, [](const auto& v) { return !v->running.load(); });
            if (voices_.empty()) {
                wake_.wait(lock, [&] { return stopping_ || !voices_.empty(); });
                continue;
            }
            sum.fill(0);
            std::size_t frames = 0;
            std::vector<std::shared_ptr<LinuxPcmVoice>> completed;
            for (const auto& voice : voices_) {
                const auto total = voice->samples.size() / 2U;
                const auto count = voice->looping ? kFrames : std::min(kFrames, total - voice->frame_offset);
                frames = std::max(frames, count);
                const auto left = voice->left_gain_q15.load(), right = voice->right_gain_q15.load();
                for (std::size_t frame = 0; frame < count; ++frame) {
                    const auto offset = voice->frame_offset * 2U;
                    sum[frame * 2U] += scale_pcm_q15(voice->samples[offset], left);
                    sum[frame * 2U + 1U] += scale_pcm_q15(voice->samples[offset + 1U], right);
                    ++voice->frame_offset;
                    if (voice->looping && voice->frame_offset == total) voice->frame_offset = 0;
                }
                if (!voice->looping && voice->frame_offset == total) completed.push_back(voice);
            }
            for (std::size_t i = 0; i < frames * 2U; ++i)
                output[i] = static_cast<int16_t>(std::clamp<int64_t>(sum[i], -32768, 32767));
            // ALSA write blocks at device pace. Submissions, gain changes and
            // handle destruction never wait for a whole sample to finish.
            void* pcm = pcm_;
            lock.unlock();
            std::size_t written = 0;
            uint32_t recoveries = 0;
            while (written < frames) {
                const long count = write_(pcm, output.data() + written * 2U,
                    static_cast<unsigned long>(frames - written));
                if (count < 0 && ++recoveries <= 3U && prepare_(pcm) >= 0) continue;
                if (count <= 0 || static_cast<unsigned long>(count) > frames - written) break;
                recoveries = 0; written += static_cast<std::size_t>(count);
            }
            lock.lock();
            if (stopping_) break;
            if (written != frames) { fail_device(); continue; }
            for (const auto& voice : completed)
                if (voice->running.exchange(false)) ++audio_completed;
        }
    }
public:
    LinuxPcmMixer() : worker_([this] { run(); }) {}
    ~LinuxPcmMixer() {
        { std::lock_guard lock(mutex_); stopping_ = true; if (pcm_) drop_(pcm_); }
        wake_.notify_all();
        worker_.join();
        if (pcm_) close_(pcm_);
        if (library_) dlclose(library_);
    }
    std::shared_ptr<LinuxPcmVoice> submit(const SamPcmData& pcm, uint32_t left, uint32_t right, bool looping) {
        std::lock_guard lock(mutex_);
        std::erase_if(voices_, [](const auto& v) { return !v->running.load(); });
        // Bound memory/work during a stalled host; completed/stopped handles do
        // not consume slots. This is a host limit, not a simulation sound rule.
        if (voices_.size() >= 256U || !open_device()) return {};
        auto voice = std::make_shared<LinuxPcmVoice>();
        voice->samples = pcm.interleaved_stereo;
        voice->left_gain_q15 = clamp_q15(left); voice->right_gain_q15 = clamp_q15(right);
        voice->looping = looping;
        voices_.push_back(voice);
        if (!looping) ++audio_queued;
        wake_.notify_one();
        return voice;
    }
};
LinuxPcmMixer& linux_mixer() { static LinuxPcmMixer mixer; return mixer; }

#endif

#if defined(_WIN32)
bool play_windows(std::vector<int16_t> samples) {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = SamPcmData::kSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    HWAVEOUT output{};
    if (waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return false;
    WAVEHDR header{};
    header.lpData = reinterpret_cast<LPSTR>(samples.data());
    header.dwBufferLength = static_cast<DWORD>(samples.size() * sizeof(int16_t));
    if (waveOutPrepareHeader(output, &header, sizeof(header)) != MMSYSERR_NOERROR) { waveOutClose(output); return false; }
    if (waveOutWrite(output, &header, sizeof(header)) != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(output, &header, sizeof(header)); waveOutClose(output); return false;
    }
    while ((header.dwFlags & WHDR_DONE) == 0U) Sleep(2);
    waveOutUnprepareHeader(output, &header, sizeof(header));
    waveOutClose(output);
    return true;
}
#endif

} // namespace

std::optional<uint32_t> sam_trailing_decimal_index(std::string_view path) noexcept {
    const auto slash = path.find_last_of("/\\");
    std::string_view leaf = slash == std::string_view::npos ? path : path.substr(slash + 1U);
    const auto dot = leaf.find_last_of('.');
    if (dot == std::string_view::npos) return std::nullopt;
    std::string_view extension = leaf.substr(dot + 1U);
    if (extension.size() != 3U || (extension[0] | 0x20) != 's' || (extension[1] | 0x20) != 'a' ||
        (extension[2] | 0x20) != 'm') return std::nullopt;
    std::size_t begin = dot;
    while (begin > 0U && leaf[begin - 1U] >= '0' && leaf[begin - 1U] <= '9') --begin;
    if (begin == dot) return std::nullopt;
    uint64_t value = 0U;
    for (std::size_t i = begin; i < dot; ++i) {
        value = value * 10U + static_cast<uint64_t>(leaf[i] - '0');
        if (value > std::numeric_limits<uint32_t>::max()) return std::nullopt;
    }
    return static_cast<uint32_t>(value);
}

SamPcmData decode_sam_to_pcm(std::span<const std::byte> data) {
    if (data.size() < kSamHeaderBytes) throw FormatError("SAM header is truncated");
    if (read_u32(data, 0x00U) != kSamMagic) throw FormatError("SAM magic mismatch");
    if (read_u32(data, 0x0CU) != kSamFlags) throw FormatError("SAM format flags mismatch");
    const uint32_t block_count = read_u32(data, 0xB0U);
    if (block_count > 1U << 20U) throw FormatError("SAM PCM block count is unreasonable");
    SamPcmData result;
    result.interleaved_stereo.resize(static_cast<std::size_t>(block_count) * kPcmSamplesPerBlock);
    std::size_t encoded_offset = kSamHeaderBytes;
    std::array<int16_t, kCoefficientCount> coefficients{};
    for (uint32_t block = 0U; block < block_count; ++block) {
        if (encoded_offset >= data.size()) throw FormatError("SAM packed payload ended before all PCM blocks");
        const std::size_t consumed = decode_coefficient_block(data.subspan(encoded_offset), coefficients);
        if (consumed == 0U) throw FormatError("SAM packed block consumed zero bytes");
        std::span<int16_t, kPcmSamplesPerBlock> output(result.interleaved_stereo.data() + static_cast<std::size_t>(block) * kPcmSamplesPerBlock,
                                                       kPcmSamplesPerBlock);
        synthesize_block(coefficients, output);
        encoded_offset += consumed;
    }
    return result;
}

std::array<float,3> spatial_sound_listener_position(std::array<float,3> target,
    int32_t camera_distance_q12) noexcept {
    target[2]+=static_cast<float>(camera_distance_q12>>2)/4096.0F;return target;
}

SpatialSoundGainsQ15 spatial_sound_gains_q15(
    const SpatialAudioListener& listener, std::array<float, 3> source_position,
    uint32_t source_gain_q15, uint32_t maximum_distance_q12,
    uint32_t effects_gain_q15, bool reverse_stereo) noexcept {
    const uint64_t product = static_cast<uint64_t>(clamp_q15(source_gain_q15)) *
                             static_cast<uint64_t>(clamp_q15(effects_gain_q15));
    const uint32_t scaled_gain_q15 = static_cast<uint32_t>(product >> 15U);
    if (scaled_gain_q15 == 0U || maximum_distance_q12 == 0U) return {};

    const double dx = static_cast<double>(source_position[0] - listener.position[0]);
    const double dy = static_cast<double>(source_position[1] - listener.position[1]);
    const double dz = static_cast<double>(source_position[2] - listener.position[2]);
    const double distance_world = std::sqrt(dx * dx + dy * dy + dz * dz);
    const uint64_t distance_q12 = static_cast<uint64_t>(std::max<int64_t>(0,
        std::llround(distance_world * 4096.0)));
    if (distance_q12 >= maximum_distance_q12) return {};

    // The executable indexes g_FixedCosQ28 over the first quarter-turn with
    // (distanceQ12 << 14) / maximumDistanceQ12. Recompute the same angle continuously here;
    // the editor-side float position can carry fractions below one retail Q12 unit anyway.
    const double radial = std::clamp(
        static_cast<double>(distance_q12) / static_cast<double>(maximum_distance_q12), 0.0, 1.0);
    const double attenuation = std::cos(radial * (std::numbers::pi / 2.0));
    uint32_t attenuated_q15 = static_cast<uint32_t>(std::max<int64_t>(0,
        std::llround(static_cast<double>(scaled_gain_q15) * attenuation)));
    attenuated_q15 = clamp_q15(attenuated_q15);
    // Exact game threshold: values <= 0x100 do not submit a positioned voice.
    if (attenuated_q15 <= 0x100U) return {};

    const double local_right = dx * listener.right[0] + dy * listener.right[1] +
                               dz * listener.right[2];
    const double local_forward = dx * listener.forward[0] + dy * listener.forward[1] +
                                 dz * listener.forward[2];
    const double azimuth = std::atan2(local_right, local_forward);
    const double cosine = std::cos(azimuth);
    const double crossfeed = cosine * cosine;
    const uint32_t cross_q15 = clamp_q15(static_cast<uint32_t>(std::max<int64_t>(0,
        std::llround(static_cast<double>(attenuated_q15) * crossfeed))));

    SpatialSoundGainsQ15 gains;
    if (local_right >= 0.0) {
        gains.left_q15 = cross_q15;
        gains.right_q15 = attenuated_q15;
    } else {
        gains.left_q15 = attenuated_q15;
        gains.right_q15 = cross_q15;
    }
    if (reverse_stereo) std::swap(gains.left_q15, gains.right_q15);
    return gains;
}

struct LoopingPcmVoice::State {
    std::atomic<bool> running{false};
    std::atomic<uint32_t> left_gain_q15{0x8000U};
    std::atomic<uint32_t> right_gain_q15{0x8000U};
    std::vector<int16_t> samples;
#if defined(_WIN32)
    HWAVEOUT output{};
    std::jthread worker;
#elif defined(__linux__)
    std::shared_ptr<LinuxPcmVoice> linux_voice;

#endif

    ~State() { stop(); }

    void set_gains(uint32_t left, uint32_t right) noexcept {
        left_gain_q15.store(clamp_q15(left), std::memory_order_release);
        right_gain_q15.store(clamp_q15(right), std::memory_order_release);
#if defined(__linux__)
        if (linux_voice) { linux_voice->left_gain_q15 = clamp_q15(left); linux_voice->right_gain_q15 = clamp_q15(right); }
#endif
    }

    void stop() noexcept {
        running.store(false, std::memory_order_release);
#if defined(_WIN32)
        if (worker.joinable()) worker.request_stop();
        if (output) (void)waveOutReset(output);
        if (worker.joinable()) worker.join();
        if (output) (void)waveOutClose(output);
        output = nullptr;
#elif defined(__linux__)
        if (linux_voice) linux_voice->running.store(false, std::memory_order_release);

#endif
    }
};

LoopingPcmVoice::LoopingPcmVoice(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
LoopingPcmVoice::~LoopingPcmVoice() = default;
LoopingPcmVoice::LoopingPcmVoice(LoopingPcmVoice&&) noexcept = default;
LoopingPcmVoice& LoopingPcmVoice::operator=(LoopingPcmVoice&&) noexcept = default;

void LoopingPcmVoice::stop() noexcept {
    if (state_) state_->stop();
}

bool LoopingPcmVoice::active() const noexcept {
#if defined(__linux__)
    return state_ && state_->linux_voice && state_->linux_voice->running.load(std::memory_order_acquire);
#else
    return state_ && state_->running.load(std::memory_order_acquire);
#endif
}

void LoopingPcmVoice::set_gains(uint32_t left_gain_q15, uint32_t right_gain_q15) noexcept {
    if (state_) state_->set_gains(left_gain_q15, right_gain_q15);
}

bool play_pcm_one_shot(const SamPcmData& pcm, uint32_t left_gain_q15,
                       uint32_t right_gain_q15) {
    if (pcm.frame_count() == 0U || (left_gain_q15 == 0U && right_gain_q15 == 0U)) return false;
#if defined(_WIN32)
    std::vector<int16_t> samples = pcm.interleaved_stereo;
    scale_stereo_samples(samples, left_gain_q15, right_gain_q15);
    ++audio_queued;
    std::thread([samples = std::move(samples)]() mutable {
        if(play_windows(std::move(samples)))++audio_completed;else ++audio_failed;
    }).detach();
    return true;
#elif defined(__linux__)
    if (linux_mixer().submit(pcm, left_gain_q15, right_gain_q15, false)) return true;
    ++audio_failed;
    return false;
#else
    (void)pcm;
    return false;
#endif
}

bool play_pcm_one_shot(const SamPcmData& pcm) {
    return play_pcm_one_shot(pcm, 0x8000U, 0x8000U);
}

std::unique_ptr<LoopingPcmVoice> play_pcm_loop(
    const SamPcmData& pcm, uint32_t left_gain_q15, uint32_t right_gain_q15) {
    if (pcm.frame_count() == 0U || (left_gain_q15 == 0U && right_gain_q15 == 0U)) return nullptr;
    auto state = std::make_unique<LoopingPcmVoice::State>();
#if defined(_WIN32)
    state->samples = pcm.interleaved_stereo;
#endif
    state->set_gains(left_gain_q15, right_gain_q15);
#if defined(_WIN32)
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = SamPcmData::kSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    if (waveOutOpen(&state->output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        return nullptr;
    state->running.store(true, std::memory_order_release);
    auto* raw = state.get();
    state->worker = std::jthread([raw](std::stop_token token) {
        constexpr std::size_t kChunkFrames = 1024U;
        const std::size_t total_frames = raw->samples.size() / SamPcmData::kChannels;
        if (total_frames == 0U) { raw->running.store(false, std::memory_order_release); return; }
        std::vector<int16_t> chunk(kChunkFrames * 2U);
        std::size_t frame_offset = 0U;
        while (!token.stop_requested() && raw->running.load(std::memory_order_acquire)) {
            const std::size_t frames = std::min(kChunkFrames, total_frames - frame_offset);
            const uint32_t left = raw->left_gain_q15.load(std::memory_order_acquire);
            const uint32_t right = raw->right_gain_q15.load(std::memory_order_acquire);
            for (std::size_t frame = 0U; frame < frames; ++frame) {
                const std::size_t source = (frame_offset + frame) * 2U;
                chunk[frame * 2U] = scale_pcm_q15(raw->samples[source], left);
                chunk[frame * 2U + 1U] = scale_pcm_q15(raw->samples[source + 1U], right);
            }
            WAVEHDR header{};
            header.lpData = reinterpret_cast<LPSTR>(chunk.data());
            header.dwBufferLength = static_cast<DWORD>(frames * 2U * sizeof(int16_t));
            if (waveOutPrepareHeader(raw->output, &header, sizeof(header)) != MMSYSERR_NOERROR) break;
            if (waveOutWrite(raw->output, &header, sizeof(header)) != MMSYSERR_NOERROR) {
                (void)waveOutUnprepareHeader(raw->output, &header, sizeof(header)); break;
            }
            while ((header.dwFlags & WHDR_DONE) == 0U && !token.stop_requested() &&
                   raw->running.load(std::memory_order_acquire)) Sleep(2);
            if (token.stop_requested() || !raw->running.load(std::memory_order_acquire))
                (void)waveOutReset(raw->output);
            (void)waveOutUnprepareHeader(raw->output, &header, sizeof(header));
            frame_offset += frames;
            if (frame_offset >= total_frames) frame_offset = 0U;
        }
        raw->running.store(false, std::memory_order_release);
    });
#elif defined(__linux__)
    state->linux_voice = linux_mixer().submit(pcm, left_gain_q15, right_gain_q15, true);
    if (!state->linux_voice) return nullptr;

#else
    return nullptr;
#endif
    return std::unique_ptr<LoopingPcmVoice>(new LoopingPcmVoice(std::move(state)));
}

std::unique_ptr<LoopingPcmVoice> play_pcm_loop(const SamPcmData& pcm) {
    return play_pcm_loop(pcm, 0x8000U, 0x8000U);
}

} // namespace thandor
