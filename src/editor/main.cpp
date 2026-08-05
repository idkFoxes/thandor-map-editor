#include "editor_app.hpp"
#include "vulkan_context.hpp"

#include <imgui.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
std::filesystem::path executable_directory() {
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

int run(int argc, char** argv) {
    auto root = executable_directory();
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--root" && i + 1 < argc) root = argv[++i];
        else if (argument == "--help") {
            std::cout << "Usage: thandor-map-editor [--root <Thandor game directory>]\n";
            return 0;
        }
    }
    if (glfwInit() != GLFW_TRUE) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1600, 960, "Thandor Vulkan Map Editor", nullptr, nullptr);
    if (!window) { glfwTerminate(); throw std::runtime_error("glfwCreateWindow failed"); }
    try {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        // Keyboard arrows are reserved for map-camera positioning unless an ImGui widget is active.
        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowRounding = 4.0F;
        ImGui::GetStyle().FrameRounding = 3.0F;
        editor::VulkanContext vulkan(window);
        vulkan.initialize_imgui();
        editor::EditorApp app(window, vulkan, root);
        app.run();
        vulkan.shutdown_imgui();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    } catch (...) {
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        throw;
    }
}
}

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) {
#ifdef _WIN32
        MessageBoxA(nullptr, error.what(), "Thandor Map Editor", MB_ICONERROR | MB_OK);
#else
        std::cerr << "Thandor Map Editor: " << error.what() << '\n';
#endif
        return 1;
    }
}
