#include "thandor/model_animation.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using thandor::model_runtime_world_child_enabled;

    require(model_runtime_world_child_enabled(14U, 0U),
            "class-14 runtime child slot 0 must remain live");
    require(model_runtime_world_child_enabled(14U, 1U),
            "class-14 runtime child slot 1 must remain live");
    require(model_runtime_world_child_enabled(14U, 2U),
            "class-14 runtime child slot 2 must remain live");
    require(!model_runtime_world_child_enabled(14U, 3U),
            "class-14 runtime child slot 3 must be detached");
    require(model_runtime_world_child_enabled(14U, 4U),
            "class-14 runtime children after slot 3 must not be globally suppressed");
    require(!model_runtime_world_child_enabled(13U, 3U),
            "class-13 placement arrow must be detached in world rendering");
    require(!model_runtime_world_child_enabled(15U, 1U),
            "class-15 placement child1 must be detached in world rendering");
    require(model_runtime_world_child_enabled(15U, 3U),
            "class 15 has a different retail detach rule and must not inherit class-14 suppression");

    // The authored ModelVisualNode is deliberately untouched. Preview/card renderers can keep the
    // serialized slot-3 child while world/runtime renderers consult the predicate above.
    thandor::ModelVisualNode authored;
    authored.runtime_class_id = 14U;
    authored.child_slot = 3U;
    authored.flags = 0U;
    authored.sprite_path = "spr/authored_floor.spr";
    require(authored.creates_runtime_node() && authored.child_slot == 3U && !authored.sprite_path.empty(),
            "authored class-14 slot-3 hierarchy was mutated instead of runtime-suppressed");

    return 0;
}
