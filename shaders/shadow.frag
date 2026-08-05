#version 450
layout(location=0) in vec3 in_normal;
layout(location=1) in vec2 in_uv;
layout(location=2) flat in uint in_texture_layer;
layout(location=3) in vec4 in_color;
layout(location=4) in vec3 in_world_position;
layout(set=0,binding=0) uniform Scene { mat4 view_projection; vec4 camera; vec4 material_scale; vec4 animation_controls; } scene;
layout(set=0,binding=2) uniform sampler2DArray object_textures;
layout(location=0) out vec4 out_color;
void main() {
    uint layer_count = max(1u, uint(scene.material_scale.z));
    float layer = float(in_texture_layer % layer_count);
    // game.exe samples a generated white 64x64 silhouette whose palette alpha is 0..255.
    // Model tint alpha is halved before submission; the editor stores that half-alpha in_color.
    float alpha = texture(object_textures, vec3(in_uv, layer)).a * in_color.a;
    if (alpha < 0.01) discard;
    out_color = vec4(0.0, 0.0, 0.0, alpha);
}
