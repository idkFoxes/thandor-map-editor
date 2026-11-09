#pragma once

// User-owned editor preferences, deliberately separate from game thandor.dat and
// from authored .lev/.thmap content. Unknown INI keys are ignored for forwards compatibility.
#include "thandor/simulation_lab.hpp"
#include <algorithm>
#include <charconv>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace editor {

struct EditorUserSettings {
    // "stock" stores the stable catalog level_path; "file" stores an absolute path.
    std::string last_map_type{"stock"};
    std::string last_map_value;
    thandor::SimulationLabConfig simulation_lab{};
    std::uint32_t simulation_tick_ms{50U};
    bool storage_overrides_enabled{};
    std::uint32_t xenite_storage_q4{4000U};
    std::uint32_t tritium_storage_q4{4000U};
    bool show_other_runs{};
    bool logging_enabled{};
    std::string ai_profile{"default"};
    int ui_scale_percent{}; // 0 = monitor setting, otherwise 100..300 percent
};

inline std::filesystem::path editor_user_settings_path(const std::filesystem::path& executable_dir) {
    return executable_dir / "thandor_map_editor.ini";
}

inline std::string editor_user_settings_encode(const EditorUserSettings& settings) {
    const auto& c=settings.simulation_lab;
    std::ostringstream out;
    out << "; Thandor Map Editor preferences (not part of the game or a map)\n"
        << "[map]\n"
        << "type=" << std::quoted(settings.last_map_type) << '\n'
        << "value=" << std::quoted(settings.last_map_value) << '\n'
        << "\n[editor]\n"
        << "logging_enabled=" << settings.logging_enabled << '\n'
        << "ui_scale_percent=" << settings.ui_scale_percent << '\n'
        << "ai_profile=" << settings.ai_profile << '\n'
        << "\n[simulation_lab]\n"
        << "run_count=" << c.run_count << '\n'
        << "worker_thread_limit=" << c.worker_thread_limit << '\n'
        << "duration_ticks=" << c.duration_ticks << '\n'
        << "tick_interval_ms=" << settings.simulation_tick_ms << '\n'
        << "base_seed=" << c.base_seed << '\n'
        << "vary_seed_per_run=" << c.vary_seed_per_run << '\n'
        << "parameter_index=" << c.parameter_index << '\n'
        << "sweep_mode=" << static_cast<unsigned>(c.sweep_mode) << '\n'
        << "fixed_value=" << c.fixed_value << '\n'
        << "minimum_value=" << c.minimum_value << '\n'
        << "maximum_value=" << c.maximum_value << '\n'
        << "class23_callback_step_ticks=" << c.class23_callback_step_ticks << '\n'
        << "runtime_callback_step_ticks=" << c.runtime_callback_step_ticks << '\n'
        << "fast_build_cheat=" << c.fast_build_cheat << '\n'
        << "storage_overrides_enabled=" << settings.storage_overrides_enabled << '\n'
        << "xenite_storage_q4=" << settings.xenite_storage_q4 << '\n'
        << "tritium_storage_q4=" << settings.tritium_storage_q4 << '\n'
        << "show_other_runs=" << settings.show_other_runs << '\n';
    for (std::size_t faction=0; faction<c.primary_queue_overrides.size(); ++faction) {
        const auto& override=c.primary_queue_overrides[faction];
        out << "primary_" << faction << "_enabled=" << override.enabled << '\n'
            << "primary_" << faction << "_count=" << override.count << '\n';
        // Persist all 64 entries: users may reduce and later restore the count.
        for (std::size_t i=0;i<override.army_asset_ids.size();++i)
            if (override.army_asset_ids[i]!=0U)
                out << "primary_" << faction << "_arm_" << i << '=' << override.army_asset_ids[i] << '\n';
    }
    return out.str();
}

inline std::string_view editor_ini_trim(std::string_view value) {
    const auto start=value.find_first_not_of(" \t\r");
    if(start==std::string_view::npos)return {};
    const auto end=value.find_last_not_of(" \t\r");
    return value.substr(start,end-start+1);
}

template<class T>
inline std::optional<T> editor_ini_integer(std::string_view value,T minimum,T maximum) {
    value=editor_ini_trim(value);
    T parsed{};
    const auto result=std::from_chars(value.data(),value.data()+value.size(),parsed);
    if(result.ec!=std::errc{}||result.ptr!=value.data()+value.size()||parsed<minimum||parsed>maximum)
        return std::nullopt;
    return parsed;
}

