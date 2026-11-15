#pragma once

#include <thandor/core/color_lanes.h>
#include <thandor/graphics/render/light_records.h>

#ifdef THANDOR_WINDOWS_HEADLESS
bool replace_jandrenator_file(const char* from, const char* to);
#else
static bool replace_jandrenator_file(const char* from, const char* to) {
    return std::rename(from, to) == 0;
}
#endif

// Small, atomically published snapshots for the editor's multi-run results panel.
static void JandrenatorTrace(const std::string& prefix) {
    const auto temporary = prefix + ".part";
    auto* file = std::fopen(temporary.c_str(), "w");
    if (!file) throw std::runtime_error("cannot open Jandrenator progress snapshot");
    std::array<unsigned, 8> buildings{}, units{};
    for (unsigned i = 0; i < ARMY_RUNTIME_SLOT_COUNT; ++i) {
        const auto& army = g_ArmyRuntimeSlots[i];
        if (!army.modelNodeRuntime || army.factionIndex <= 0 || army.factionIndex >= 8) continue;
        (army.depthBinClass == 144 ? buildings : units)[army.factionIndex]++;
    }
    const auto& game = g_GameFactionRuntimeImage;
    std::fprintf(file, "%u\n", game.tail.simulationTick);
    for (unsigned i = 1; i <= game.tail.activeFactionCount && i < 8; ++i) {
        const auto& faction = game.records[i];
        std::fprintf(file, "%u %u %u %u %u %u\n", i,
                     unsigned(game.tail.factionLifecycleStates[i]), buildings[i], units[i],
                     faction.xeniteCurrentQ4, faction.tritiumCurrentQ4);
    }
    const bool write_failed = std::ferror(file);
    if (std::fclose(file) != 0 || write_failed ||
        !replace_jandrenator_file(temporary.c_str(), prefix.c_str())) {
        throw std::runtime_error("cannot publish Jandrenator progress snapshot");
    }
}

// Visibility is applied by the editor for its currently selected view faction.
static uint32_t JandrenatorTint(uint32_t state, uint32_t definition) {
    return ColorLanes_PackWordsUnsignedSaturate(
        pmulhw(ColorLanes_UnpackBytesShiftRight(state, 4),
               ColorLanes_UnpackBytesShiftRight(definition, 4)));
}

static void WriteJandrenatorPose(FILE* file, const ModelRuntimeNode& node) {
    const auto& matrix = node.worldTransform;
    std::fprintf(file, " %d %d %d", matrix.translation.x, matrix.translation.y, matrix.translation.z);
    for (const auto* row : {matrix.basisRow0, matrix.basisRow1, matrix.basisRow2})
        for (unsigned column = 0; column < 3; ++column) std::fprintf(file, " %d", row[column]);
}

static void WriteJandrenatorModel(FILE* file, const ModelRuntimeNode& node, uint64_t path, unsigned depth) {
    std::fprintf(file, "m %llu", static_cast<unsigned long long>(path));
    WriteJandrenatorPose(file, node);
    std::fprintf(file, " %u %u %d %d %d %d\n",
                 unsigned(node.primaryAnimatedSubresourceIndex), unsigned(node.secondaryAnimatedSubresourceIndex),
                 int(node.primaryTextureOffsetU), int(node.primaryTextureOffsetV),
                 int(node.secondaryTextureOffsetU), int(node.secondaryTextureOffsetV));
    if (depth == 15) return;
    for (unsigned i = 0; i < std::min<unsigned>(node.childCount, 13); ++i)
        if (const auto* child = node.childNodes[i].get())
            WriteJandrenatorModel(file, *child, (path << 4) | i, depth + 1);
}

