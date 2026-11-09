#include "editor_paths.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace fs=std::filesystem;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Environment {
    const char* name;
    std::optional<std::string> old;
    explicit Environment(const char* key):name(key){if(auto* value=std::getenv(key))old=value;}
    ~Environment(){if(old)setenv(name,old->c_str(),1);else unsetenv(name);}
};
int main()try{
    Environment image("APPIMAGE"),original("OWD");
    const auto cwd=fs::current_path();
    const auto temp=fs::temp_directory_path()/(
        "thandor-appimage-paths-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{fs::path temp,cwd;~Cleanup(){std::error_code ec;fs::current_path(cwd,ec);fs::remove_all(temp,ec);}}cleanup{temp,cwd};
    const auto portable=temp/"portable game with spaces",caller=temp/"caller";
    fs::create_directories(portable);fs::create_directories(caller);
    std::ofstream(portable/"Editor.AppImage").put('\0');
    std::ofstream(portable/"THANDOR.EXE").put('\0');
    std::ofstream(portable/"ENGINE.PcK").put('\0');
    std::ofstream(caller/"game.exe").put('\0');
    std::ofstream(caller/"daten.pck").put('\0');
    setenv("APPIMAGE",(portable/"Editor.AppImage").c_str(),1);
    setenv("OWD",caller.c_str(),1);
    fs::current_path(caller);
    check(editor::contains_game_files(portable),"mixed-case executable/PCK names were not recognized");
    check(editor::default_game_root()==portable,"AppImage sibling game files lost priority");
    check(editor::application_directory()==portable,"external AppImage directory was not selected");
    check(editor::executable_directory()!=portable,"bundled resource path was redirected outside");
    check(editor::editor_logs_directory()==portable/"Log","logs still target the read-only mount");
    check(editor::project_maps_directory()==portable/"maps","maps still target the read-only mount");
    fs::rename(portable/"ENGINE.PcK",portable/"FILME.PCK");
    check(!editor::contains_game_files(portable),"movie-only archive passed game-data detection");
    check(editor::default_game_root()==caller,"original working-directory fallback failed");
    unsetenv("APPIMAGE");
    check(editor::application_directory()==editor::executable_directory(),"ordinary executable paths changed");
    check(editor::invocation_directory()==caller,"OWD was trusted outside an AppImage");
    check(editor::default_game_root()==caller,"ordinary working-directory game detection failed");
    setenv("APPIMAGE",(portable/"Editor.AppImage").c_str(),1);
    fs::remove(caller/"daten.pck");
    check(editor::default_game_root()==portable,"missing-data fallback hid the external directory");
    std::cout<<"PASS: external game EXE/PCK detection, fallback, bundled resources, logs and maps\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
