#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag;
layout(std140, binding = 0) uniform buf {
    vec2 view;
    vec2 texSize;
    vec2 timeInfo;   // x = 运行秒数，y = 暖机毫秒（-1 = 非暖机期）
    vec2 _pad;
    vec4 scanTint;   // 扫描线暗行掺色（调色板）
    vec4 refl;       // 玻璃反光色（调色板）
    vec4 dustCol;    // 灰尘点色（调色板）
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

float hash21(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    // ---- 内容空间：弯曲的电子图像（含视差）。栅网/扫描线/玻璃都在
    // 屏幕空间——真机上它们是固定在玻璃上的，不随视差移动 ----
    vec2 cuv = clamp(curve(v_uv * 0.965 + 0.0175), 0.0, 1.0);
    vec3 col = sampleAt(cuv);

    // 真衍射：内容空间亮边 ±1px R/B 彩边（bright(x)−bright(x±1) 差分，
    // 与 Crt::edgeDiff 同模型，强度 kDiffAlpha=0.30）
    {
        const float px = 1.0 / ubuf.texSize.x;
        vec3 lm = sampleAt(clamp(cuv - vec2(px, 0.0), 0.0, 1.0));
        vec3 rp = sampleAt(clamp(cuv + vec2(px, 0.0), 0.0, 1.0));
        const vec3 w = vec3(0.333);
        float eR = max(0.0, dot(col, w) - dot(rp, w));
        float eB = max(0.0, dot(col, w) - dot(lm, w));
        col += vec3(1.0, 0.15, 0.02) * eR * 0.30;
        col += vec3(0.02, 0.15, 1.0) * eB * 0.30;
    }

    // 亮度（束斑宽度与栅条调制的共同输入）
    float lum = dot(col, vec3(0.333));

    // 束斑物理：水平扫描把束斑沿扫描方向拉长（不对称核：水平 6 抽头
    // 重、垂直 2 抽头轻），亮度越高束斑越宽——加法溢出：亮字核心
    // 饱和、四周变软变晕，暗处不动
    {
        const vec2 off = 1.0 / ubuf.texSize;
        vec3 blur = (sampleAt(clamp(cuv + vec2( off.x, 0.0), 0.0, 1.0))
                   + sampleAt(clamp(cuv - vec2( off.x, 0.0), 0.0, 1.0))) * 0.24
                  + (sampleAt(clamp(cuv + vec2( off.x * 2.0, 0.0), 0.0, 1.0))
                   + sampleAt(clamp(cuv - vec2( off.x * 2.0, 0.0), 0.0, 1.0))) * 0.14
                  + (sampleAt(clamp(cuv + vec2(0.0,  off.y), 0.0, 1.0))
                   + sampleAt(clamp(cuv + vec2(0.0, -off.y), 0.0, 1.0))) * 0.12;
        col = clamp(col + blur * smoothstep(0.12, 0.85, lum) * 0.40, 0.0, 1.0);
    }

    // ---- 屏幕空间：固定不动的磷粉栅、扫描线（真玻璃结构）----
    vec2 sp = v_uv * ubuf.texSize; // 屏幕物理像素
    // 磷粉栅：束斑扫过栅条的软调制——束斑越宽（亮处）暗带越宽；
    // 束斑水平偏转（内容水平梯度）让栅相位微移，斜边出摩尔纹
    {
        const float px = 1.0 / ubuf.texSize.x;
        float lumL = dot(sampleAt(clamp(cuv - vec2(px, 0.0), 0.0, 1.0)), vec3(0.333));
        float lumR = dot(sampleAt(clamp(cuv + vec2(px, 0.0), 0.0, 1.0)), vec3(0.333));
        float beamW = mix(0.12, 0.42, smoothstep(0.05, 0.9, lum));
        float maskPhase = fract(sp.x + (lumL - lumR) * 0.5);
        float stripe = 1.0 - 0.22 * smoothstep(0.5 - beamW, 0.5 + beamW, maskPhase);
        col *= stripe;
    }
    float scanline = step(0.5, fract(sp.y));
    col *= 1.0 - 0.18 * scanline;
    col *= 1.0 - 0.06 * scanline * ubuf.scanTint.rgb;

    // 噪声与灰尘：细颗粒闪烁 + 稀疏灰尘点（时间驱动，极克制）
    {
        float t = ubuf.timeInfo.x;
        float grain = (hash21(sp + fract(t) * 61.7) - 0.5) * 0.05;
        float dust = step(0.9992, hash21(floor(sp * 0.05) + floor(t * 8.0)))
                     * (0.5 + 0.5 * hash21(floor(sp * 0.05)));
        col += grain + dust * ubuf.dustCol.rgb * 0.10;
    }

    // 玻璃反光带：一道对角淡白反光，随观察者移动（真玻璃反射）
    {
        vec2 n = normalize(vec2(ubuf.view.x * 0.8, 0.6));
        float refl = pow(max(0.0, 1.0 - abs(dot(n, vec2(0.35, 0.94)) - 0.62) * 3.2), 2.0);
        col += ubuf.refl.rgb * refl * 0.045;
    }

    // 滚动刷新带：暗带 3 秒扫一周（屏幕空间，扫描时序）
    {
        float phase = fract(ubuf.timeInfo.x * 0.333);
        float band = 1.0 - smoothstep(0.0, 0.035, abs(v_uv.y - phase));
        col *= 1.0 - 0.22 * (1.0 - band);
    }

    // 入场暖机：由暗到亮的一次预热脉冲（过冲后回落，~1.4s 结束）
    if (ubuf.timeInfo.y >= 0.0 && ubuf.timeInfo.y < 1400.0) {
        float t = ubuf.timeInfo.y;
        float wk = 1.0;
        if (t < 600.0)
            wk = 0.1 + 0.9 * (t / 600.0);            // 暗 → 亮
        else if (t < 900.0)
            wk = 1.0 + 0.08 * (1.0 - (t - 600.0) / 300.0); // 过冲脉冲
        else
            wk = 1.0 + 0.08 * (1.0 - (t - 900.0) / 500.0); // 回落到 1.0
        col *= wk;
    }

    // 暗角（克制，屏幕空间——玻璃固定）
    float d = length(v_uv - 0.5) * 1.5;
    col *= 1.0 - 0.22 * smoothstep(0.4, 1.0, d);

    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
