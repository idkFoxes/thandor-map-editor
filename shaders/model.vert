#version 450
layout(set=0,binding=0) uniform Scene { mat4 view_projection; vec4 camera; vec4 material_scale; vec4 animation_controls; mat4 shadowViewProjection; vec4 shadowParameters; } scene;
layout(location=0) in vec3 in_position;
layout(location=1) in vec3 in_normal;
layout(location=2) in vec2 in_uv;
layout(location=3) in uint in_texture_layer;
layout(location=4) in uint in_argb;
layout(location=5) in uint in_direct_tint_bypass;
layout(location=6) in float in_animation_v_amplitude;
layout(location=7) in float in_animation_cycle_ticks;
layout(location=8) in uint in_casts_shadow;
layout(location=4) out vec3 shadowWorldPosition;
layout(location=6) flat out uint receivesShadow;

struct ResourceShadingRecord {
    vec4 positionRadius;
    vec4 colorPeriod;
    vec4 phaseGrowStopRelease;
};
layout(std430,set=0,binding=3) readonly buffer ResourceShadingBuffer {
    uvec4 header;
    ResourceShadingRecord records[];
} resourceShading;

layout(location=0) out vec3 out_normal;
layout(location=1) out vec2 out_uv;
layout(location=2) flat out uint out_texture_layer;
layout(location=3) out vec4 out_color;
layout(location=5) flat out uint out_static_animation_category;

vec4 unpack_argb(uint value) {
    return vec4(float((value >> 16u) & 255u), float((value >> 8u) & 255u),
                float(value & 255u), float((value >> 24u) & 255u)) / 255.0;
}

float direct_draw_radius_at_tick(ResourceShadingRecord record, float current_tick) {
    float cycle = max(2.0, floor(record.colorPeriod.w + 0.5));
    float spawn = floor(record.phaseGrowStopRelease.x + 0.5);
    float age = mod(floor(current_tick) - spawn + cycle, cycle);
    float target_q12 = floor(record.positionRadius.w * 4096.0 + 0.5);
    float grow = max(1.0, floor(record.phaseGrowStopRelease.y + 0.5));
    float stop = floor(record.phaseGrowStopRelease.z + 0.5);
    float release = max(1.0, floor(record.phaseGrowStopRelease.w + 0.5));
    float radius_q12 = 0.0;
    if (age < grow) radius_q12 = floor(target_q12 * age / grow);
    else if (age < stop) radius_q12 = target_q12;
    else if (age < stop + release)
        radius_q12 = floor(target_q12 * (stop + release - age) / release);
    return radius_q12 / 4096.0;
}

float direct_draw_lookup_index(ResourceShadingRecord record, vec3 position) {
    float radius = direct_draw_radius_at_tick(record, scene.material_scale.w);
    if (radius <= 0.0) return 0.0;
    vec3 delta = position - record.positionRadius.xyz;
    float radius_squared = radius * radius;
    float remaining_squared = radius_squared - dot(delta, delta);
    if (remaining_squared <= 0.0) return 0.0;
    return clamp(floor(remaining_squared * 128.0 / radius_squared), 0.0, 128.0);
}

float direct_draw_raw_shading_word(float channel_byte, float lookup_index) {
    float source_word = floor(floor(channel_byte + 0.5) * 257.0 / 4.0);
    float lookup_word = floor(lookup_index * 257.0 / 16.0);
    return floor(source_word * lookup_word / 65536.0);
}

vec3 direct_draw_resource_shading_words(vec3 position) {
    vec3 words = vec3(0.0);
    uint count = min(resourceShading.header.x, 256u);
    for (uint index = 0u; index < count; ++index) {
        ResourceShadingRecord record = resourceShading.records[index];
        float lookup_index = direct_draw_lookup_index(record, position);
        if (lookup_index <= 0.0) continue;
        vec3 color_bytes = floor(clamp(record.colorPeriod.rgb, vec3(0.0), vec3(1.0)) * 255.0 + 0.5);
        words = min(vec3(65535.0), words +
            vec3(direct_draw_raw_shading_word(color_bytes.r, lookup_index),
                 direct_draw_raw_shading_word(color_bytes.g, lookup_index),
                 direct_draw_raw_shading_word(color_bytes.b, lookup_index)));
    }
    return words;
}

vec3 direct_draw_finalize_vertex_lighting(vec3 base_light_bytes, vec3 shading_words) {
    float white_source_word = floor(255.0 * 257.0 / 64.0);
    vec3 lit_words = min(vec3(65535.0), vec3(white_source_word) + shading_words);
    vec3 modulation_words = floor(clamp(base_light_bytes, vec3(0.0), vec3(255.0)) * 257.0 / 4.0);
    return min(vec3(255.0), floor(lit_words * modulation_words / 65536.0));
}

void main() {
    shadowWorldPosition=in_position;
    receivesShadow=in_casts_shadow;
    gl_Position = scene.view_projection * vec4(in_position, 1.0);
    out_normal = normalize(in_normal);
    out_uv = in_uv;
    // ModelRuntimeSlotClassInit copies the class-13 MDL selector to the
    // model-node animated subresource.  The runtime then varies its V offset
    // over the exact 0..0x80000 Q12 range.  0x80000 is half of the normalized
    // 0x100000 SPR UV period, so the preview reproduces that range with a
    // deterministic triangular cycle while leaving all other triangles static.
    if (in_animation_cycle_ticks > 1.0 && in_animation_v_amplitude != 0.0) {
        float phase = mod(floor(scene.animation_controls.y), in_animation_cycle_ticks) /
                      in_animation_cycle_ticks;
        float triangle_wave = 1.0 - abs(phase * 2.0 - 1.0);
        out_uv.y += in_animation_v_amplitude * triangle_wave;
    }
    out_texture_layer = in_texture_layer;
    out_static_animation_category = in_direct_tint_bypass >> 1u;
    vec4 base = unpack_argb(in_argb);
    // game.exe 0x004BD5A2-0x004BD610: triangle flag 0x0200 copies the model tint
    // straight into the prepared vertex and returns before directional or nearby-resource
    // lighting.  Most dense resource crystals and every luminous particle use this route.
    if ((in_direct_tint_bypass & 1u) != 0u) {
        out_color = base;
    } else {
        vec3 bytes = direct_draw_finalize_vertex_lighting(
            floor(base.rgb * 255.0 + 0.5),direct_draw_resource_shading_words(in_position));
        out_color = vec4(bytes / 255.0, base.a);
    }
}
