#include "editor_jandrenator.hpp"

#include "editor_ai_profiles.hpp"
#include "thandor/map_document.hpp"
#include "thandor/pck.hpp"
#include "thandor/simulation_lab.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace editor {
namespace {

std::string lower_ascii(std::string_view value) {
    std::string result(value);
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

bool ini_bool(std::string_view value, bool fallback) {
    constexpr std::array yes{"an", "ja", "1", "true", "on", "ein", "yes"};
    constexpr std::array no{"aus", "nein", "0", "false", "off", "no"};
    if (std::find(yes.begin(), yes.end(), value) != yes.end()) return true;
    if (std::find(no.begin(), no.end(), value) != no.end()) return false;
    return fallback;
}

int ini_integer(std::string_view value, int fallback, int minimum, int maximum) {
    if (value.starts_with('+')) value.remove_prefix(1);
    if (value.empty()) return fallback;
    int parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return fallback;
    return std::clamp(parsed, minimum, maximum);
}



}

JandrenatorSettings decode_jandrenator_settings(std::string_view source) {
    JandrenatorSettings settings;
    if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
    std::istringstream input{std::string(source)};
    for (std::string line; std::getline(input, line);) {
        const auto content = std::string_view(line).substr(0, line.find_first_of(";#"));
        const auto equals = content.find('=');
        if (equals == std::string_view::npos) continue;
        const auto key = lower_ascii(ai_ini_trim(content.substr(0, equals)));
        const auto value = lower_ascii(ai_ini_trim(content.substr(equals + 1)));
        if (key == "stufe" || key == "level") {
            settings.level = ini_integer(value, settings.level, 0, 7);
        } else if (key == "schonfrist_minuten" || key == "schonfrist" || key == "grace_minutes") {
            settings.grace_minutes = ini_integer(value, settings.grace_minutes, -1, 600);
        } else {
            for (const auto& toggle : jandrenator_toggles) {
                if (key != toggle.key && key != toggle.alias) continue;
                auto& flag = settings.*toggle.value;
                flag = ini_bool(value, flag);
                break;
            }
        }
    }
    return settings;
}

std::string encode_jandrenator_settings(const JandrenatorSettings& settings) {
    std::ostringstream output;
    output << "# Jandrenator AI settings for OpenThandor Test15 single-player.\n"
           << "# Difficulty ranges from 0 to 7. Grace period -1 follows difficulty.\n"
           << "stufe = " << settings.level << '\n'
           << "schonfrist_minuten = " << settings.grace_minutes << '\n';
    for (const auto& toggle : jandrenator_toggles) {
        output << "\n# " << toggle.description << '\n'
               << toggle.key << " = " << (settings.*toggle.value ? "an" : "aus") << '\n';
    }
    return output.str();
}

JandrenatorSettings load_jandrenator_settings(const std::filesystem::path& directory) {
    const auto path = directory / jandrenator_ini_name;
    if (!std::filesystem::exists(path)) save_jandrenator_settings(directory, {});
    if (std::filesystem::file_size(path) > 65536) throw std::runtime_error("Jandrenator INI exceeds 64 KiB");
    const auto bytes = thandor::read_file(path);
    return decode_jandrenator_settings({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

void save_jandrenator_settings(const std::filesystem::path& directory, const JandrenatorSettings& settings) {
    ai_profile_write(directory / jandrenator_ini_name, encode_jandrenator_settings(settings));
}

std::unique_ptr<thandor::SimulationLabRun> read_jandrenator_scene(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::string version;
    auto run = std::make_unique<thandor::SimulationLabRun>();
    if (!(file >> version >> run->tick >> run->active_faction_count) ||
        (version != "JANDRENATOR_SCENE_1" && version != "JANDRENATOR_SCENE_2") || run->active_faction_count > 7) return {};
    for (auto& mask : run->faction_technology_masks)
        for (auto& word : mask)
            if (!(file >> word)) return {};
    const auto read_pose = [&](thandor::SimulationLabEffectPose& pose) {
        int x{}, y{}, z{};
        if (!(file >> x >> y >> z)) return false;
        pose.position = {float(x) / 4096.F, float(y) / 4096.F, float(z) / 4096.F};
        for (auto& component : pose.basis) {
            int q28{};
            if (!(file >> q28)) return false;
            component = float(q28) / 268435456.F;
        }
        return true;
    };
    for (std::string slot; file >> slot;) {
        if (slot == "m") {
            if (run->entities.empty() || run->entities.back().native_model_poses.size() >= 1024) return {};
            auto& pose = run->entities.back().native_model_poses.emplace_back();
            if (!(file >> pose.child_path) || !read_pose(pose.pose) ||
                !(file >> pose.primary_texture >> pose.secondary_texture)) return {};
            for (auto& offset : pose.texture_offsets)
                if (!(file >> offset)) return {};
            continue;
        }
        if (slot == "e" || slot == "s") {
            if (run->native_visuals.size() >= 8192) return {};
            auto& visual = run->native_visuals.emplace_back();
            int scale{}, beam{};
            visual.shot = slot == "s";
            if (!(file >> visual.definition_id >> visual.age >> visual.frame >> visual.tint >>
                  scale >> beam >> visual.faction) || visual.faction > 7 || !read_pose(visual.pose)) return {};
            visual.scale = float(scale) / 4096.F;
            visual.beam_length = float(beam) / 4096.F;
            continue;
        }
        if (slot == "l") {
            if (run->native_lights.size() >= 256) return {};
            auto& light = run->native_lights.emplace_back();
            int x{}, y{}, z{};
            uint64_t radius_squared{};
            if (!(file >> x >> y >> z >> radius_squared >> light.rgb)) return {};
            light.position = {float(x) / 4096.F, float(y) / 4096.F, float(z) / 4096.F};
            light.radius = static_cast<float>(std::sqrt(double(radius_squared)) / 4096.0);
            continue;
        }
        if (slot == "f") {
            std::size_t length{};
            uint64_t mask{};
            auto& occupancy = run->current_visibility_occupancy;
            if (!(file >> length >> mask) || length == 0 || length > 1048576 - occupancy.size()) return {};
            occupancy.insert(occupancy.end(), length, mask);
            continue;
        }
        if (slot == "end") {
            // The native worker rebuilds current sight on phase 7, but may
            // publish the scene later. Use the sight tick for the viewport's
            // fog replay so moving models see the same mask as the terrain.
            if (!run->current_visibility_occupancy.empty() && run->tick >= 7U)
                run->current_visibility_occupancy_tick = run->tick - (run->tick - 7U) % 8U;
            return run;
        }
        unsigned id{}, building{};
        const auto parsed = std::from_chars(slot.data(), slot.data() + slot.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != slot.data() + slot.size() ||
            run->entities.size() >= 65536) return {};
        thandor::SimulationLabEntity entity;
        int x{}, y{}, z{};
        if (!(file >> entity.army_asset_id >> entity.faction_index >> entity.runtime_model_class_id >>
              building >> x >> y >> z >> entity.actor.body_yaw_turn16 >>
              entity.root_model_integrity >> entity.root_model_maximum_integrity) ||
            entity.faction_index == 0 || entity.faction_index > 7 || building > 1) return {};
        entity.native_army_pool_slot = id;
        entity.synthetic = true;
        entity.source_placement_index = SIZE_MAX;
        entity.building = building != 0;
        entity.actor.position = {float(x) / 4096.F, float(y) / 4096.F, float(z) / 4096.F};
        run->entities.push_back(std::move(entity));
    }
    return {};
}

#ifdef THANDOR_EDITOR_JANDRENATOR
namespace {

#ifdef _WIN32
using NativeProcess = HANDLE;
#else
using NativeProcess = pid_t;
#endif

NativeProcess spawn_run(const std::filesystem::path& runner, const std::filesystem::path& directory,
                unsigned ticks, std::uint32_t seed, unsigned human, unsigned opponent_percent,
                unsigned tick_ms, unsigned idle_minutes) {
#ifdef _WIN32
    auto command = L"\"" + runner.native() + L"\" --jandrenator-worker " + std::to_wstring(ticks) + L" progress " +
                   std::to_wstring(seed) + L" jandrenator_run " + std::to_wstring(human) + L" " +
                   std::to_wstring(opponent_percent) + L" " + std::to_wstring(tick_ms) + L" ../paused " + std::to_wstring(idle_minutes);
    std::vector<wchar_t> environment;
    const auto block = GetEnvironmentStringsW();
    if (!block) throw std::system_error(GetLastError(), std::system_category(), "Read runner environment");
    for (auto entry = block; *entry; entry += std::wcslen(entry) + 1) {
        const std::wstring_view value(entry);
        if (_wcsnicmp(entry, L"OPEN_THANDOR_AI_", 16) != 0 &&
            _wcsnicmp(entry, L"OPEN_THANDOR_RECORD=", 20) != 0) {
            environment.insert(environment.end(), entry, entry + value.size() + 1);
        }
    }
    FreeEnvironmentStringsW(block);
    if (environment.empty()) environment.push_back(L'\0');
    environment.push_back(L'\0');

    SECURITY_ATTRIBUTES inherited{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const auto log = CreateFileW((directory / "runner.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                &inherited, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) throw std::system_error(GetLastError(), std::system_category(), "Open runner log");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullptr;
    startup.hStdOutput = startup.hStdError = log;
    PROCESS_INFORMATION process{};
    const bool started = CreateProcessW(runner.c_str(), command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        environment.data(), directory.c_str(), &startup, &process);
    const auto error = GetLastError();
    CloseHandle(log);
    if (!started) throw std::system_error(error, std::system_category(), "Cannot start Jandrenator runner");
    CloseHandle(process.hThread);
    return process.hProcess;
#else
    std::array args{runner.string(), std::string("--jandrenator-worker"), std::to_string(ticks), std::string("progress"),
                    std::to_string(seed), std::string("jandrenator_run"), std::to_string(human),
                    std::to_string(opponent_percent), std::to_string(tick_ms), std::string("../paused"),
                    std::to_string(idle_minutes)};
    std::array<char*, args.size() + 1> argv{};
    for (std::size_t i = 0; i < args.size(); ++i) argv[i] = args[i].data();
    std::vector<char*> environment;
    for (auto entry = environ; *entry; ++entry) {
        const std::string_view value(*entry);
        if (!value.starts_with("OPEN_THANDOR_AI_") && !value.starts_with("OPEN_THANDOR_RECORD=")) {
            environment.push_back(*entry);
        }
    }
    environment.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    auto error = posix_spawn_file_actions_init(&actions);
    if (error) throw std::system_error(error, std::generic_category(), "Cannot prepare native runner");
    error = posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    if (!error) error = posix_spawn_file_actions_addopen(&actions, 2, "runner.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    pid_t child{};
    if (!error) error = posix_spawn(&child, runner.c_str(), &actions, nullptr, argv.data(), environment.data());
    posix_spawn_file_actions_destroy(&actions);
    if (error) throw std::system_error(error, std::generic_category(), "Cannot start Jandrenator runner");
    return child;
#endif
}

struct NativeChild {
    NativeProcess process;
    std::filesystem::path directory;
};

void stage_archive(const std::filesystem::path& source, const std::filesystem::path& target) {
#ifdef _WIN32
    std::error_code error;
    std::filesystem::create_hard_link(source, target, error);
    if (error) std::filesystem::copy_file(source, target);
#else
    std::filesystem::create_symlink(source, target);
#endif
}

void write_result(const std::filesystem::path& directory, std::string_view message) {
    std::ofstream output(directory / "result");
    output << message << '\n';
}

void stop_children(const std::vector<NativeChild>& children) {
    for (const auto& child : children) {
#ifdef _WIN32
        TerminateProcess(child.process, 1);
#else
        kill(child.process, SIGKILL);
#endif
    }
    for (const auto& child : children) {
#ifdef _WIN32
        WaitForSingleObject(child.process, INFINITE);
        CloseHandle(child.process);
#else
        while (waitpid(child.process, nullptr, 0) < 0 && errno == EINTR) {}
#endif
        write_result(child.directory, "Canceled");
    }
}

}
#endif

void JandrenatorBatch::set_paused(bool paused) {
    const auto control = output_ / "paused";
    if (paused) {
        std::ofstream marker(control);
        if (!marker) throw std::runtime_error("Cannot pause Jandrenator workers.");
    } else {
        std::filesystem::remove(control);
    }
    paused_ = paused;
}

void JandrenatorBatch::start(const thandor::MapDocument& map, const std::filesystem::path& game_directory,
                           const std::filesystem::path& output_directory,
                           const JandrenatorSettings& settings, unsigned runs, unsigned ticks,
                           std::uint32_t seed, bool vary_seed, unsigned opponent_percent, unsigned concurrency,
                           unsigned tick_ms, unsigned idle_minutes) {
#ifdef THANDOR_EDITOR_JANDRENATOR
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    paused_ = false;
#ifdef _WIN32
    std::wstring executable_path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
    if (length == 0 || length == executable_path.size()) {
        throw std::system_error(GetLastError(), std::system_category(), "Locate the editor executable");
    }
    executable_path.resize(length);
    const std::filesystem::path executable(executable_path);
#else
    const std::filesystem::path executable("/proc/self/exe");
#endif
    const auto root = std::filesystem::absolute(output_directory);
    std::filesystem::create_directories(root);
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto output = root / ("batch-" + std::to_string(id));
    if (!std::filesystem::create_directory(output)) throw std::runtime_error("Batch directory already exists.");
    concurrency = std::clamp(concurrency, 1U, std::min(std::max(runs, 1U), 32U));
    output_ = output;
    runs_ = runs;
    ticks_ = ticks;
    running_ = true;
    try {
        worker_ = std::jthread([this, map, game_directory, executable, output, settings,
                               runs, ticks, seed, vary_seed, opponent_percent, concurrency, tick_ms, idle_minutes](std::stop_token stop) {
            std::vector<NativeChild> children;
            children.reserve(concurrency);
            try {
                auto entries = map.campaign_package_entries();
                // The runner gets a stable LEV entry while the LEV's own terrain path stays authored.
                entries.front().name = "level/jandrenator_run.lev";
                thandor::write_stored_pck(output / "map.pck", entries);
                const auto human = std::max(1U, map.default_human_faction());
                constexpr std::array archives{"ENGINE.PCK", "DATEN.PCK", "MODELLE.PCK",
                                              "GRAPHIK.PCK", "SOUND.PCK", "LEVEL.PCK"};
                std::array<std::filesystem::path, archives.size()> sources;
                for (const auto& file : std::filesystem::directory_iterator(game_directory)) {
                    const auto name = lower_ascii(file.path().filename().string());
                    for (std::size_t i = 0; i < archives.size(); ++i) {
                        if (name == lower_ascii(archives[i]) && file.is_regular_file()) {
                            sources[i] = std::filesystem::absolute(file.path());
                        }
                    }
                }
                for (std::size_t i = 0; i < archives.size(); ++i) {
                    if (sources[i].empty()) throw std::runtime_error(std::string("Missing game archive ") + archives[i]);
                }

                unsigned started = 0;
                while (!stop.stop_requested() && (started < runs || !children.empty())) {
                    while (!stop.stop_requested() && started < runs && children.size() < concurrency) {
                        const auto directory = output / ("run-" + std::to_string(++started));
                        std::filesystem::create_directory(directory);
                        for (std::size_t i = 0; i < archives.size(); ++i) {
                            stage_archive(sources[i], directory / archives[i]);
                        }
                        stage_archive(output / "map.pck", directory / "map.pck");
                        save_jandrenator_settings(directory, settings);
                        const auto run_seed = vary_seed ? seed + (started - 1) * 0x9E3779B9U : seed;
                        auto child = NativeChild{{}, directory};
                        child.process = spawn_run(executable, directory, ticks, run_seed, human, opponent_percent, tick_ms, idle_minutes);
                        children.push_back(std::move(child));
                    }
                    std::erase_if(children, [](const NativeChild& child) {
#ifdef _WIN32
                        if (WaitForSingleObject(child.process, 0) == WAIT_TIMEOUT) return false;
                        DWORD code{};
                        const bool success = GetExitCodeProcess(child.process, &code) && code == 0;
                        CloseHandle(child.process);
#else
                        int status{};
                        const auto result = waitpid(child.process, &status, WNOHANG);
                        if (result == 0 || (result < 0 && errno == EINTR)) return false;
                        const bool success = result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
                        if (!success)
                            write_result(child.directory, "Failed (see runner.log)");
                        else if (!std::ifstream(child.directory / "result"))
                            write_result(child.directory, "Finished");
                        return true;
                    });
                    if (!children.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (stop.stop_requested()) {
                    stop_children(children);
                    write_result(output, "Canceled");
                } else {
                    write_result(output, "Batch ended. See each run's result.");
                }
            } catch (const std::exception& error) {
                stop_children(children);
                write_result(output, error.what());
            }
            running_ = false;
        });
    } catch (...) {
        running_ = false;
        throw;
    }
#else
    (void)map; (void)game_directory; (void)output_directory; (void)settings;
    (void)runs; (void)ticks; (void)seed; (void)vary_seed; (void)opponent_percent; (void)concurrency; (void)tick_ms; (void)idle_minutes;
    throw std::runtime_error("This editor was built without Jandrenator AI.");
#endif
}

}
