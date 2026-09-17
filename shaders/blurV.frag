#version 450
// blurV.frag —— 小图垂直盒式模糊，半径 2（5 抽头 /5）。
// 与 Crt::boxBlurV(radius=2) 同模型；边缘 ClampToEdge。
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
    vec2 t = vec2(0.0, ubuf.glowInfo.z);
    vec3 c = texture(src, v_uv).rgb
           + texture(src, v_uv + t).rgb + texture(src, v_uv - t).rgb
           + texture(src, v_uv + 2.0 * t).rgb + texture(src, v_uv - 2.0 * t).rgb;
    frag = vec4(c / 5.0, 1.0);
}
