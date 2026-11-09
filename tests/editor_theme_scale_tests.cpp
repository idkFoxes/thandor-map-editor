#include "editor_theme.hpp"
#include <cmath>
#include <stdexcept>
#include <iostream>
static void check(float a,float b,const char* message){if(std::abs(a-b)>0.1F)throw std::runtime_error(message);}
int main(){
    ImGui::CreateContext();
    const ImGuiStyle baseline;
    for(float scale:{2.0F,1.5F,2.0F,1.0F,3.0F,2.0F}){
        editor::apply_scaled_editor_theme(scale);
        const auto& style=ImGui::GetStyle();
        check(style.WindowMinSize.x,std::floor(baseline.WindowMinSize.x*scale),"minimum window width accumulated");
        check(style.WindowMinSize.y,std::floor(baseline.WindowMinSize.y*scale),"header minimum height accumulated");
        check(style.IndentSpacing,std::floor(baseline.IndentSpacing*scale),"indentation accumulated");
        check(style.FramePadding.x,std::floor(8*scale),"theme padding not scaled");
    }
    ImGui::DestroyContext();
    std::cout<<"Repeated live style scaling restores the complete baseline PASS\n";
}
