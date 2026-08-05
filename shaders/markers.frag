#version 450
layout(location=0) in vec4 color;layout(location=0) out vec4 outColor;
void main(){vec2 d=gl_PointCoord-vec2(0.5);if(dot(d,d)>0.25)discard;outColor=color;}
