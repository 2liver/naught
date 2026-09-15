#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag;
layout(std140, binding = 0) uniform buf {
    vec2 view;
    vec2 texSize;
} ubuf;
layout(std430, binding = 1) buffer Pixels { uint p[]; } px;

uint texel(uint x, uint y)
{
    return px.p[y * uint(ubuf.texSize.x) + x];
}

vec3 sampleAt(vec2 uv)
{
    uint x = uint(uv.x * ubuf.texSize.x);
    uint y = uint((1.0 - uv.y) * ubuf.texSize.y); // Metal 的 Y 翻转：屏幕顶=缓冲顶
    uint v = texel(x, y);
    // RGBA8888 在 GPU 上按小端解释：R 在低字节
    return vec3(float(v & 255u), float((v >> 8) & 255u), float((v >> 16) & 255u)) / 255.0;
}

vec2 curve(vec2 uv) {
    vec2 c = uv - 0.5;
    // 视差（鼠标即观察者）：左右——观察者侧的图像后退；上下——已确认为直觉同向
    c.x += ubuf.view.x * 0.032;
    c.y -= ubuf.view.y * 0.032;
    float r2 = dot(c, c);
    return c * (1.0 + 0.05 * r2) + 0.5;
}

void main()
{
    // 曲率 + 视差；输入先内缩 1.75% 防曲率越界，但越界区域钳制采样
    //（不再填暗色壳边——暗条与滚动条同宽，视觉上像"左边多了一条滚动条"）
    vec2 uv = clamp(curve(v_uv * 0.965 + 0.0175), 0.0, 1.0);
    vec2 pxpos = uv * ubuf.texSize;

    // 荧光粉竖纹：1 物理像素周期（DPR 感知管线），深度可见——
    // 光栅质感回来，但不再是粗线条
    float stripe = 1.0 - 0.22 * step(0.5, fract(pxpos.x));
    vec3 col = sampleAt(uv);

    // 真衍射（二期三件套·回接）：N=3 磷粉栅的竖直亮边 ±1px R/B 彩边。
    // 模型与 Crt::edgeDiff 同源：bright(x)−bright(x±1)>0 处即亮边，
    // 亮边右侧出红、左侧出蓝（色相相反），强度 kDiffAlpha=0.20。
    {
        const float px = 1.0 / ubuf.texSize.x;
        vec3 lm = sampleAt(clamp(uv - vec2(px, 0.0), 0.0, 1.0));
        vec3 rp = sampleAt(clamp(uv + vec2(px, 0.0), 0.0, 1.0));
        const vec3 w = vec3(0.333);
        float eR = max(0.0, dot(col, w) - dot(rp, w));
        float eB = max(0.0, dot(col, w) - dot(lm, w));
        col += vec3(1.0, 0.15, 0.02) * eR * 0.20;
        col += vec3(0.02, 0.15, 1.0) * eB * 0.20;
    }
    col *= stripe;

    // 扫描线：每行一条，暗行掺一丝上行残辉
    float scanline = step(0.5, fract(pxpos.y));
    col *= 1.0 - 0.18 * scanline;
    col *= 1.0 - 0.06 * scanline * vec3(0.35, 0.4, 0.25);

    // 暗角（克制）
    float d = length(uv - 0.5) * 1.5;
    col *= 1.0 - 0.22 * smoothstep(0.4, 1.0, d);

    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