inline EditorUserSettings editor_user_settings_decode(std::string_view source,EditorUserSettings initial) {
    std::istringstream input{std::string(source)};
    std::string line,section;
    while(std::getline(input,line)){
        auto trimmed=editor_ini_trim(line);
        if(trimmed.empty()||trimmed[0]==';'||trimmed[0]=='#')continue;
        if(trimmed.front()=='['&&trimmed.back()==']'){
            section=std::string(trimmed.substr(1,trimmed.size()-2));continue;
        }
        const auto pos=trimmed.find('=');if(pos==std::string_view::npos)continue;
        const auto key=editor_ini_trim(trimmed.substr(0,pos));
        const auto value=editor_ini_trim(trimmed.substr(pos+1));
        if(section=="map"){
            std::istringstream quoted{std::string(value)};
            std::string decoded;
            if(!(quoted>>std::quoted(decoded)))continue;
            if(key=="type"&&(decoded=="stock"||decoded=="file"))initial.last_map_type=decoded;
            if(key=="value"&&decoded.size()<4096U)initial.last_map_value=decoded;
            continue;
        }
        if(section=="editor"){
            if(key=="logging_enabled"){
                if(value=="1"||value=="true")initial.logging_enabled=true;
                else if(value=="0"||value=="false")initial.logging_enabled=false;
            }else if(key=="ai_profile"){
                if(value=="default"||value=="custom")initial.ai_profile=std::string(value);
            }else if(key=="ui_scale_percent"){
                if(auto v=editor_ini_integer<int>(value,0,300);v && (*v==0||*v>=100))initial.ui_scale_percent=*v;
            }
            continue;
        }
        if(section!="simulation_lab")continue;
        auto& c=initial.simulation_lab;
        const auto uint32=[&](std::uint32_t& target,std::uint32_t min,std::uint32_t max){
            if(auto v=editor_ini_integer<std::uint32_t>(value,min,max))target=*v;
        };
        const auto boolean=[&](bool& target){
            if(value=="1"||value=="true")target=true;
            else if(value=="0"||value=="false")target=false;
        };
        const auto signed32=[&](std::int32_t& target){
            if(auto v=editor_ini_integer<std::int32_t>(value,INT32_MIN,INT32_MAX))target=*v;
        };
        if(key=="run_count")uint32(c.run_count,1,32);
        else if(key=="worker_thread_limit")uint32(c.worker_thread_limit,0,32);
        else if(key=="duration_ticks")uint32(c.duration_ticks,120,108000);
        else if(key=="tick_interval_ms")uint32(initial.simulation_tick_ms,1,5000);
        else if(key=="base_seed")uint32(c.base_seed,0,UINT32_MAX);
        else if(key=="vary_seed_per_run")boolean(c.vary_seed_per_run);
        else if(key=="parameter_index")uint32(c.parameter_index,0,255);
        else if(key=="sweep_mode"){
            if(auto v=editor_ini_integer<unsigned>(value,0,2))c.sweep_mode=static_cast<thandor::SimulationLabSweepMode>(*v);
        }
        else if(key=="fixed_value")signed32(c.fixed_value);
        else if(key=="minimum_value")signed32(c.minimum_value);
        else if(key=="maximum_value")signed32(c.maximum_value);
        else if(key=="class23_callback_step_ticks")uint32(c.class23_callback_step_ticks,1,4096);
        else if(key=="runtime_callback_step_ticks")uint32(c.runtime_callback_step_ticks,1,4096);
        else if(key=="fast_build_cheat")boolean(c.fast_build_cheat);
        else if(key=="storage_overrides_enabled")boolean(initial.storage_overrides_enabled);
        else if(key=="xenite_storage_q4")uint32(initial.xenite_storage_q4,0,UINT32_MAX);
        else if(key=="tritium_storage_q4")uint32(initial.tritium_storage_q4,0,UINT32_MAX);
        else if(key=="show_other_runs")boolean(initial.show_other_runs);
        else if(key.starts_with("primary_")){
            auto suffix=key.substr(8);
            const auto separator=suffix.find('_');if(separator==std::string_view::npos)continue;
            const auto idx=editor_ini_integer<std::size_t>(suffix.substr(0,separator),0,
                c.primary_queue_overrides.size()-1);
            if(!idx)continue;
            auto& override=c.primary_queue_overrides[*idx];
            const auto rest=suffix.substr(separator+1);
            if(rest=="enabled")boolean(override.enabled);
            else if(rest=="count")uint32(override.count,0,
                static_cast<std::uint32_t>(override.army_asset_ids.size()));
            else if(rest.starts_with("arm_")){
                const auto slot=editor_ini_integer<std::size_t>(rest.substr(4),0,
                    override.army_asset_ids.size()-1);
                if(slot)uint32(override.army_asset_ids[*slot],0,UINT32_MAX);
            }
        }
    }
    return initial;
}

inline EditorUserSettings editor_user_settings_read(const std::filesystem::path& path,
                                                    EditorUserSettings defaults) {
    std::ifstream file(path,std::ios::binary);
    if(!file)return defaults;
    // Prevent unbounded input from an accidentally corrupted or hostile INI.
    std::string data;
    data.resize(131072U);
    file.read(data.data(),static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(file.gcount()));
    return editor_user_settings_decode(data,std::move(defaults));
}

inline bool editor_user_settings_write(const std::filesystem::path& path,
                                       std::string_view content) {
    const auto temporary=std::filesystem::path(path.string()+".tmp");
    {
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        if(!output)return false;
        output.write(content.data(),static_cast<std::streamsize>(content.size()));
        output.flush();
        if(!output)return false;
    }
    std::error_code error;
    std::filesystem::rename(temporary,path,error);
    if(!error)return true;
    // Windows rename() cannot replace an existing destination. Keep the prior
    // INI until a fully-written replacement is available.
    std::filesystem::copy_file(temporary,path,std::filesystem::copy_options::overwrite_existing,error);
    std::error_code cleanup;
    std::filesystem::remove(temporary,cleanup);
    return !error;
}
}
