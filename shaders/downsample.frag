#version 450
// downsample.frag —— 4×4 盒式平均降采样（辉光 1/4 图）。
// 与 Crt::phosphorBloom 的 4×4 平均同模型：4 个线性采样点各覆盖
// 2×2，合起来正好 16 纹素均匀平均（±1 源纹素偏移 = 象限中心）。
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag;
layout(std140, binding = 0) uniform buf {
    vec2 view;
    vec2 texSize;
    vec2 timeInfo;
    vec2 flags;
    vec4 scanTint;
    vec4 refl;
    vec4 dustCol;
    vec4 persist1;
    vec4 persist2;
    vec4 glowInfo;
} ubuf;
layout(binding = 1) uniform sampler2D src;

void main()
{
    vec2 t = 1.0 / ubuf.texSize;
    vec3 c = (texture(src, v_uv + vec2( t.x,  t.y)).rgb
            + texture(src, v_uv + vec2(-t.x,  t.y)).rgb
            + texture(src, v_uv + vec2( t.x, -t.y)).rgb
            + texture(src, v_uv + vec2(-t.x, -t.y)).rgb) * 0.25;
    frag = vec4(c, 1.0);
}
