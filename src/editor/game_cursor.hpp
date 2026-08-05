#pragma once

#include "thandor/binary.hpp"
#include "thandor/gfx.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace editor {

// engine\\mouse.dat is an array of 0x20-byte records. game.exe copies the range starts
// at +0x08 and +0x10 into the two runtime cursors at +0x18 and +0x1C, then advances
// those cursors every second 20-ms graphics timer tick.
struct GameCursorFrameRecord {
    int32_t hotspot_x{};
    int32_t hotspot_y{};
    uint32_t idle_first{};
    uint32_t idle_last{};
    uint32_t active_first{};
    uint32_t active_last{};
};

class GameCursorAsset {
public:
    static constexpr std::size_t kRecordSize = 0x20U;
    static constexpr double kAnimationStepSeconds = 0.040;

    // Cursor-frame IDs recovered from the stock mouse.dat table and their in-game uses.
    static constexpr uint32_t kDefaultFrame = 0U;
    static constexpr uint32_t kCameraMoveFrame = 1U;
    // Cursor IDs 0x15/0x16 and 0x19/0x1A are selected by
    // InGameUiCommand_ResolveCursorCodeByMode for pointer-target validity. The stock table's
    // adjacent green/red pairs are retained exactly; the editor uses the green members while a
    // contextual order is currently valid.
    static constexpr uint32_t kPointTargetValidFrame = 21U;
    static constexpr uint32_t kPointTargetInvalidFrame = 22U;
    static constexpr uint32_t kMoveOrderValidFrame = 23U;
    static constexpr uint32_t kMoveOrderInvalidFrame = 24U;
    static constexpr uint32_t kAttackPlacementValidFrame = 25U;
    static constexpr uint32_t kAttackPlacementInvalidFrame = 26U;
    static constexpr uint32_t kCameraTurnFrame = 37U;
    static constexpr uint32_t kEdgeScrollFirstFrame = 47U;
    static constexpr uint32_t kEdgeScrollLastFrame = 54U;

    static GameCursorAsset parse(std::span<const std::byte> gfx_bytes,
                                 std::span<const std::byte> dat_bytes) {
        if (dat_bytes.empty() || dat_bytes.size() % kRecordSize != 0U)
            throw thandor::FormatError("engine/mouse.dat is not a non-empty 0x20-byte record table");

        GameCursorAsset result;
        const auto gfx = thandor::GfxAsset::parse(gfx_bytes);
        if (gfx.subresources().empty())
            throw thandor::FormatError("engine/mouse.gfx has no cursor subresources");

        result.images_.reserve(gfx.subresources().size());
        for (std::size_t index = 0; index < gfx.subresources().size(); ++index)
            result.images_.push_back(gfx.decode_logical(thandor::GraphicsSubresourceIndex{static_cast<uint32_t>(index)}, false));

        const std::size_t frame_count = dat_bytes.size() / kRecordSize;
        result.frames_.reserve(frame_count);
        for (std::size_t index = 0; index < frame_count; ++index) {
            const std::size_t offset = index * kRecordSize;
            GameCursorFrameRecord frame{
                thandor::read_i32(dat_bytes, offset + 0x00U),
                thandor::read_i32(dat_bytes, offset + 0x04U),
                thandor::read_u32(dat_bytes, offset + 0x08U),
                thandor::read_u32(dat_bytes, offset + 0x0CU),
                thandor::read_u32(dat_bytes, offset + 0x10U),
                thandor::read_u32(dat_bytes, offset + 0x14U),
            };
            const auto valid_range = [&](uint32_t first, uint32_t last) {
                return first <= last && last < result.images_.size();
            };
            if (!valid_range(frame.idle_first, frame.idle_last) ||
                !valid_range(frame.active_first, frame.active_last)) {
                throw thandor::FormatError("engine/mouse.dat selects a missing mouse.gfx subresource");
            }
            result.frames_.push_back(frame);
        }
        return result;
    }

    bool empty() const noexcept { return frames_.empty() || images_.empty(); }
    std::size_t frame_count() const noexcept { return frames_.size(); }
    std::size_t subresource_count() const noexcept { return images_.size(); }

    const GameCursorFrameRecord& frame(uint32_t frame_index) const {
        if (frames_.empty()) throw std::out_of_range("game cursor has no frames");
        return frames_[std::min<std::size_t>(frame_index, frames_.size() - 1U)];
    }

    uint32_t subresource_index(uint32_t frame_index, bool button_down,
                               double absolute_seconds) const {
        const auto& selected = frame(frame_index);
        const uint32_t first = button_down ? selected.active_first : selected.idle_first;
        const uint32_t last = button_down ? selected.active_last : selected.idle_last;
        const uint32_t count = last - first + 1U;
        const double safe_seconds = std::max(0.0, absolute_seconds);
        const auto animation_step = static_cast<uint64_t>(
            std::floor(safe_seconds / kAnimationStepSeconds));
        return first + static_cast<uint32_t>(animation_step % count);
    }

    const thandor::RgbaImage& image(uint32_t frame_index, bool button_down,
                                    double absolute_seconds) const {
        return images_.at(subresource_index(frame_index, button_down, absolute_seconds));
    }

private:
    std::vector<GameCursorFrameRecord> frames_;
    std::vector<thandor::RgbaImage> images_;
};

// InGameWorldInput_UpdateDragSelectionAndCameraCf adds pointerDelta * 0x40 to a
// 16-bit heading. Preserve that exact turn granularity for editor camera dragging.
constexpr float game_camera_turn_radians_per_pixel() noexcept {
    return 64.0F * (2.0F * std::numbers::pi_v<float> / 65536.0F);
}

// WorldRuntime_TranslateCameraByScreenDelta multiplies each screen delta by
// (0x20 * targetDistanceQ12) >> 16. Converted from Q12, that is distance / 2048.
constexpr float game_camera_pan_world_units_per_pixel(float camera_distance) noexcept {
    return std::max(camera_distance, 0.0F) / 2048.0F;
}

} // namespace editor
