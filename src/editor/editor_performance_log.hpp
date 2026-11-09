#pragma once
#include "thandor/async_diagnostic_log.hpp"
#include <array>
#include <cmath>
#include <sstream>
#include <atomic>
#include <iomanip>
#include <memory>
#include <optional>

namespace editor {
enum class EditorPerfStage : size_t {
    Frame, FrameInterval, PollEvents, Update, Ui, UiMainMenu, UiRibbon, UiLeftPanel, UiRightPanel, UiInspectorSelection, UiInspectorSimulation, UiInspectorAiKnowledge, UiLabOverlay, UiCanvasInteraction, LabSnapshotSync, LabStartSeeds, LabStartProducts, LabStartScenario, LabScenarioBlockers, LabScenarioRuntimeCatalog, LabScenarioEnvironment, LabScenarioSound, LabStartWorker, LabStartFinalize, ObjectAssembly, ScenePrepare, BeginFrame, DynamicUpload,
    StaticSort, SurfaceSort, RenderCommands, ImGuiCommands, EndFrame,
    MapLoad, MaterialLoad, TerrainRebuild, ObjectRebuild, ObjectUpload,
    DynamicBuild, Fog, Models, Shadows, Effects, DynamicSort, SceneCacheCheck, Count
};
inline constexpr std::array<const char*,size_t(EditorPerfStage::Count)> kEditorPerfStageNames{
    "frame","frame_interval","poll_events","update","ui","ui_main_menu","ui_ribbon",
    "ui_left_panel","ui_right_panel","ui_inspector_selection","ui_inspector_simulation",
    "ui_inspector_ai_knowledge","ui_lab_overlay","ui_canvas_interaction",
    "lab_snapshot_sync","lab_start_seeds","lab_start_products","lab_start_scenario",
    "lab_scenario_blockers","lab_scenario_runtime_catalog","lab_scenario_environment","lab_scenario_sound",
    "lab_start_worker","lab_start_finalize","object_assembly","scene_prepare","begin_frame_wait",
    "dynamic_upload","static_sort","surface_sort","render_commands","imgui_commands",
    "submit_present","map_load","material_load","terrain_rebuild","object_rebuild",
    "object_upload","dynamic_build","fog","models_setup","shadow_receivers","effects","dynamic_sort","scene_cache_check"};
struct EditorPerfStats {
    uint64_t calls{};double sum_ms{},max_ms{},last_ms{};
    void add(double ms) noexcept {++calls;sum_ms+=ms;max_ms=std::max(max_ms,ms);last_ms=ms;}
};
struct EditorPerfContext {
    std::string map{"<none>"};bool simulation{},lab{},paused{};
    uint32_t tick{},fog_mode{};bool shadows{},animate_units{},animate_buildings{},animate_effects{},animate_surface{};
    uint64_t placements{},model_vertices{},model_indices{},shadow_vertices{},shadow_indices{},textures{},shadow_templates{},upload_bytes{};
    uint64_t static_model_indices{},static_shadow_indices{},terrain_vertices{},field_width{},field_height{},dynamic_revision{};
    uint64_t audio_requests{},audio_submitted{},audio_inaudible{},audio_missing{},audio_host_failed{};
    std::array<uint64_t,17> object_upload{};
    std::array<uint64_t,7> scene_cache{};
    std::array<uint64_t,4> canvas_cache{};
    std::array<uint64_t,3> source_reuse{};
    std::array<uint64_t,4> object_layer_identity{};
    uint64_t lab_snapshot_dirty_coalesced{};
    uint64_t lab_selected_render_variants{};
    uint64_t generated_shadow_cache_hits{},generated_shadow_cache_misses{},generated_shadow_cache_entries{};
    uint64_t generated_shadow_compaction_saved_vertices{};
    uint64_t generated_shadow_surface_culled_triangles{};
    uint64_t generated_shadow_dynamic_culled_triangles{};
    uint64_t generated_shadow_pose_cache_hits{},generated_shadow_pose_cache_misses{};
    uint64_t generated_shadow_pose_cache_reused_vertices{};
    uint64_t dynamic_texture_family_fast_hits{},dynamic_texture_family_fast_misses{};
    uint64_t lab_product_cache_hits{},lab_product_cache_misses{},lab_product_cache_assets{};
    uint64_t lab_runtime_catalog_cache_hits{},lab_runtime_catalog_cache_misses{},lab_runtime_catalog_cache_ready{};
    std::array<float,6> camera{};std::array<float,2> viewport{};
};
class EditorPerformanceLog {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr uint64_t kDefaultByteLimit=64U*1024U*1024U;
    class Scope {
    public:
        Scope(EditorPerformanceLog& log,EditorPerfStage stage):log_(log),stage_(stage),start_(log.active()?Clock::now():Clock::time_point{}){}
        ~Scope(){if(start_!=Clock::time_point{})log_.record(stage_,std::chrono::duration<double,std::milli>(Clock::now()-start_).count());}
        Scope(const Scope&)=delete;
    private:EditorPerformanceLog& log_;EditorPerfStage stage_;Clock::time_point start_;
    };
    bool active() const noexcept {return log_ && !capped_->load();}
    Scope measure(EditorPerfStage stage){return Scope(*this,stage);}
    bool start(const std::filesystem::path& file,const std::string& version,uint64_t limit=kDefaultByteLimit) noexcept {
        try{
            close("restart");
            window_={};session_={};last_frame_finish_.reset();
            frames_=presented_=not_presented_=slow16_=slow33_=slow100_=samples_=0;
            created_=last_sample_=Clock::now();
            capped_=std::make_shared<std::atomic_bool>(false);
            std::ostringstream header;
            header<<"THANDOR EDITOR PERFORMANCE "<<version<<"\n"
                <<"LOG_POLICY sample_wall_ms=1000 async_format=1 queue_max=4 max_bytes="<<limit
                <<" session_maxima_retained=1 cpu_timings=1 gpu_timestamps=unavailable inclusive_scopes_overlap=1\n"
                <<"START startup_elapsed_ms="<<elapsed_ms(created_,Clock::now())
                <<" hardware_threads="<<std::thread::hardware_concurrency()<<'\n';
            log_=std::make_unique<thandor::AsyncDiagnosticLog>(file,header.str());
            path_=file.string();status_="Recording";limit_=limit;return true;
        }catch(const std::exception& error){status_=std::string("Log unavailable: ")+error.what();return false;}
    }
    void unavailable(const std::string& message) noexcept {
        try{status_="Log unavailable: "+message;}catch(...){}
    }
    void record(EditorPerfStage stage,double ms) noexcept {
        if(!active()||!std::isfinite(ms)||ms<0)return;
        window_[size_t(stage)].add(ms);session_[size_t(stage)].add(ms);
    }
    void frame(bool presented,double ms,Clock::time_point now=Clock::now()) noexcept {
        if(!active())return;
        record(EditorPerfStage::Frame,ms);++frames_;presented_+=presented;not_presented_+=!presented;
        const double interval=last_frame_finish_?elapsed_ms(*last_frame_finish_,now):ms;
        if(last_frame_finish_)record(EditorPerfStage::FrameInterval,interval);
        last_frame_finish_=now;
        slow16_+=interval>16.667;slow33_+=interval>33.333;slow100_+=interval>100;
    }
    bool due(Clock::time_point now=Clock::now()) const noexcept {
        return active()&&now-last_sample_>=std::chrono::seconds(1);
    }
    bool sample(EditorPerfContext context,bool final=false,Clock::time_point now=Clock::now()) {
        if(!log_||(!final&&!due(now)))return false;
        context.map.resize(std::min<size_t>(context.map.size(),256));
        for(auto& c:context.map)if(static_cast<unsigned char>(c)<32)c='_';
        last_context_=context;
        const auto window=window_,session=session_;const auto writer=log_->stats();
        const auto frames=frames_,presented=presented_,not_presented=not_presented_;
        const auto slow16=slow16_,slow33=slow33_,slow100=slow100_;
        const auto number=++samples_;const double uptime=elapsed_ms(created_,now),duration=elapsed_ms(last_sample_,now);
        const auto capped=capped_;const auto limit=limit_;
        const bool accepted=log_->enqueue([=](std::ostream& out){
            if(capped->load())return;
            if(out.tellp()>=std::streamoff(limit)){out<<"LIMIT max_bytes="<<limit<<" logging_stopped=1\n";capped->store(true);return;}
            out<<"EDITOR_PERF sample="<<number<<" final="<<final<<" uptime_ms="<<uptime<<" window_ms="<<duration
                <<" mode="<<(context.lab?"lab":context.simulation?"simulation":"editor")
                <<" map="<<std::quoted(context.map)<<" tick="<<context.tick<<" paused="<<context.paused
                <<" session_frames="<<frames<<" session_presented="<<presented<<" session_not_presented="<<not_presented
                <<" session_over_16ms="<<slow16<<" session_over_33ms="<<slow33<<" session_over_100ms="<<slow100;
            for(size_t i=0;i<window.size();++i){const auto& w=window[i];const auto& s=session[i];const auto* name=kEditorPerfStageNames[i];
                out<<' '<<name<<"_calls="<<w.calls<<' '<<name<<"_avg_ms="<<(w.calls?w.sum_ms/w.calls:0)
                    <<' '<<name<<"_max_ms="<<w.max_ms<<' '<<name<<"_session_calls="<<s.calls
                    <<' '<<name<<"_session_max_ms="<<s.max_ms;}
            constexpr std::array<const char*,17> upload_names{
                "object_update_calls","object_update_skipped","object_array_recreates","object_partial_uploads",
                "object_layers_uploaded","object_bytes_uploaded","object_buffer_writes","object_idle_waits",
                "object_layers_resampled","object_layers_reused","object_array_capacity","object_last_dirty_layers",
                "object_layers_relocated","object_frame_fence_waits",
                "object_frame_slot_uploads","object_frame_slot_skips","object_async_transfer_copies"};
            for(size_t i=0;i<upload_names.size();++i)out<<' '<<upload_names[i]<<'='<<context.object_upload[i];
            constexpr std::array<const char*,7> cache_names{
                "scene_model_cache_hits","scene_model_cache_misses","scene_view_cache_misses",
                "scene_fog_cache_hits","scene_shadow_cache_hits","scene_cache_bytes",
                "scene_plain_geometry_fast_hits"};
            for(size_t i=0;i<cache_names.size();++i)out<<' '<<cache_names[i]<<'='<<context.scene_cache[i];
            constexpr std::array<const char*,4> canvas_names{
                "sprite_canvas_cache_hits","sprite_canvas_cache_misses",
                "sprite_canvas_cache_evictions","sprite_canvas_cache_bytes"};
            for(size_t i=0;i<canvas_names.size();++i)
                out<<' '<<canvas_names[i]<<'='<<context.canvas_cache[i];
            out<<" object_source_reuse_hits="<<context.source_reuse[0]
                <<" object_source_reuse_misses="<<context.source_reuse[1]
                <<" object_source_reuse_bytes="<<context.source_reuse[2];
            out<<" object_layer_stable="<<context.object_layer_identity[0]
                <<" object_layer_moved="<<context.object_layer_identity[1]
                <<" object_layer_added="<<context.object_layer_identity[2]
                <<" object_layer_removed="<<context.object_layer_identity[3]
                <<" lab_snapshot_dirty_coalesced="<<context.lab_snapshot_dirty_coalesced
                <<" lab_selected_render_variants="<<context.lab_selected_render_variants;
            out<<" dynamic_texture_family_fast_hits="<<context.dynamic_texture_family_fast_hits
               <<" dynamic_texture_family_fast_misses="<<context.dynamic_texture_family_fast_misses;
            out<<" generated_shadow_cache_hits="<<context.generated_shadow_cache_hits
               <<" generated_shadow_cache_misses="<<context.generated_shadow_cache_misses
               <<" generated_shadow_cache_entries="<<context.generated_shadow_cache_entries
               <<" generated_shadow_compaction_saved_vertices="<<context.generated_shadow_compaction_saved_vertices
               <<" generated_shadow_surface_culled_triangles="<<context.generated_shadow_surface_culled_triangles
               <<" generated_shadow_dynamic_culled_triangles="<<context.generated_shadow_dynamic_culled_triangles
               <<" generated_shadow_pose_cache_hits="<<context.generated_shadow_pose_cache_hits
               <<" generated_shadow_pose_cache_misses="<<context.generated_shadow_pose_cache_misses
               <<" generated_shadow_pose_cache_reused_vertices="<<context.generated_shadow_pose_cache_reused_vertices;
            out<<" lab_product_cache_hits="<<context.lab_product_cache_hits
               <<" lab_product_cache_misses="<<context.lab_product_cache_misses
               <<" lab_product_cache_assets="<<context.lab_product_cache_assets
               <<" lab_runtime_catalog_cache_hits="<<context.lab_runtime_catalog_cache_hits
               <<" lab_runtime_catalog_cache_misses="<<context.lab_runtime_catalog_cache_misses
               <<" lab_runtime_catalog_cache_ready="<<context.lab_runtime_catalog_cache_ready;
            out<<" field_width="<<context.field_width<<" field_height="<<context.field_height
                <<" terrain_vertices="<<context.terrain_vertices<<" static_model_indices="<<context.static_model_indices
                <<" static_shadow_indices="<<context.static_shadow_indices<<" dynamic_revision="<<context.dynamic_revision
                <<" fog_mode="<<context.fog_mode<<" projected_shadows="<<context.shadows
                <<" animate_units="<<context.animate_units<<" animate_buildings="<<context.animate_buildings
                <<" animate_effects="<<context.animate_effects<<" animate_surface="<<context.animate_surface
                <<" placements="<<context.placements<<" dynamic_model_vertices="<<context.model_vertices<<" dynamic_model_indices="<<context.model_indices
                <<" shadow_vertices="<<context.shadow_vertices<<" shadow_indices="<<context.shadow_indices
                <<" texture_layers="<<context.textures<<" shadow_templates="<<context.shadow_templates<<" dynamic_upload_bytes_estimate="<<context.upload_bytes
                <<" camera="<<context.camera[0]<<','<<context.camera[1]<<','<<context.camera[2]<<','<<context.camera[3]<<','<<context.camera[4]<<','<<context.camera[5]
                <<" viewport="<<context.viewport[0]<<','<<context.viewport[1]
                <<" audio_requests="<<context.audio_requests<<" audio_submitted="<<context.audio_submitted
                <<" audio_inaudible="<<context.audio_inaudible<<" audio_missing="<<context.audio_missing<<" audio_host_failed="<<context.audio_host_failed
                <<" log_written="<<writer.written<<" log_coalesced="<<writer.coalesced<<" log_failures="<<writer.failures
                <<" writer_last_ms="<<writer.last_write_ms<<" writer_peak_ms="<<writer.peak_write_ms<<'\n';
        },final);
        if(accepted){window_={};last_sample_=now;}return accepted;
    }
    void error(std::string message) noexcept {
        if(!log_)return;
        try{message.resize(std::min<size_t>(message.size(),1024));for(auto& c:message)if(static_cast<unsigned char>(c)<32)c='_';log_->enqueue([message=std::move(message)](std::ostream& out){out<<"ERROR message="<<std::quoted(message)<<'\n';},true);}catch(...){}
    }
    void close(std::string reason="editor_closed") noexcept {
        if(!log_)return;
        try{sample(last_context_,true);log_->close(reason);}catch(...){}log_.reset();
    }
    void finish(EditorPerfContext context,std::string reason="editor_closed") noexcept {
        last_context_=std::move(context);close(std::move(reason));
    }
    ~EditorPerformanceLog(){close("editor_destroyed");}
    const std::string& path()const noexcept{return path_;}
    const std::string& status()const noexcept{return status_;}
    bool capped()const noexcept{return capped_->load();}
private:
    static double elapsed_ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
    std::optional<Clock::time_point> last_frame_finish_;
    Clock::time_point created_{Clock::now()},last_sample_{created_};
    std::array<EditorPerfStats,size_t(EditorPerfStage::Count)> window_{},session_{};
    uint64_t frames_{},presented_{},not_presented_{},slow16_{},slow33_{},slow100_{},samples_{},limit_{kDefaultByteLimit};
    std::unique_ptr<thandor::AsyncDiagnosticLog> log_;std::string path_,status_{"Starts with editor session"};
    std::shared_ptr<std::atomic_bool> capped_{std::make_shared<std::atomic_bool>(false)};
    EditorPerfContext last_context_{};
};
} // namespace editor
