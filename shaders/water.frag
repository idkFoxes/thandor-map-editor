#version 450
layout(location=0) in vec2 uv;
layout(location=1) flat in uint textureLayer;
layout(location=2) in vec4 vertexDiffuse;
layout(set=0,binding=0) uniform Scene { mat4 viewProjection; vec4 camera; vec4 materialScale; vec4 animationControls; } scene;
layout(set=0,binding=4) uniform sampler2DArray surfaceTextures;
layout(location=0) out vec4 outColor;

vec3 linearToSrgb(vec3 value) {
    value = clamp(value, vec3(0.0), vec3(1.0));
    bvec3 low = lessThanEqual(value, vec3(0.0031308));
    vec3 lower = value * 12.92;
    vec3 upper = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(upper, lower, low);
}

void main() {
    uint layerCount = max(1u, uint(scene.camera.w));
    uint layer = min(textureLayer, layerCount - 1u);
    vec4 texel = texture(surfaceTextures, vec3(uv, float(layer)));
    float alpha = texel.a * vertexDiffuse.a;
    if (alpha < 0.0025) discard;

    // Direct3D_PrimitiveHandler_TexturedPreset3 copies the queued diffuse colors.
    // WaterVertex_ProjectDisplacedSurface supplies the LEV secondary colour at +0x12c;
    // its alpha is the stock per-map water opacity used by SRCALPHA/INVSRCALPHA.
    // packet->modulationColor (the PAL-associated dword) is not read on this path.
    // The sRGB image view decodes the texture, so return to the game's byte domain
    // for its one proven texture-by-vertex-diffuse modulation.
    vec3 encodedTexture = linearToSrgb(texel.rgb);
    vec3 encodedColor = encodedTexture * clamp(vertexDiffuse.rgb, 0.0, 1.0);
    outColor = vec4(encodedColor, alpha);
}
