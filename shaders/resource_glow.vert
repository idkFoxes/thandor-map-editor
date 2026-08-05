#version 450
layout(set=0,binding=0) uniform Scene { mat4 view_projection; vec4 camera; vec4 material_scale; vec4 animation_controls; } scene;
layout(location=0) in vec3 in_position;
layout(location=1) in vec2 in_local_offset;
layout(location=2) in uint in_packed_rgb;
layout(location=3) in vec4 in_pulse0; // radius, period, phase, grow
layout(location=4) in vec2 in_pulse1; // stop, release
layout(location=0) out vec2 out_local_offset;
layout(location=1) flat out uint out_packed_rgb;
layout(location=2) flat out vec4 out_pulse0;
layout(location=3) flat out vec2 out_pulse1;
void main() {
    gl_Position = scene.view_projection * vec4(in_position, 1.0);
    out_local_offset = in_local_offset;
    out_packed_rgb = in_packed_rgb;
    out_pulse0 = in_pulse0;
    out_pulse1 = in_pulse1;
}
