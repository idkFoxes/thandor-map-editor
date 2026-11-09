#include "editor_workspace.hpp"
#include <cmath>
#include <stdexcept>
#include <iostream>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool close(float a,float b){return std::abs(a-b)<0.02F;}
}
int main(){
    for(float width:{640.0F,1024.0F,1600.0F,2560.0F})
    for(float height:{360.0F,720.0F,960.0F,1440.0F})
    for(bool left:{false,true})for(bool right:{false,true})
    for(bool compact:{false,true})for(bool overview:{false,true}){
        const auto layout=editor::EditorWorkspaceLayout::calculate(width,height,left,right,compact,overview);
        const auto& c=layout.canvas;
        require(c.width>0&&c.height>0,"empty map viewport on resize");
        require(close(c.x,layout.assets.width),"viewport overlaps assets");
        require(close(c.x+c.width,width-layout.inspector.width),"viewport overlaps inspector");
        require(close(c.y,layout.ribbon.y+layout.ribbon.height),"viewport overlaps ribbon");
        require(close(c.y+c.height,layout.overview.y),"viewport overlaps overview");
        require(!c.contains(c.x,c.y-1)&&!c.contains(c.x,c.y+c.height),"chrome accepts map input");
        editor::Camera camera;camera.target={4,-7,2};camera.distance=38;
        const auto vp=camera.view_projection(c.aspect());
        float x=0,y=0,depth=0;
        require(editor::project_to_workspace(camera.target,vp,c,x,y,depth),"camera center invisible");
        require(close(x,c.x+c.width/2)&&close(y,c.y+c.height/2),"camera center is offset");
        for(float u:{0.1F,0.5F,0.9F})for(float v:{0.1F,0.5F,0.9F}){
            const float px=c.x+u*c.width,py=c.y+v*c.height;
            const auto ray=editor::workspace_view_ray(camera,px,py,c);
            const auto point=editor::add(ray.origin,editor::mul(ray.direction,40.0F));
            require(editor::project_to_workspace(point,vp,c,x,y,depth),"pointer ray point invisible");
            require(close(x,px)&&close(y,py),"map projection and mouse picking disagree");
        }
    }
    const auto full=editor::EditorWorkspaceLayout::calculate(1600,960,true,true,false,true);
    const auto collapsed=editor::EditorWorkspaceLayout::calculate(1600,960,true,true,true,false);
    require(collapsed.canvas.height>full.canvas.height,"collapsing chrome does not return map space");
    std::cout<<"V607 workspace resize, hit boundaries and projection/ray parity PASS\n";
}
