#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <memory>
#include <filesystem>
#include <string>
#include <string_view>

namespace thandor { class MapDocument; struct SimulationLabRun; }

namespace editor {

struct JandrenatorSettings {
    int level{};
    int grace_minutes{-1};
    bool repairs{true};
    bool log{};
    bool spectate{};
    bool evade{true};
    bool formation{true};
    bool record{true};
    bool waves{true};
    bool tight_building{true};
};

struct JandrenatorToggle {
    const char* key;
    const char* alias;
    const char* label;
    const char* description;
    bool JandrenatorSettings::*value;
};

inline constexpr std::array jandrenator_toggles{
    JandrenatorToggle{"reparaturen", "repairs", "Original AI repairs", "Fix stuck pioneers and attacks, blocked purchases, and other original AI behavior.", &JandrenatorSettings::repairs},
    JandrenatorToggle{"protokoll", "log", "Log AI decisions", "Include AI decisions in each run's runner.log.", &JandrenatorSettings::log},
    JandrenatorToggle{"zuschauen", "spectate", "Spectate (AI controls your faction)", "The AI also commands your faction. The grace period is disabled.", &JandrenatorSettings::spectate},
    JandrenatorToggle{"ausweichen", "evade", "Vehicles make way", "Idle allied vehicles move aside when blocked by a friendly vehicle.", &JandrenatorSettings::evade},
    JandrenatorToggle{"aufstellung", "formation", "Group movement formations", "Group move orders assign individual free destinations, with lighter vehicles in front.", &JandrenatorSettings::formation},
    JandrenatorToggle{"aufzeichnung", "record", "Record player commands", "Record player state in player.log. Headless runs have no manual commands.", &JandrenatorSettings::record},
    JandrenatorToggle{"wellen", "waves", "Stagger attacks in waves", "From level 2, attack groups leave in up to five waves.", &JandrenatorSettings::waves},
    JandrenatorToggle{"dicht_bauen", "tight_building", "Closer player building placement", "Reduce player building clearance to 75%. Headless runs issue no manual building orders.", &JandrenatorSettings::tight_building},
};

inline constexpr auto jandrenator_ini_name = "advanced_ai_jandrenator.ini";

JandrenatorSettings decode_jandrenator_settings(std::string_view source);
std::string encode_jandrenator_settings(const JandrenatorSettings& settings);
JandrenatorSettings load_jandrenator_settings(const std::filesystem::path& directory);
void save_jandrenator_settings(const std::filesystem::path& directory, const JandrenatorSettings& settings);

std::unique_ptr<thandor::SimulationLabRun> read_jandrenator_scene(const std::filesystem::path& path);

class JandrenatorBatch {
public:
    bool running() const noexcept { return running_; }
    bool paused() const noexcept { return paused_; }
    void set_paused(bool paused);
    void cancel() noexcept { worker_.request_stop(); }
    void start(const thandor::MapDocument& map, const std::filesystem::path& game_directory,
               const std::filesystem::path& output_directory,
               const JandrenatorSettings& settings, unsigned runs, unsigned ticks,
               std::uint32_t seed, bool vary_seed, unsigned opponent_percent = 100,
               unsigned concurrency = 4, unsigned tick_ms = 0, unsigned idle_minutes = 5);
    const std::filesystem::path& output() const noexcept { return output_; }
    unsigned runs() const noexcept { return runs_; }
    unsigned ticks() const noexcept { return ticks_; }

private:
    std::filesystem::path output_;
    unsigned runs_{};
    unsigned ticks_{};
    bool paused_{};
    std::atomic<bool> running_{};
    std::jthread worker_;
};

}
