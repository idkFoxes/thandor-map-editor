# Corrected native replay runner 0.9.776

The 0.9.774 headless bootstrap omitted FixedMath_BuildSinCosTables. Its repeatability check passed, but its gameplay baseline is INVALID: zero trigonometric projections prevented movement and affected geometry/visibility. Do not use the earlier native faction outcomes or divergence timings as gameplay evidence. Those checkpoints remain historical records only.

This runner calls the stock startup table builders before loading assets, and asserts that a 4096-Q12 axis projection is (4096,0). The replay test also requires initial mobile armies to move in runs of at least 600 ticks. Gameplay source itself is unchanged.

Version 0.9.776 also calls the stock FrontendPlayerRuntime_InitializeFactionAssignments before loading the world. The .775 offline bootstrap left unused faction slots 5–7 active on four-faction maps, consuming extra diplomacy RNG. Those .775 outcomes do not represent the normal four-faction setup. Every tick now records lifecycle0 through lifecycle7; the runner asserts startup slot states. The original six-faction slot-7 initialization quirk is preserved.

Linux x86-64 runner built from the supplied Open Thandor 1.0.7 source. It executes the original private InGameTick_RunSimulationStep, keeping the native world update order, RNG, AI, movement/pathfinding, terrain, model geometry, combat, damage, projectiles, effects, factories and research. It does not link Simulation Lab, SDL, Wine, Vulkan or an X/Wayland client. The native PCK mounting and decoder remain original.

Build with Python 3 and GCC C++20:

```
python3 native_elf/build.py --source reference/open-thandor-main --jobs 2
```

Run from the game archive directory (the output directory must already exist):

```
cd /path/to/game_data
/path/to/native_elf/open-thandor-headless 36000 /path/to/output/run 12345 mittelpunkt
```

Arguments are ticks, output prefix, seed and ASCII stock map leaf name. Defaults: 20, native_run, 12345, mittelpunkt. ENGINE.PCK, DATEN.PCK, MODELLE.PCK, GRAPHIK.PCK, SOUND.PCK and LEVEL.PCK must be present. Proprietary PCKs are not bundled. The supplied ELF requires a compatible Linux glibc/libstdc++; rebuild on older distributions.

Every active faction is AI controlled. One native tick per callback, 20 game ticks per second, game speed Q8=256. The native runner lowers its priority by 10 and targets half of one logical CPU, pacing every 20 ticks. No network port or socket is used. A 36,000-tick run is 30 simulated minutes.

Outputs are checked and renamed from .part only after successful completion:

- _ticks.csv: pointer-free observational hashes and counts every tick, including tick 0. This is a selected-state hash, not all memory.
- _factions.csv: economy, queues, technology masks and faction counts every tick.
- _armies.csv: faction-owned root armies every tick, neutral scenery at 1 Hz. Includes HP, positions, orders, route points, production and research. Attached model state is not fully exported. Pool slots differ from Lab entity IDs.

Platform boundary:

- The native 96 MiB arena algorithm is retained; its OS heap block comes from MAP_32BIT. Ptr32 overflow aborts. No pointer truncation is accepted. Principal native record layouts have static assertions.
- File IO slots use stdio; native package validation, decompression, resource parsing, sprites and geometry relocation remain original. Packaged spatial sounds use SOUND.PCK and the native silent backend. Loose directory enumeration is empty.
- Native CPU texture/palette metadata, field initialization, light/clamp tables and model geometry remain. No display or rendered frame is created.
- Movie callbacks advance the loading counter only. Simulation is frozen during loading and starts explicitly at tick 1.
- Selection-panel thumbnail generation is omitted, so temporary preview armies are not spawned. The omitted file's aircraft IDs are retained verbatim because native hangars also use them. UI refresh/input functions are presentation adapters; gameplay functions are not stubbed.
- One empty observer record exists during the original player-picture loader, which does not support zero records. It controls no faction; the frontend player count is zero before the first tick.
- Both RNG streams are seeded before world instantiation; gameplay selects the secondary stream, as the supplied game's determinism test does to avoid UI frame-rate randomness. Native direct primary-stream draws during terrain animation setup remain native.

This is a documented Linux offline bootstrap, not proof of byte-identical Windows startup or complete Lab parity. Initial geometry/derived-cache differences must be reported. Rendered unit shadows, pixels, every attached model and every internal AI workspace are outside this trace's scope.

The build records hashes for original source inputs in source_manifest.json. Original copyright and license are in reference/open-thandor-main/LICENSE.

## Replay and test the checkpoint again

The checkpoint contains both executables, all native source, all editor/Lab source, initial seed/map configuration and complete traces. Only the original PCK data must be supplied separately. Build Lab with CMake/Python/GCC available on PATH:

```
python3 native_elf/build_lab.py --editor editor/ThandorMapEditor_Vulkan_Source --jobs 2
python3 native_elf/run_replay_tests.py /path/to/game_data --output replay_test --ticks 36000 --seed 12345
```

That script runs native twice, checks complete tick coverage and identical native output, then runs Lab. It deliberately does not run the comparison. Save its output directory as the simulation checkpoint first. Then:

```
python3 native_elf/compare_traces.py replay_test/run1 replay_test/lab --repeat-prefix replay_test/run2 --output replay_test/comparison.json
```

These recorded runs use one low-priority worker capped at half one logical CPU. Observation and pacing do not consume game RNG.


## Existing corrected evidence

In the .776 checkpoint, `evidence/native_run1_*` and `evidence/native_run2_*` contain the corrected native repeats; `evidence/lab_full_final/` is the .776 Lab recording. After saving or copying the raw checkpoint, compare it with:

```
python3 native_elf/compare_traces.py evidence/native_run1 evidence/lab_full_final --repeat-prefix evidence/native_run2 --output comparison.json
```

`run_replay_tests.py` produces fresh `run1_*`, `run2_*` and `lab/` names in the output directory supplied to it. Full tests are in the editor source; the optional direct native flood oracle can be run after building the core:

```
python3 editor/ThandorMapEditor_Vulkan_Source/tools/verify_native_reachability.py reference/open-thandor-main lab_build
```

The corrected offline bootstrap still has documented differences from normal Windows startup. Native selected-state hashes and Lab rolling hashes are different formats and must not be compared against each other. This checkpoint is not a complete Windows compatibility or rendered-shadow test.

The bundle is a reproducible test checkpoint, not a complete native saved game. Replay scripts reload the map with the recorded seed and advance from tick zero; selected-state CSVs cannot restore every internal AI/model workspace at an arbitrary tick.
