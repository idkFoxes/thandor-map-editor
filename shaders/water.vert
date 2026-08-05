#version 450
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUv;
layout(location=3) in uint inTextureLayer;
layout(location=4) in uint inPackedDiffuse;
layout(location=5) in uvec3 inWaveRecord;
layout(location=6) in vec4 inEncodedLighting;
layout(set=0,binding=0) uniform Scene { mat4 viewProjection; vec4 camera; vec4 materialScale; vec4 animationControls; } scene;
layout(location=0) out vec2 uv;
layout(location=1) flat out uint textureLayer;
layout(location=2) out vec4 vertexDiffuse;

const float kTurn16ToRadians = 6.28318530717958647692 / 65536.0;

int unpackSigned16(uint packed, uint shift) {
    int value = int((packed >> shift) & 65535u);
    return value >= 32768 ? value - 65536 : value;
}

float phaseRadians(uint initialPhase, int step, int tick) {
    int wrapped = (int(initialPhase & 65535u) + step * tick) & 65535;
    return float(wrapped) * kTurn16ToRadians;
}

vec4 unpackArgb(uint value) {
    return vec4(float((value >> 16u) & 255u),
                float((value >> 8u) & 255u),
                float(value & 255u),
                float((value >> 24u) & 255u)) / 255.0;
}

void main() {
    uint phaseXY = inWaveRecord.x & 65535u;
    uint phaseZ = (inWaveRecord.x >> 16u) & 65535u;
    int stepXY = unpackSigned16(inWaveRecord.y, 0u);
    int stepZ = unpackSigned16(inWaveRecord.y, 16u);
    float amplitudeXY = float(inWaveRecord.z & 65535u) / 4096.0;
    float amplitudeZ = float((inWaveRecord.z >> 16u) & 65535u) / 4096.0;
    int tick = int(scene.animationControls.x);
    float angleXY = phaseRadians(phaseXY, stepXY, tick);
    float angleZ = phaseRadians(phaseZ, stepZ, tick);

    // FixedMath_SinCosScaled at 0x00484B00 returns cosine in EAX and sine in EDX.
    // TerrainDirectionTable_AdvanceAndRebuildVectors stores those as X/Y and the
    // second cosine as Z before incrementing the packed phases.
    vec3 displacement = vec3(cos(angleXY) * amplitudeXY,
                             sin(angleXY) * amplitudeXY,
                             cos(angleZ) * amplitudeZ);
    vec3 worldPosition = inPosition + displacement;
    gl_Position = scene.viewProjection * vec4(worldPosition, 1.0);
    uv = inUv;
    textureLayer = inTextureLayer;
    vertexDiffuse = unpackArgb(inPackedDiffuse);
}
