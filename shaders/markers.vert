#version 450
layout(location=0) in vec3 inPosition;
layout(location=1) in vec4 inColor;
layout(set=0,binding=0) uniform Scene { mat4 viewProjection; vec4 camera; vec4 materialScale; vec4 animationControls; mat4 shadowViewProjection; vec4 shadowParameters; } scene;
layout(location=0) out vec4 color;
void main(){gl_Position=scene.viewProjection*vec4(inPosition,1.0);gl_PointSize=12.0;color=inColor;}
