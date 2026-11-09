#include "thandor/model.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
} // namespace

int main() try {
    thandor::ModelRuntimeSimulationProfile profile{};
    profile.class_step_q12 = 0x123;
    require(profile.class_step_q12 == 0x123,
            "ModelRuntimeSimulationProfile class_step_q12 member is unavailable");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto model_header = read_text(root / "include/thandor/model.hpp");
    const auto model_source = read_text(root / "src/core/model.cpp");
    const auto catalog_source = read_text(root / "src/core/catalog.cpp");

    require(model_header.find("class_step_q12") != std::string::npos,
            "model header does not declare class_step_q12");
    require(model_source.find("class_step_q12") != std::string::npos,
            "model parser does not populate class_step_q12");
    require(catalog_source.find("root_node->simulation.class_step_q12") !=
                std::string::npos,
            "catalog motion-profile propagation does not consume class_step_q12");

    require(model_header.find("class_speed_q12") == std::string::npos &&
            model_source.find("class_speed_q12") == std::string::npos &&
            catalog_source.find("class_speed_q12") == std::string::npos,
            "a production translation unit still references the superseded class_speed_q12 member");

    std::cout << "G385_SEMANTIC_FIELD_RENAME_INCREMENTAL_CLOSURE PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G385 checks failed: " << error.what() << '\n';
    return 1;
}
