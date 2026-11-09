#ifndef THANDOR_GAMEPLAY_RECORDING_OWNER_H
#define THANDOR_GAMEPLAY_RECORDING_OWNER_H
/* V2: owner class 0 has a ModelRuntimeSlot payload, not ArmyRuntimeSlot.
   The native 0x51D2AF..0x51D2BB loads node+0x48 -> model+8 -> army+0x0C.
   Only the owning army's root model is an actor. Child models are not actors. */
static u32 tgr_army_for_node_v2(u32 node, u32 (*read32)(u32)) {
    u32 model, army;
    if (read32(node + 0xA4u) != 0u) return 0u;
    model = read32(node + 0x48u);
    if (!model) return 0u;
    army = read32(model + 0x08u);
    if (!army || read32(army) != model) return 0u;
    return army;
}
#endif
