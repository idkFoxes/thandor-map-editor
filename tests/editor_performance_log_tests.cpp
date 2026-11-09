#include "editor_performance_log.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace editor;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::string read(const std::filesystem::path& path){
    std::ifstream in(path);return {(std::istreambuf_iterator<char>(in)),{}};
}
int main()try{
    const auto dir=std::filesystem::temp_directory_path()/
        ("thandor-editor-perf-"+std::to_string(EditorPerformanceLog::Clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    const auto before=EditorPerformanceLog::Clock::now()-std::chrono::seconds(5);
    {
        EditorPerformanceLog log;
        check(log.start(dir/"idle.log","test"),"idle logger failed to open");
        check(!log.due(before),"logger sampled before its first interval");
        log.record(EditorPerfStage::Ui,250);
        log.record(EditorPerfStage::Ui,-1);
        log.record(EditorPerfStage::Ui,std::numeric_limits<double>::quiet_NaN());
        log.record(EditorPerfStage::ObjectRebuild,800);
        log.record(EditorPerfStage::UiLabOverlay,27);
        log.record(EditorPerfStage::UiInspectorSelection,12);
        log.record(EditorPerfStage::UiInspectorSimulation,140);
        log.record(EditorPerfStage::UiInspectorAiKnowledge,8);
        log.record(EditorPerfStage::LabSnapshotSync,9);
        log.record(EditorPerfStage::ObjectAssembly,64);
        log.record(EditorPerfStage::LabStartSeeds,11);
        log.record(EditorPerfStage::LabStartProducts,23);
        log.record(EditorPerfStage::LabStartScenario,37);
        log.record(EditorPerfStage::LabScenarioBlockers,3);
        log.record(EditorPerfStage::LabScenarioRuntimeCatalog,29);
        log.record(EditorPerfStage::LabScenarioEnvironment,5);
        log.record(EditorPerfStage::LabScenarioSound,1);
        log.record(EditorPerfStage::LabStartWorker,71);
        log.record(EditorPerfStage::LabStartFinalize,5);
        log.frame(true,260,before);
        log.frame(false,5,before+std::chrono::milliseconds(200));
        EditorPerfContext context;context.map="Idle\nMap";context.shadow_templates=17;context.scene_cache={101,7,3,104,104,123456};
        context.object_layer_identity={10,8,4,3};context.lab_snapshot_dirty_coalesced=7;
        context.generated_shadow_cache_hits=12;context.generated_shadow_cache_misses=4;context.generated_shadow_cache_entries=3;context.generated_shadow_compaction_saved_vertices=1234;
        context.generated_shadow_surface_culled_triangles=23;
        context.generated_shadow_dynamic_culled_triangles=42;
        context.dynamic_texture_family_fast_hits=91;
        context.dynamic_texture_family_fast_misses=3;
        context.object_upload[7]=3;context.object_upload[13]=5;
        // Boundary snapshots cannot coalesce: validate ownership independently of writer speed.
        check(log.sample(context,true),"first snapshot rejected");
        context.map="mutated";context.shadow_templates=99;context.scene_cache={};
        log.record(EditorPerfStage::Ui,2);
        log.frame(true,3);
        log.error("test\nerror");
        log.finish(context,"idle_test");
    }
    const auto text=read(dir/"idle.log");
    check(text.find("mode=editor map=\"Idle_Map\"")!=std::string::npos,"idle immutable context lost");
    check(text.find("shadow_templates=17")!=std::string::npos,"writer read mutated context");
    check(text.find("scene_model_cache_hits=101 scene_model_cache_misses=7 scene_view_cache_misses=3 scene_fog_cache_hits=104 scene_shadow_cache_hits=104 scene_cache_bytes=123456")!=std::string::npos,"immutable cache telemetry lost");
    const auto final=text.substr(text.rfind("EDITOR_PERF"));
    check(final.find("final=1")!=std::string::npos&&final.find("session_frames=3 session_presented=2 session_not_presented=1")!=std::string::npos,"final partial frame counters lost");
    check(final.find("ui_calls=1 ui_avg_ms=2 ui_max_ms=2 ui_session_calls=2 ui_session_max_ms=250")!=std::string::npos,"window reset erased the session hitch or admitted invalid durations");
    check(final.find("object_rebuild_calls=0")!=std::string::npos&&final.find("object_rebuild_session_max_ms=800")!=std::string::npos,"idle rebuild maxima lost");
    check(text.find("dynamic_texture_family_fast_hits=91 dynamic_texture_family_fast_misses=3")!=std::string::npos,
        "scene-local material family lookup telemetry missing");
    check(text.find("generated_shadow_compaction_saved_vertices=1234")!=std::string::npos,
        "generated shadow compaction telemetry missing");
    check(text.find("generated_shadow_surface_culled_triangles=23")!=std::string::npos &&
          text.find("generated_shadow_dynamic_culled_triangles=42")!=std::string::npos,
        "shadow surface rejection telemetry missing");
    check(text.find("generated_shadow_cache_hits=12 generated_shadow_cache_misses=4 generated_shadow_cache_entries=3")!=std::string::npos,
        "generated shadow cache telemetry missing");
    check(text.find("object_idle_waits=3")!=std::string::npos &&
          text.find("object_frame_fence_waits=5")!=std::string::npos,
        "device idle versus frame-fence diagnostics missing");
    check(text.find("object_layer_stable=10 object_layer_moved=8 object_layer_added=4 object_layer_removed=3 lab_snapshot_dirty_coalesced=7")!=std::string::npos,
        "semantic layer diagnostics missing");
    check(text.find("ui_inspector_selection_session_max_ms=12")!=std::string::npos &&
          text.find("ui_inspector_simulation_session_max_ms=140")!=std::string::npos &&
          text.find("ui_inspector_ai_knowledge_session_max_ms=8")!=std::string::npos,
        "inspector substage diagnostics missing");
    check(text.find("ui_lab_overlay_session_max_ms=27")!=std::string::npos &&
          text.find("lab_snapshot_sync_session_max_ms=9")!=std::string::npos &&
          text.find("object_assembly_session_max_ms=64")!=std::string::npos,
        "nested editor/SimLab timing scopes missing");
    check(text.find("lab_start_seeds_session_max_ms=11")!=std::string::npos &&
        text.find("lab_start_products_session_max_ms=23")!=std::string::npos &&
        text.find("lab_start_scenario_session_max_ms=37")!=std::string::npos &&
        text.find("lab_start_worker_session_max_ms=71")!=std::string::npos &&
        text.find("lab_start_finalize_session_max_ms=5")!=std::string::npos,
        "simulation startup stages missing from the performance log");
    check(text.find("lab_scenario_runtime_catalog_session_max_ms=29")!=std::string::npos &&
          text.find("lab_scenario_environment_session_max_ms=5")!=std::string::npos &&
          text.find("lab_scenario_blockers_session_max_ms=3")!=std::string::npos &&
          text.find("lab_scenario_sound_session_max_ms=1")!=std::string::npos,
          "scenario stage breakdown missing");
    check(text.find("frame_interval_max_ms=200")!=std::string::npos,"whole-frame cadence missing");
    check(text.find("ERROR message=\"test_error\"")!=std::string::npos&&text.ends_with("END reason=idle_test\n"),"error or final flush lost");
    {
        EditorPerformanceLog log;
        check(log.start(dir/"periodic.log","test"),"periodic open failed");
        const auto now=EditorPerformanceLog::Clock::now();
        check(!log.sample({}),"idle sample ignored the one-second interval");
        check(log.due(now+std::chrono::seconds(2)),"idle sampling depends on simulation");
        check(log.sample({},false,now+std::chrono::seconds(2)),"ordinary sample rejected");
        check(!log.due(now+std::chrono::milliseconds(2500)),"periodic window did not reset");
        log.close();
    }
    {
        EditorPerformanceLog log;
        check(log.start(dir/"limited.log","test",512),"limited open failed");
        for(unsigned i=0;i<32;++i)check(log.sample({},true),"bounded snapshot rejected");
        log.close();check(log.capped(),"byte cap not enforced");
    }
    const auto limited=read(dir/"limited.log");const auto marker=limited.find("LIMIT max_bytes=512 logging_stopped=1");
    check(marker!=std::string::npos&&limited.find("LIMIT ",marker+1)==std::string::npos,"limit marker missing or repeated");
    check(limited.size()<16384,"bounded log exceeded one sample of overshoot");
    {
        EditorPerformanceLog log;
        check(!log.start(dir/"absent"/"bad.log","test"),"invalid path accepted");
        check(log.status().find("Log unavailable:")==0&&!log.sample({}),"optional open failure was not contained");
        log.unavailable("directory denied");check(log.status()=="Log unavailable: directory denied","directory failure not visible");
        log.frame(false,2);log.close();
    }
    {
        EditorPerformanceLog log;
        check(!log.active()&&!log.due(),"disabled logger is active");
        log.record(EditorPerfStage::Ui,999);log.frame(true,999);
        check(!log.sample({},true),"disabled logger wrote a sample");
        check(log.start(dir/"toggle_first.log","test"),"enable failed");
        log.record(EditorPerfStage::Ui,7);log.frame(true,7);log.close("disabled_in_ui");
        const auto stopped=read(dir/"toggle_first.log");
        log.record(EditorPerfStage::Ui,999);log.frame(true,999);
        check(read(dir/"toggle_first.log")==stopped,"disabled logger kept writing");
        check(log.start(dir/"toggle_second.log","test"),"re-enable failed");
        log.record(EditorPerfStage::Ui,2);log.frame(true,2);log.close();
        const auto restarted=read(dir/"toggle_second.log");
        check(restarted.find("session_frames=1")!=std::string::npos,"re-enable retained prior frame counters");
        check(restarted.find("ui_session_max_ms=2")!=std::string::npos,"disabled timing leaked into restarted log");
    }
    std::filesystem::remove_all(dir);
    std::cout<<"EDITOR_PERFORMANCE_LOG PASS idle periodic immutable peaks cadence final_flush error cap unavailable\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
