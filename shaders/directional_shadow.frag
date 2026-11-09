#version 450
layout(location=1) in vec2 in_uv;
layout(location=2) flat in uint in_texture_layer;
layout(location=3) in vec4 in_color;
layout(location=5) flat in uint in_static_animation_category;
layout(location=6) flat in uint castsShadow;
layout(set=0,binding=0) uniform Scene { mat4 view_projection; vec4 camera; vec4 material_scale; vec4 animation_controls; mat4 shadowViewProjection; vec4 shadowParameters; } scene;
layout(set=0,binding=2) uniform sampler2DArray object_textures;
layout(std430,set=0,binding=5) readonly buffer FogVisibilityBuffer { uvec4 header; uint cells[]; } fogVisibility;
void main(){
    if(castsShadow==0u)discard;
    // z/w are tiny integer masks. Authored animation and live simulation replacement must be
    // suppressed independently: a Lab snapshot may own a dynamic replacement for one actor while
    // unrelated authored animated buildings/units remain static. The former broad boolean made
    // those unrelated static models disappear.
    uint building_hide_mask = uint(scene.animation_controls.z + 0.5); // bit0 authored, bit1 simulated
    uint unit_hide_mask = uint(scene.animation_controls.w + 0.5);     // bit0 simulated, bit1 authored
    if ((building_hide_mask & 1u) != 0u && (in_static_animation_category & 1u) != 0u) discard;
    if ((building_hide_mask & 2u) != 0u && (in_static_animation_category & 2u) != 0u) discard;
    if ((unit_hide_mask & 1u) != 0u && (in_static_animation_category & 4u) != 0u) discard;
    if ((unit_hide_mask & 2u) != 0u && (in_static_animation_category & 8u) != 0u) discard;
    // Linked runtime-class-10 radar assemblies are always rebuilt dynamically. Their
    // continuous child rotation is independent from simulation mode or enemy targets.
    if ((in_static_animation_category & 16u) != 0u) discard;
    // Fog preview routes every world placement, including Element/General scenery, through the
    // dynamic root-visibility path. Suppress only those tagged static copies while fog is enabled.
    if (fogVisibility.header.x != 0u && (in_static_animation_category & 32u) != 0u) discard;
    uint layer_count = max(1u, uint(scene.material_scale.z));
    float layer = float(in_texture_layer % layer_count);
    vec4 texel = texture(object_textures, vec3(in_uv, layer));
    if (texel.a * in_color.a < 0.5) discard;
}
