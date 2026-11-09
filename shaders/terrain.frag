#version 450
layout(location=0) in vec3 normal;
layout(location=1) in vec2 uv;
layout(location=2) flat in uint materialId;
layout(location=3) in vec3 lightingBytes;
layout(set=0,binding=0) uniform Scene { mat4 viewProjection; vec4 camera; vec4 materialScale; vec4 animationControls; mat4 shadowViewProjection; vec4 shadowParameters; } scene;
layout(set=0,binding=1) uniform sampler2DArray materials;
layout(location=0) out vec4 outColor;

vec3 gameByteModulate(vec3 sampledBytes, vec3 packedLightBytes) {
    // Ground is uploaded as UNORM so bilinear filtering remains in the encoded byte domain,
    // matching the recovered DirectDraw/D3D2 texture-by-diffuse modulation path.
    return clamp(sampledBytes, 0.0, 1.0) * clamp(packedLightBytes, 0.0, 1.0);
}

layout(set=0,binding=6) uniform sampler2DShadow directionalShadow;
layout(location=4) in vec3 shadowWorldPosition;
float shadowVisibility() {
    if(scene.shadowParameters.x<=0.0)return 1.0;
    vec4 light=scene.shadowViewProjection*vec4(shadowWorldPosition,1.0);
    vec3 p=light.xyz/light.w;
    vec2 uv=p.xy*0.5+0.5;
    if(p.z<=0.0||p.z>=1.0||any(lessThan(uv,vec2(0.0)))||any(greaterThan(uv,vec2(1.0))))return 1.0;
    // Compare each tap against the receiver plane at that texel center. A single
    // constant reference makes steep surfaces stripe under nearest-depth PCF.
    vec3 dx=dFdx(p),dy=dFdy(p);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=abs(determinant)>1e-12?
        vec2(dx.z*dy.y-dy.z*dx.y,dx.x*dy.z-dy.x*dx.z)/determinant:vec2(0.0);
    float texel=scene.shadowParameters.y;
    vec2 center=(floor(uv/texel)+0.5)*texel;
    float sum=0.0;
    // Explicit PCF also supports depth formats without linear filtering.
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec2 tap=center+vec2(x,y)*texel;
        float reference=p.z+dot(gradient,(tap-uv)*2.0)-scene.shadowParameters.z;
        sum+=texture(directionalShadow,vec3(tap,reference));
    }
    float edge=min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y));
    return 1.0-scene.shadowParameters.x*(1.0-sum/9.0)*smoothstep(0.0,0.025,edge);
}

void main(){
    uint layers=max(1u,uint(scene.materialScale.y));
    uint layer=min(materialId,layers-1u);
    // Each draw triangle is one exact soil.dat packet; source alpha supplies the irregular transition.
    // Resource light is already part of the smoothly interpolated encoded lighting.
    vec4 tex=texture(materials,vec3(uv,float(layer)));
    outColor=vec4(gameByteModulate(tex.rgb,lightingBytes)*shadowVisibility(),tex.a);
}
