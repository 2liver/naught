#version 450
// persist.frag —— 磷粉余晖（lighten(max) 双指数模型）GPU 版。
// 与 Crt::phosphorPersistence 同模型：out = max(snapshot, histA·k1,
// histB·k2)；k = persist^(dt/80ms)——按时间衰减，节奏无关（CPU 版按
// 80ms 烘拍量化，GPU 版更接近连续物理）。前两帧由 CPU 把 persist
// 清零（历史纹理未初始化）。
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
    vec4 glowInfo;   // x=辉光alpha y=1/小图宽 z=1/小图高 w=帧时差ms
} ubuf;
layout(binding = 1) uniform sampler2D snap;
layout(binding = 2) uniform sampler2D histA;
layout(binding = 3) uniform sampler2D histB;

void main()
{
    float dt = clamp(ubuf.glowInfo.w, 16.0, 500.0);
    vec3 k1 = pow(clamp(ubuf.persist1.rgb, 0.0, 1.0), vec3(dt / 80.0));
    vec3 k2 = pow(clamp(ubuf.persist2.rgb, 0.0, 1.0), vec3(dt / 80.0));
    vec3 s = texture(snap, v_uv).rgb;
    vec3 a = texture(histA, v_uv).rgb * k1;
    vec3 b = texture(histB, v_uv).rgb * k2;
    frag = vec4(max(s, max(a, b)), 1.0);
}
