#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace editor {
inline float text_relative_luminance(const std::array<float,3>& rgb){
    const auto linear=[](float value){return value<=0.04045F?value/12.92F:std::pow((value+0.055F)/1.055F,2.4F);};
    return 0.2126F*linear(rgb[0])+0.7152F*linear(rgb[1])+0.0722F*linear(rgb[2]);
}
// UI text only. Retain the faction hue while lifting dark retail frontend
// colours for the editor's dark panels and hovered menu backgrounds.
inline std::array<float,3> readable_editor_text_rgb(uint32_t rgb){
    const std::array<float,3> base{float((rgb>>16U)&255U)/255.F,float((rgb>>8U)&255U)/255.F,float(rgb&255U)/255.F};
    if(text_relative_luminance(base)>=0.5F)return base;
    float low=0.F,high=1.F;
    for(int i=0;i<12;++i){
        const float mix=(low+high)*0.5F;
        const std::array<float,3> candidate{base[0]+(1-base[0])*mix,base[1]+(1-base[1])*mix,base[2]+(1-base[2])*mix};
        if(text_relative_luminance(candidate)<0.5F)low=mix;else high=mix;
    }
    return {base[0]+(1-base[0])*high,base[1]+(1-base[1])*high,base[2]+(1-base[2])*high};
}
}
