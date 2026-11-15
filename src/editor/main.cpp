#include "editor_strings.hpp"
#include "editor_app.hpp"
#include "editor_paths.hpp"
#include "editor_theme.hpp"
#include "editor_user_settings.hpp"
#include "vulkan_context.hpp"
#include "thandor/editor_version.hpp"

#include <imgui.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using editor::tr;

int run(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << thandor::kEditorBuildLabel << '\n';
        return 0;
    }
    const auto application_dir = editor::application_directory();
    const auto settings = editor::editor_user_settings_read(editor::editor_user_settings_path(application_dir), {});
    editor::set_editor_language(settings.language, editor::executable_directory());
    auto root = editor::default_game_root();
    bool print_game_root=false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--root" && i + 1 < argc) {
            root = argv[++i];
            if(root.is_relative())root=editor::invocation_directory()/root;
        }
        else if(argument=="--print-game-root")print_game_root=true;
        else if (argument == "--help") {
            std::cout << tr("Usage: thandor-map-editor [--root <Thandor game directory>] [--version] [--print-game-root]") << '\n';
            return 0;
        }
    }
    if(print_game_root) {
        if(!editor::contains_game_files(root))
            throw std::runtime_error(tr("No game.exe/thandor.exe and PCK archives found. Put them beside the AppImage or use --root <game directory>."));
        std::cout << root.lexically_normal().string() << '\n';
        return 0;
    }
#ifdef _WIN32
    // Resolve dynamically so older Windows releases can still use GLFW's fallback.
    if(const auto user32=GetModuleHandleW(L"user32.dll")){
        using SetDpiContext=BOOL(WINAPI*)(HANDLE);
        if(const auto set_context=reinterpret_cast<SetDpiContext>(GetProcAddress(user32,"SetProcessDpiAwarenessContext")))
            set_context(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))); // per-monitor v2
    }
#endif
    if (glfwInit() != GLFW_TRUE) throw std::runtime_error(tr("glfwInit failed"));
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#ifdef GLFW_COCOA_RETINA_FRAMEBUFFER
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif
    GLFWwindow* window = glfwCreateWindow(1600, 960, thandor::kEditorWindowTitle, nullptr, nullptr);
    if (!window) { glfwTerminate(); throw std::runtime_error(tr("glfwCreateWindow failed")); }
    try {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        // Keyboard arrows are reserved for map-camera positioning unless an ImGui widget is active.
        editor::apply_editor_theme();
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

#ifdef THANDOR_EDITOR_JANDRENATOR
int jandrenator_worker_main(int argc, char** argv);
#endif

int main(int argc, char** argv) {
#ifdef THANDOR_EDITOR_JANDRENATOR
    if (argc > 1 && std::string_view(argv[1]) == "--jandrenator-worker") {
        return jandrenator_worker_main(argc - 1, argv + 1);
    }
#endif
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
