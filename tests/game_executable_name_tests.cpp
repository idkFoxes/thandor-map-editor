#include "thandor/catalog.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("thandor_game_executable_test_" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void create_file(const std::filesystem::path& path) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("failed to create executable-name fixture");
    stream << "MZ";
}

} // namespace

int main() {
    try {
        TemporaryDirectory root;
        require(!thandor::has_supported_game_executable(root.path()),
                "empty directory must not be accepted as a game root");

        create_file(root.path() / "THANDOR.EXE");
        require(thandor::has_supported_game_executable(root.path()),
                "THANDOR.EXE must be accepted case-insensitively");
        const auto catalog = thandor::AssetCatalog::scan_game_root(root.path());
        require(catalog.assets().empty(),
                "an otherwise empty THANDOR.EXE root should scan as an empty catalog");

        std::filesystem::remove(root.path() / "THANDOR.EXE");
        create_file(root.path() / "GaMe.ExE");
        require(thandor::has_supported_game_executable(root.path()),
                "game.exe must remain accepted case-insensitively");

        std::filesystem::remove(root.path() / "GaMe.ExE");
        std::filesystem::create_directory(root.path() / "thandor.exe");
        require(!thandor::has_supported_game_executable(root.path()),
                "a directory named thandor.exe must not be accepted");

        std::cout << "game executable name tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "game executable name tests failed: " << error.what() << '\n';
        return 1;
    }
}
