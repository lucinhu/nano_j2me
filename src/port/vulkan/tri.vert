#version 450
layout(location=0) in vec3 position;  // ROI-relative NDC xy, unclamped window z
layout(location=1) in vec3 uvq;
layout(location=2) in vec4 color;
layout(location=0) out vec3 texCoord;
layout(location=1) out vec4 tint;
layout(location=2) out float windowDepth;
void main() {
    gl_Position = vec4(position.xy, 0.0, 1.0);
    windowDepth = position.z;
    texCoord = uvq;
    tint = color;
}
