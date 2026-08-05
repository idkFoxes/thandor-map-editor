#version 450
layout(location=0) in vec3 normal;
layout(location=1) in vec2 uv;
layout(location=2) flat in uint materialId;
layout(location=3) in vec3 lightingBytes;
layout(set=0,binding=0) uniform Scene { mat4 viewProjection; vec4 camera; vec4 materialScale; vec4 animationControls; } scene;
layout(set=0,binding=1) uniform sampler2DArray materials;
layout(location=0) out vec4 outColor;

vec3 linearToSrgb(vec3 value) {
    value = clamp(value, vec3(0.0), vec3(1.0));
    bvec3 low = lessThanEqual(value, vec3(0.0031308));
    vec3 lower = value * 12.92;
    vec3 upper = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(upper, lower, low);
}

vec3 srgbToLinear(vec3 value) {
    value = clamp(value, vec3(0.0), vec3(1.0));
    bvec3 low = lessThanEqual(value, vec3(0.04045));
    vec3 lower = value / 12.92;
    vec3 upper = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(upper, lower, low);
}

vec3 gameByteModulate(vec3 sampledLinear, vec3 packedLightBytes) {
    // The target is an UNORM swapchain image in SRGB_NONLINEAR presentation space. Return the
    // encoded byte-domain product directly: Direct3D 2 modulates and blends these values without
    // an sRGB decode/encode stage.
    return linearToSrgb(sampledLinear) * clamp(packedLightBytes, 0.0, 1.0);
}

void main(){
    uint layers=max(1u,uint(scene.materialScale.y));
    uint layer=min(materialId,layers-1u);
    // Each draw triangle is one exact soil.dat packet; source alpha supplies the irregular transition.
    // Resource light is already part of the smoothly interpolated encoded lighting, matching
    // game.exe 0x005004A0 -> 0x004CCA90.
    vec4 tex=texture(materials,vec3(uv,float(layer)));
    outColor=vec4(gameByteModulate(tex.rgb,lightingBytes),tex.a);
}