// Keep the scene separate from the low-frequency faction summary.
static void JandrenatorScene(const std::string& prefix) {
    const auto destination = prefix + "_scene";
    const auto temporary = destination + ".part";
    auto* file = std::fopen(temporary.c_str(), "w");
    if (!file) throw std::runtime_error("cannot open Jandrenator scene");
    const auto& game = g_GameFactionRuntimeImage;
    std::fprintf(file, "JANDRENATOR_SCENE_2 %u %u\n", game.tail.simulationTick,
                 unsigned(game.tail.activeFactionCount));
    for (unsigned i = 0; i < 8; ++i) {
        for (auto word : game.records[i].technologyMasks256Bits) std::fprintf(file, "%u ", word);
        std::fputc('\n', file);
    }
    for (unsigned i = 0; i < ARMY_RUNTIME_SLOT_COUNT; ++i) {
        const auto& army = g_ArmyRuntimeSlots[i];
        const auto* node = army.modelNodeRuntime.get();
        if (!node || army.factionIndex <= 0 || army.factionIndex >= 8) continue;
        const auto* model = army.modelRuntimeOrSavedOffset.modelRuntime.get();
        if (!model || !model->definitionOrSavedId.runtimeDefinition) continue;
        const auto* definition = model->definitionOrSavedId.runtimeDefinition.get();
        const auto& pos = node->worldTransform.translation;
        std::fprintf(file, "%u %u %u %u %u %d %d %d %u %u %u\n", i,
                     unsigned(army.armyAssetId), unsigned(army.factionIndex),
                     unsigned(definition->runtimeClassId), unsigned(army.depthBinClass == 144),
                     pos.x, pos.y, pos.z, unsigned(node->modelPayload.worldRotationAngle2) & 0xffffu,
                     unsigned(model->health), unsigned(definition->maximumHealth));
        WriteJandrenatorModel(file, *node, 1, 0);
    }
    for (unsigned i = 0; g_EffectRuntimeSlots && i < EFFECT_RUNTIME_SLOT_COUNT; ++i) {
        const auto& effect = g_EffectRuntimeSlots[i];
        const auto* node = effect.modelNodeOrSavedOffset.modelNode.get();
        if (!node || !effect.definitionOrSavedId.definition) continue;
        std::fprintf(file, "e %u %u %u %u %d 0 0",
                     unsigned(effect.definitionOrSavedId.definition->definitionId), effect.effectAgeTicks,
                     unsigned(node->textureSubresourceBaseIndex),
                     JandrenatorTint(effect.stateTintArgb, effect.definitionOrSavedId.definition->stateTintArgb),
                     node->modelScaleQ12);
        WriteJandrenatorPose(file, *node);
        std::fputc('\n', file);
    }
    for (unsigned i = 0; g_ShotRuntimeSlots && i < SHOT_RUNTIME_SLOT_COUNT; ++i) {
        const auto& shot = g_ShotRuntimeSlots[i];
        const auto* node = shot.modelNodeOrSavedOffset.modelNode.get();
        const auto* definition = shot.definitionOrSavedId.definition.get();
        if (!node || !definition) continue;
        const auto* owner = shot.ownerAndTrajectory.ownerArmyRuntime.get();
        std::fprintf(file, "s %u %u %u %u %d %d %u", unsigned(definition->definitionId),
                     shot.projectileAgeTicks, shot.ownerAndTrajectory.animationFrameIndex,
                     JandrenatorTint(0xffffffffU, definition->stateTintArgb),
                     Q12_ONE, node->renderDepthBiasOrState, owner ? unsigned(owner->factionIndex) : 0U);
        WriteJandrenatorPose(file, *node);
        std::fputc('\n', file);
    }
    for (const auto& light : g_GraphicsShadingRuntimeRecords) {
        if (light.packedColorRgbActive == 0 || light.squaredRadiusQ24 == 0) continue;
        std::fprintf(file, "l %d %d %d %llu %u\n", light.worldXQ12, light.worldYQ12, light.worldZQ12,
                     static_cast<unsigned long long>(light.squaredRadiusQ24), unsigned(light.packedColorRgbActive));
    }
    const auto* field = root.worldRuntime.fieldGrid.get();
    if (field) {
        const unsigned count = field->gridWidth * field->gridHeight;
        for (unsigned i = 0; i < count;) {
            constexpr uint64_t visibility = 0xf9f9f9f9f9f9f9f9ULL;
            const uint64_t occupancy = uint64_t(field->cells[i].occupancyMask) & visibility;
            unsigned next = i + 1;
            while (next < count && (uint64_t(field->cells[next].occupancyMask) & visibility) == occupancy) ++next;
            std::fprintf(file, "f %u %llu\n", next - i, static_cast<unsigned long long>(occupancy));
            i = next;
        }
    }
    std::fputs("end\n", file);
    const bool failed = std::ferror(file);
    if (std::fclose(file) != 0 || failed || !replace_jandrenator_file(temporary.c_str(), destination.c_str())) {
        throw std::runtime_error("cannot publish Jandrenator scene");
    }
}
