#version 450
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUv;
layout(location=3) in uint inMaterial0;
layout(location=4) in uint inPackedLight;
layout(location=5) in uvec3 inMaterial123;
layout(location=6) in vec4 inBlendWeights;
layout(set=0,binding=0) uniform Scene { mat4 viewProjection; vec4 camera; vec4 materialScale; vec4 animationControls; } scene;

struct ResourceShadingRecord {
    vec4 positionRadius;
    vec4 colorPeriod;
    vec4 phaseGrowStopRelease;
};
layout(std430,set=0,binding=3) readonly buffer ResourceShadingBuffer {
    uvec4 header;
    ResourceShadingRecord records[];
} resourceShading;

layout(location=0) out vec3 normal;
layout(location=1) out vec2 uv;
layout(location=2) flat out uint materialId;
layout(location=3) out vec3 lightingBytes;

vec3 unpackRgbBytes(uint value) {
    return vec3(float((value >> 16u) & 255u),
                float((value >> 8u) & 255u),
                float(value & 255u));
}

float directDrawRadiusAtTick(ResourceShadingRecord record, float currentTick) {
    float cycle = max(2.0, floor(record.colorPeriod.w + 0.5));
    float spawn = floor(record.phaseGrowStopRelease.x + 0.5);
    float age = mod(floor(currentTick) - spawn + cycle, cycle);
    float targetQ12 = floor(record.positionRadius.w * 4096.0 + 0.5);
    float grow = max(1.0, floor(record.phaseGrowStopRelease.y + 0.5));
    float stop = floor(record.phaseGrowStopRelease.z + 0.5);
    float release = max(1.0, floor(record.phaseGrowStopRelease.w + 0.5));
    float radiusQ12 = 0.0;
    if (age < grow) {
        radiusQ12 = floor(targetQ12 * age / grow);
    } else if (age < stop) {
        radiusQ12 = targetQ12;
    } else if (age < stop + release) {
        radiusQ12 = floor(targetQ12 * (stop + release - age) / release);
    }
    return radiusQ12 / 4096.0;
}

float directDrawLookupIndex(ResourceShadingRecord record, vec3 position) {
    float radius = directDrawRadiusAtTick(record, scene.materialScale.w);
    if (radius <= 0.0) return 0.0;
    vec3 delta = position - record.positionRadius.xyz;
    float radiusSquared = radius * radius;
    float remainingSquared = radiusSquared - dot(delta, delta);
    if (remainingSquared <= 0.0) return 0.0;
    // game.exe 0x004CCA90: (remainingQ24 >> 5) / (radiusSquaredQ24 >> 12).
    // The quotient is 0..128 and selects the immutable qword table at 0x0041DE80.
    return clamp(floor(remainingSquared * 128.0 / radiusSquared), 0.0, 128.0);
}

float directDrawRawShadingWord(float channelByte, float lookupIndex) {
    float sourceWord = floor(floor(channelByte + 0.5) * 257.0 / 4.0);
    float lookupWord = floor(lookupIndex * 257.0 / 16.0);
    return floor(sourceWord * lookupWord / 65536.0);
}

vec3 directDrawResourceShadingWords(vec3 position) {
    vec3 words = vec3(0.0);
    uint count = min(resourceShading.header.x, 256u);
    for (uint index = 0u; index < count; ++index) {
        ResourceShadingRecord record = resourceShading.records[index];
        float lookupIndex = directDrawLookupIndex(record, position);
        if (lookupIndex <= 0.0) continue;
        vec3 colorBytes = floor(clamp(record.colorPeriod.rgb, vec3(0.0), vec3(1.0)) * 255.0 + 0.5);
        words = min(vec3(65535.0), words +
            vec3(directDrawRawShadingWord(colorBytes.r, lookupIndex),
                 directDrawRawShadingWord(colorBytes.g, lookupIndex),
                 directDrawRawShadingWord(colorBytes.b, lookupIndex)));
    }
    return words;
}

vec3 directDrawFinalizeVertexLighting(vec3 baseLightBytes, vec3 shadingWords) {
    // game.exe initializes every field-cell overlay at +0x04 to 0xFFFFFFFF
    // (0x00503D26). The renderer therefore previews the fully visible/no-fog state.
    // 0x0050054C-0x00500588 unpacks that white overlay, adds resource shading,
    // pmulhw-modulates the directional light, and packs the result back to bytes.
    const vec3 fullVisibilityOverlayBytes = vec3(255.0);
    vec3 fullVisibilityWords = floor(fullVisibilityOverlayBytes * 257.0 / 64.0);
    vec3 litWords = min(vec3(65535.0), fullVisibilityWords + shadingWords);
    vec3 modulationWords = floor(clamp(baseLightBytes, vec3(0.0), vec3(255.0)) * 257.0 / 4.0);
    return min(vec3(255.0), floor(litWords * modulationWords / 65536.0));
}

void main(){
    gl_Position=scene.viewProjection*vec4(inPosition,1.0);
    normal=inNormal;
    uv=inUv;
    materialId=inMaterial0;
    // DirectDraw applies GraphicsShadingRuntime at terrain vertices before the textured packet is
    // submitted. Let rasterization interpolate the resulting lighting just as the original D3D
    // triangle does; never turn it into a translucent fragment overlay.
    lightingBytes=directDrawFinalizeVertexLighting(
        unpackRgbBytes(inPackedLight),directDrawResourceShadingWords(inPosition))/255.0;
}
