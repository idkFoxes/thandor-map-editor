#version 450
layout(location=0) in vec3 in_normal;
layout(location=1) in vec2 in_uv;
layout(location=2) flat in uint in_texture_layer;
layout(location=3) in vec4 in_color;
layout(location=6) flat in uint receivesShadow;
layout(location=5) flat in uint in_static_animation_category;
layout(set=0,binding=0) uniform Scene { mat4 view_projection; vec4 camera; vec4 material_scale; vec4 animation_controls; mat4 shadowViewProjection; vec4 shadowParameters; } scene;
layout(set=0,binding=2) uniform sampler2DArray object_textures;
layout(std430,set=0,binding=5) readonly buffer FogVisibilityBuffer {
    uvec4 header;
    uint cells[];
} fogVisibility;
layout(location=0) out vec4 out_color;

vec3 linear_to_srgb(vec3 value) {
    value = clamp(value, vec3(0.0), vec3(1.0));
    bvec3 low = lessThanEqual(value, vec3(0.0031308));
    vec3 lower = value * 12.92;
    vec3 upper = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(upper, lower, low);
}

vec3 srgb_to_linear(vec3 value) {
    value = clamp(value, vec3(0.0), vec3(1.0));
    bvec3 low = lessThanEqual(value, vec3(0.04045));
    vec3 lower = value / 12.92;
    vec3 upper = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(upper, lower, low);
}

vec3 game_byte_modulate(vec3 sampled_linear, vec3 lighting_bytes) {
    // DirectDraw/Direct3D 2 performs texture modulation and ONE/ONE resource accumulation in the
    // packed framebuffer domain. The UNORM target therefore receives encoded bytes directly.
    return linear_to_srgb(sampled_linear) * clamp(lighting_bytes, 0.0, 1.0);
}

layout(set=0,binding=6) uniform sampler2DShadow directionalShadow;
layout(location=4) in vec3 shadowWorldPosition;
float shadowVisibility() {
    if(scene.shadowParameters.x<=0.0)return 1.0;
    vec4 light=scene.shadowViewProjection*vec4(shadowWorldPosition,1.0);
    vec3 p=light.xyz/light.w;
    vec2 uv=p.xy*0.5+0.5;
    if(p.z<=0.0||p.z>=1.0||any(lessThan(uv,vec2(0.0)))||any(greaterThan(uv,vec2(1.0))))return 1.0;
    // Compare each tap against the receiver plane at that texel center. A single
    // constant reference makes steep surfaces stripe under nearest-depth PCF.
    vec3 dx=dFdx(p),dy=dFdy(p);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=abs(determinant)>1e-12?
        vec2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant:vec2(0.0);
    float texel=scene.shadowParameters.y;
    vec2 center=(floor(uv/texel)+0.5)*texel;
    float sum=0.0;
    // Explicit PCF also supports depth formats without linear filtering.
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec2 tap=center+vec2(x,y)*texel;
        float reference=p.z+dot(gradient,(tap-uv)*2.0)-scene.shadowParameters.z;
        sum+=texture(directionalShadow,vec3(tap,reference));
    }
    float edge=min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
    return 1.0-scene.shadowParameters.x*(1.0-sum/9.0)*smoothstep(0.0,0.025,edge);
}

void main() {
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
    if (texel.a < 0.025) discard;
    // Resource lighting was evaluated at the model vertices, as in the DirectDraw software packet
    // preparation path. Fragment interpolation replaces G340's per-pixel radial wash.
    out_color = vec4(game_byte_modulate(texel.rgb, in_color.rgb) * (receivesShadow!=0u?shadowVisibility():1.0), texel.a * in_color.a);
}
