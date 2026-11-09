#include "editor_user_settings.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool expression,const char* message){if(!expression)throw std::runtime_error(message);}
}

int main(){
    editor::EditorUserSettings defaults;
    defaults.simulation_lab.parameter_index=103;
    const auto missing=std::filesystem::temp_directory_path()/(
        "thandor_ini_missing_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto initial=editor::editor_user_settings_read(missing,defaults);
    check(initial.simulation_lab.parameter_index==103,"missing file destroyed loaded KI baseline");

    check(!initial.logging_enabled&&initial.ui_scale_percent==0,"default editor preferences");
    editor::EditorUserSettings a;
    a.logging_enabled=true;a.ui_scale_percent=200;
    a.last_map_type="file";
    a.last_map_value="/tmp/my map/quotes\\\" and space.lev";
    auto& c=a.simulation_lab;
    c.run_count=7;c.worker_thread_limit=3;c.duration_ticks=36000;
    c.base_seed=12345;c.vary_seed_per_run=true;c.parameter_index=31;
    c.sweep_mode=thandor::SimulationLabSweepMode::MinMiddleMax;
    c.fixed_value=-42;c.minimum_value=-500;c.maximum_value=700;
    c.class23_callback_step_ticks=16;c.runtime_callback_step_ticks=2;c.fast_build_cheat=true;
    c.primary_queue_overrides[2].enabled=true;
    c.primary_queue_overrides[2].count=3;
    c.primary_queue_overrides[2].army_asset_ids[0]=300;
    c.primary_queue_overrides[2].army_asset_ids[2]=431;
    a.simulation_tick_ms=1;a.storage_overrides_enabled=true;
    a.xenite_storage_q4=42000;a.tritium_storage_q4=24;a.show_other_runs=true;

    const std::string encoded=editor::editor_user_settings_encode(a);
    const auto b=editor::editor_user_settings_decode(encoded,{});
    check(editor::editor_user_settings_encode(b)==encoded,"INI round-trip mismatch");
    const auto base=std::filesystem::temp_directory_path()/(
        "thandor_ini_test_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path=editor::editor_user_settings_path(base);
    std::filesystem::create_directories(base);
    check(editor::editor_user_settings_write(path,encoded),"failed to create INI");
    check(editor::editor_user_settings_encode(editor::editor_user_settings_read(path,{}))==encoded,
        "read after write differs");
    a.last_map_type="stock";a.last_map_value="missions/asgard.lev";
    check(editor::editor_user_settings_write(path,editor::editor_user_settings_encode(a)),"failed to replace INI");
    check(editor::editor_user_settings_read(path,{}).last_map_type=="stock","replacement missing");
    const auto damaged=editor::editor_user_settings_decode(
        "[map]\ntype=\"invalid\"\nvalue=\"still okay\"\n"
        "[simulation_lab]\nrun_count=-1\nduration_ticks=9999999\n"
        "worker_thread_limit=abc\nsweep_mode=7\nunknown=32\n"
        "primary_2_count=100000\nprimary_99_enabled=1\n"
        "run_count=5\n", defaults);
    check(damaged.last_map_type=="stock"&&damaged.last_map_value=="still okay","invalid map kind accepted");
    check(damaged.simulation_lab.run_count==5,"valid later setting ignored");
    check(damaged.simulation_lab.duration_ticks==defaults.simulation_lab.duration_ticks,
        "out-of-range value accepted");
    const auto editor_bad=editor::editor_user_settings_decode(
        "[editor]\nlogging_enabled=nope\nui_scale_percent=999\n",b);
    check(editor_bad.logging_enabled&&editor_bad.ui_scale_percent==200,"invalid display preferences accepted");
    const auto editor_off=editor::editor_user_settings_decode(
        "[editor]\nlogging_enabled=false\nui_scale_percent=0\n",b);
    check(!editor_off.logging_enabled&&editor_off.ui_scale_percent==0,"logging off/auto scale not restored");
    std::filesystem::remove_all(base);
    std::cout<<"EDITOR_USER_SETTINGS PASS roundtrip=1 last_map=1 overrides=1 invalid_input=1\n";
}
