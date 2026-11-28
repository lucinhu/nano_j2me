#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D src;
layout(push_constant) uniform Post {
    float srcWidth, srcHeight;  /* source texels */
    float dstX, dstY, dstW, dstH; /* destination rect in pixels */
    float sharp;                /* RCAS-style contrast 0..1 */
    float pad;
} p;
void main() {
    vec2 texel = vec2(1.0 / p.srcWidth, 1.0 / p.srcHeight);
    vec3 c = texture(src, uv).rgb;
    vec3 n = texture(src, uv + vec2(0.0, -texel.y)).rgb;
    vec3 s = texture(src, uv + vec2(0.0,  texel.y)).rgb;
    vec3 w = texture(src, uv + vec2(-texel.x, 0.0)).rgb;
    vec3 e = texture(src, uv + vec2( texel.x, 0.0)).rgb;
    vec3 blur = (n + s + w + e) * 0.25;
    vec3 sharpened = c + (c - blur) * p.sharp;
    outColor = vec4(clamp(sharpened, 0.0, 1.0), 1.0);
}
