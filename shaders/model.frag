#version 450
layout(location=0) in vec3 in_normal;
layout(location=1) in vec2 in_uv;
layout(location=2) flat in uint in_texture_layer;
layout(location=3) in vec4 in_color;
layout(location=5) flat in uint in_static_animation_category;
layout(set=0,binding=0) uniform Scene { mat4 view_projection; vec4 camera; vec4 material_scale; vec4 animation_controls; } scene;
layout(set=0,binding=2) uniform sampler2DArray object_textures;
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

void main() {
    // z hides authored animated buildings and transient simulated buildings.
    // w hides transient simulated units and the authored unit-subnode copies rebuilt at 20 Hz.
    // Static actors that merely have a weapon or movement profile are never discarded.
    if (scene.animation_controls.z > 0.5 && (in_static_animation_category & 3u) != 0u) discard;
    if (scene.animation_controls.w > 0.5 && (in_static_animation_category & 12u) != 0u) discard;
    // Linked runtime-class-10 radar assemblies are always rebuilt dynamically. Their
    // continuous child rotation is independent from simulation mode or enemy targets.
    if ((in_static_animation_category & 16u) != 0u) discard;
    uint layer_count = max(1u, uint(scene.material_scale.z));
    float layer = float(in_texture_layer % layer_count);
    vec4 texel = texture(object_textures, vec3(in_uv, layer));
    if (texel.a < 0.025) discard;
    // Resource lighting was evaluated at the model vertices, as in the DirectDraw software packet
    // preparation path. Fragment interpolation replaces G340's per-pixel radial wash.
    out_color = vec4(game_byte_modulate(texel.rgb, in_color.rgb), texel.a * in_color.a);
}
