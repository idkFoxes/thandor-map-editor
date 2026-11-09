#pragma once

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace editor {
inline std::filesystem::path executable_directory() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return std::filesystem::current_path();
    path.resize(length);
    return std::filesystem::path(path).parent_path();
#else
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::current_path() : path.parent_path();
#endif
}

// Bundled resources use executable_directory(). User/game data belongs beside
// the external AppImage, not inside its temporary, read-only mount.
inline std::filesystem::path application_directory() {
#ifndef _WIN32
    if(const auto* image=std::getenv("APPIMAGE"); image&&*image) {
        std::error_code error;
        const auto path=std::filesystem::absolute(image,error);
        if(!error)return path.parent_path();
    }
#endif
    return executable_directory();
}

inline std::filesystem::path invocation_directory() {
#ifndef _WIN32
    if(const auto* image=std::getenv("APPIMAGE"); image&&*image)
        if(const auto* original=std::getenv("OWD"); original&&*original) {
            std::error_code error;
            const auto path=std::filesystem::absolute(original,error);
            if(!error)return path;
        }
#endif
    return std::filesystem::current_path();
}

inline bool contains_game_files(const std::filesystem::path& directory) {
    bool executable=false,archives=false;
    std::error_code error;
    for(auto it=std::filesystem::directory_iterator(directory,error);
        !error&&it!=std::filesystem::directory_iterator{};it.increment(error)) {
        if(!it->is_regular_file(error))continue;
        auto name=it->path().filename().string();
        std::transform(name.begin(),name.end(),name.begin(),
            [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
        executable|=name=="game.exe"||name=="thandor.exe";
        archives|=name.ends_with(".pck")&&name!="filme.pck";
    }
    return executable&&archives;
}

inline std::filesystem::path default_game_root() {
    for(const auto& path:{application_directory(),invocation_directory(),
                         std::filesystem::current_path(),executable_directory()})
        if(contains_game_files(path))return path;
    return application_directory();
}

inline std::filesystem::path editor_logs_directory() {
    return application_directory() / "Log";
}

inline std::filesystem::path simulation_lab_logs_directory() {
    return editor_logs_directory() / "Simlab";
}

inline std::filesystem::path project_maps_directory() {
    return application_directory() / "maps";
}
} // namespace editor
