#version 450
layout(location=0) in vec3 texCoord;
layout(location=1) in vec4 tint;
layout(location=2) in float windowDepth;
layout(location=0) out vec4 outColor;
layout(std430,binding=0) readonly buffer TexturePool { uint texels[]; };
layout(push_constant) uniform Material {
    int offset, width, height, flags; // bit 0/1: repeat s/t, 2: linear, 3: RGB
    int function, threshold, blend, fog;
    uint fogColor;
    float nearDist, farDist, density;
} m;
vec4 texel(ivec2 index) {
    index = clamp(index, ivec2(0), ivec2(m.width-1, m.height-1));
    return unpackUnorm4x8(texels[m.offset + index.y*m.width + index.x]);
}
vec4 sampleTexture(vec2 uv) {
    uv = vec2((m.flags & 1)!=0 ? fract(uv.x) : clamp(uv.x,0,1),
              (m.flags & 2)!=0 ? fract(uv.y) : clamp(uv.y,0,1));
    vec2 p = uv * vec2(m.width,m.height);
    if ((m.flags & 4)==0) return texel(ivec2(p));
    p -= 0.5;
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p);
    return mix(mix(texel(i),texel(i+ivec2(1,0)),f.x),
               mix(texel(i+ivec2(0,1)),texel(i+ivec2(1,1)),f.x),f.y);
}
void main() {
    vec4 c = clamp(tint,0,1);
    if (m.width>0) {
        vec4 t = sampleTexture(texCoord.xy/texCoord.z);
        if (m.function==227) c *= t;
        else {
            c.rgb=t.rgb;
            if (m.function==228 && (m.flags & 8)==0) c.a=t.a;
        }
    }
    if (floor(c.a*255.0+0.5)<m.threshold) discard;
    if (m.fog!=0) {
        float dist=1.0/texCoord.z;
        float f=m.fog==80 ? exp(-m.density*dist) :
            (m.farDist>m.nearDist ? (m.farDist-dist)/(m.farDist-m.nearDist) : 1.0);
        c.rgb=mix(unpackUnorm4x8(m.fogColor).bgr,c.rgb,clamp(f,0,1));
    }
    if (m.blend==65) c.rgb*=c.a;
    if (m.blend>=65 && m.blend<=67) c.a=1.0;
    // Exact binary scale permits D32 to represent every D24 rank and a
    // separate sentinel 1.0 for imported clear keys. Far geometry passes both.
    float p=clamp(windowDepth,0,1)*16777215.0;
    gl_FragDepth=(floor(p)+step(0.5,fract(p)))*(1.0/16777216.0);
    outColor=c;
}
