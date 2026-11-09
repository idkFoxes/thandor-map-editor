#version 450
layout(location=0) in vec2 uv;
layout(location=1) flat in uint textureLayer;
layout(location=2) in vec4 vertexDiffuse;
layout(set=0,binding=0) uniform Scene { mat4 viewProjection; vec4 camera; vec4 materialScale; vec4 animationControls; mat4 shadowViewProjection; vec4 shadowParameters; } scene;
layout(set=0,binding=4) uniform sampler2DArray surfaceTextures;
layout(location=0) out vec4 outColor;

void main() {
    uint layerCount = max(1u, uint(scene.camera.w));
    uint layer = min(textureLayer, layerCount - 1u);
    vec4 texel = texture(surfaceTextures, vec3(uv, float(layer)));
    float alpha = texel.a * vertexDiffuse.a;
    if (alpha < 0.0025) discard;

    // WaterVertex_ProjectDisplacedSurface supplies the shared primary-surface diffuse path.
    // Primary-surface textures are UNORM so filtering and texture-by-diffuse modulation stay
    // in the recovered DirectDraw encoded component domain.
    vec3 encodedColor = clamp(texel.rgb, 0.0, 1.0) * clamp(vertexDiffuse.rgb, 0.0, 1.0);
    outColor = vec4(encodedColor, alpha);
}
