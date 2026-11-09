#version 450
layout(set=0,binding=0) uniform Scene { mat4 view_projection; vec4 camera; vec4 material_scale; vec4 animation_controls; mat4 shadowViewProjection; vec4 shadowParameters; } scene;
layout(push_constant) uniform ShadowCamera { mat4 viewProjection; } light;
layout(location=0) in vec3 position;
layout(location=2) in vec2 uv;
layout(location=3) in uint layer;
layout(location=4) in uint color;
layout(location=5) in uint control;
layout(location=6) in float amplitude;
layout(location=7) in float cycle;
layout(location=8) in uint caster;
layout(location=1) out vec2 in_uv;
layout(location=2) flat out uint in_texture_layer;
layout(location=3) out vec4 in_color;
layout(location=5) flat out uint in_static_animation_category;
layout(location=6) flat out uint castsShadow;
void main(){
    gl_Position=light.viewProjection*vec4(position,1.0);
    in_uv=uv;
    if(cycle>1.0&&amplitude!=0.0){
        float phase=mod(floor(scene.animation_controls.y),cycle)/cycle;
        in_uv.y+=amplitude*(1.0-abs(phase*2.0-1.0));
    }
    in_texture_layer=layer;in_color=vec4(1.0,1.0,1.0,float(color>>24u)/255.0);
    in_static_animation_category=control>>1u;castsShadow=caster;
}
