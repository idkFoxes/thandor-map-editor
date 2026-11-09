#include "editor_app.hpp"
#include "editor_ui_scale.hpp"
#include "editor_theme.hpp"
#include "editor_paths.hpp"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <filesystem>

namespace editor {
void EditorApp::update_ui_scale(){
    float sx=1.0F,sy=1.0F;
    glfwGetWindowContentScale(window_,&sx,&sy);
    int width=0,height=0,fw=0,fh=0;
    glfwGetWindowSize(window_,&width,&height);
    glfwGetFramebufferSize(window_,&fw,&fh);
    if(width<=0||height<=0||fw<=0||fh<=0)return;
    const float framebuffer_ratio=std::max(float(fw)/width,float(fh)/height);
    const auto scale=editor_ui_scale(std::max(sx,sy),framebuffer_ratio,ui_scale_percent_);
    if(std::abs(scale.geometry-editor_ui_scale_factor)<0.01F &&
       std::abs(scale.font_density-applied_font_density_)<0.01F)return;
    // Called before NewFrame: no draw data may still reference the old atlas or style.
    vulkan_.prepare_imgui_font_update();
    editor_ui_scale_factor=scale.geometry;
    applied_font_density_=scale.font_density;
    apply_scaled_editor_theme(scale.geometry);
    auto& io=ImGui::GetIO();
    io.Fonts->Clear();
    const float raster_size=14.0F*scale.geometry*scale.font_density;
    ImFont* font=nullptr;
    // Prefer a relocatable font beside the executable, then the source-tree build path.
    const auto bundled=executable_directory()/"assets/fonts/DejaVuSans.ttf";
    if(std::filesystem::is_regular_file(bundled))
        font=io.Fonts->AddFontFromFileTTF(bundled.string().c_str(),raster_size);
#ifdef THANDOR_UI_FONT_PATH
    if(!font && std::filesystem::is_regular_file(THANDOR_UI_FONT_PATH))
        font=io.Fonts->AddFontFromFileTTF(THANDOR_UI_FONT_PATH,raster_size);
#endif
    if(!font){ImFontConfig config;config.SizePixels=raster_size;io.Fonts->AddFontDefault(&config);}
    io.FontGlobalScale=1.0F/scale.font_density;
    vulkan_.finish_imgui_font_update();
}

void EditorApp::set_logging_enabled(bool enabled){
    if(enabled==logging_enabled_)return;
    logging_enabled_=enabled;
    if(enabled){
        start_editor_performance_log();
        if(simulation_lab_active())begin_simulation_lab_diagnostics();
    }else{
        editor_performance_log_.finish(editor_performance_context(),"disabled_in_ui");
        end_simulation_lab_diagnostics("disabled_in_ui");
        simulation_lab_diagnostic_path_.clear();
    }
    save_user_settings(true);
}
} // namespace editor
