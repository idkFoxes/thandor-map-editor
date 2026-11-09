#version 450
layout(location=0) in vec2 in_local_offset;
layout(location=1) flat in uint in_packed_rgb;
layout(location=2) flat in vec4 in_pulse0; // radius, period, phase, grow
layout(location=3) flat in vec2 in_pulse1; // stop, release
layout(set=0,binding=0) uniform Scene { mat4 view_projection; vec4 camera; vec4 material_scale; vec4 animation_controls; mat4 shadowViewProjection; vec4 shadowParameters; } scene;
layout(location=0) out vec4 out_color;

vec3 unpack_rgb(uint value) {
    return vec3(float((value >> 16) & 255u), float((value >> 8) & 255u),
                float(value & 255u)) / 255.0;
}

float radius_at_age(float age, float target_radius, float grow_ticks,
                    float stop_ticks, float release_ticks) {
    if (age < 0.0) return 0.0;
    if (age < grow_ticks) return target_radius * clamp(age / max(grow_ticks, 0.0001), 0.0, 1.0);
    if (age < stop_ticks) return target_radius;
    float release_age = age - stop_ticks;
    if (release_age >= release_ticks) return 0.0;
    return target_radius * clamp(1.0 - release_age / max(release_ticks, 0.0001), 0.0, 1.0);
}

float record_contribution(float age, float distance_world, float target_radius,
                          float grow_ticks, float stop_ticks, float release_ticks) {
    float radius = radius_at_age(age, target_radius, grow_ticks, stop_ticks, release_ticks);
    if (radius <= 0.0 || distance_world >= radius) return 0.0;
    float normalized = distance_world / radius;
    // The immutable 256-entry table at 0x0041DE80 is linear 0..4095 and is indexed by
    // remaining squared radius, yielding the exact 1 - distance^2/radius^2 falloff.
    return clamp(1.0 - normalized * normalized, 0.0, 1.0);
}

void main() {
    float target_radius = in_pulse0.x;
    float period = max(1.0, in_pulse0.y);
    float phase = in_pulse0.z;
    float grow = in_pulse0.w;
    float stop_tick = in_pulse1.x;
    float release = in_pulse1.y;
    float current_age = mod(scene.material_scale.w + phase, period);
    float distance_world = length(in_local_offset);
    // Effect 114/116 remains active through stop+release ticks while the model emits every
    // 80..89 ticks, so only the current and immediately previous shading records can overlap.
    float contribution = record_contribution(current_age, distance_world, target_radius,
                                             grow, stop_tick, release);
    contribution += record_contribution(current_age + period, distance_world, target_radius,
                                        grow, stop_tick, release);
    if (contribution <= 0.0005) discard;
    contribution = clamp(contribution, 0.0, 1.0);
    // RGB-only additive projection. Destination alpha is preserved by the pipeline.
    out_color = vec4(unpack_rgb(in_packed_rgb) * contribution, 0.0);
}
