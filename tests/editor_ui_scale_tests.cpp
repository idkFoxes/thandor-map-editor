#include "editor_ui_scale.hpp"
#include "editor_workspace.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iostream>
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static bool near(float a,float b){return std::abs(a-b)<0.02F;}
int main(){
    using namespace editor;
    const auto desktop=editor_ui_scale(2,1,0);
    check(near(desktop.geometry,2)&&near(desktop.font_density,1),"4K desktop scale");
    const auto retina=editor_ui_scale(2,2,0);
    check(near(retina.geometry,1)&&near(retina.font_density,2),"Retina double scaling");
    const auto manual=editor_ui_scale(1,1,175);
    check(near(manual.geometry,1.75F),"manual scale ignored");
    const auto invalid=editor_ui_scale(std::numeric_limits<float>::quiet_NaN(),0,0);
    check(near(invalid.geometry,1),"invalid monitor scale");
    for(float scale:{1.0F,1.25F,1.5F,2.0F,3.0F}){
        const auto base=EditorWorkspaceLayout::calculate(1920,1080,true,true,false,true);
        const auto hdpi=EditorWorkspaceLayout::calculate(1920*scale,1080*scale,true,true,false,true,scale);
        check(near(hdpi.inspector.width,base.inspector.width*scale),"inspector not scaled");
        check(near(hdpi.ribbon.height,base.ribbon.height*scale),"ribbon not scaled");
        check(near(hdpi.canvas.aspect(),base.canvas.aspect()),"canvas aspect changed");
        Camera camera;camera.target={4,-7,2};camera.distance=38;
        const auto vp=camera.view_projection(hdpi.canvas.aspect());
        for(float u:{0.1F,0.5F,0.9F})for(float v:{0.1F,0.5F,0.9F}){
            const float px=hdpi.canvas.x+u*hdpi.canvas.width,py=hdpi.canvas.y+v*hdpi.canvas.height;
            const auto ray=workspace_view_ray(camera,px,py,hdpi.canvas);
            float x,y,z;
            check(project_to_workspace(add(ray.origin,mul(ray.direction,40.0F)),vp,hdpi.canvas,x,y,z),"DPI pick invisible");
            check(near(px,x)&&near(py,y),"DPI projection/pick mismatch");
        }
        editor_ui_scale_factor=scale;
        check(ui_px(0)==0&&ui_px(-1)==-1&&near(ui_px(80),80*scale),"ImGui sizing sentinels changed");
    }
    std::cout<<"UI scale / Retina / manual / invalid values / projection and picking PASS\n";
}
