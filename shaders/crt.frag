#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag;
layout(std140, binding = 0) uniform buf {
    vec2 view;
    vec2 texSize;
    vec2 timeInfo;   // x = 运行秒数，y = 暖机毫秒（-1 = 非暖机期）
    vec2 flags;      // x = 实验·屏幕实体，y = 保留
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
    float k = mix(0.05, 0.10, step(0.5, ubuf.flags.x)); // 实验·屏幕实体：曲率加倍
    return c * (1.0 + k * r2) + 0.5;
}

float hash21(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// 磷粉栅条纹：束斑扫过栅条的软调制。period = 栅周期（物理像素），
// phase = 通道相位。C64 真彩管红/绿/蓝三栅各差 1/3 周期（3px 一组
// RGB），白色笔画上逐列轮流压暗一个通道——细密 RGB 栅纹（彩色 CRT
// 指纹）；单色机整面单色粉，无三色结构，保持 1px 周期单栅
float stripe(float spx, float phase, float period, float halfW, float grad)
{
    float maskPhase = fract((spx + phase + grad) / period);
    return 1.0 - 0.22 * smoothstep(0.5 - halfW, 0.5 + halfW, maskPhase);
}

void main()
{
    // ---- 内容空间：弯曲的电子图像（含视差）。栅网/扫描线/玻璃都在
    // 屏幕空间——真机上它们是固定在玻璃上的，不随视差移动 ----
    // 实验·屏幕实体：曲率加倍时输入内缩同步加大——内容永不越界（不裁字）
    float ent = step(0.5, ubuf.flags.x);
    vec2 inuv = v_uv * mix(0.965, 0.93, ent) + mix(0.0175, 0.035, ent);
    vec2 cuv = clamp(curve(inuv), 0.0, 1.0);
    vec3 col = sampleAt(cuv);

    // 真衍射：内容空间亮边 ±1px R/B 彩边（bright(x)−bright(x±1) 差分，
    // 与 Crt::edgeDiff 同模型，强度 kDiffAlpha=0.30）。
    // 白磷机（flags.y=3，IBM PC 5150）：单色荧光粉——衍射无彩，改中性白边
    {
        const float px = 1.0 / ubuf.texSize.x;
        vec3 lm = sampleAt(clamp(cuv - vec2(px, 0.0), 0.0, 1.0));
        vec3 rp = sampleAt(clamp(cuv + vec2(px, 0.0), 0.0, 1.0));
        const vec3 w = vec3(0.333);
        float eR = max(0.0, dot(col, w) - dot(rp, w));
        float eB = max(0.0, dot(col, w) - dot(lm, w));
        float white = step(3.5, ubuf.flags.y); // 机器 3 = 白磷（苹果 II 已归档）
        float frg = mix(0.30, 0.22, white);
        vec3 tR = mix(vec3(1.0, 0.15, 0.02), vec3(0.90, 0.92, 1.0), white);
        vec3 tB = mix(vec3(0.02, 0.15, 1.0), vec3(0.90, 0.92, 1.0), white);
        col += tR * eR * frg;
        col += tB * eB * frg;
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

    // 聚焦漂移（M4）：四角轻微散焦——真机边缘聚焦变差，角落内容
    // 微微发糊（屏幕空间锚定，随玻璃固定）
    {
        const vec2 off = 1.0 / ubuf.texSize;
        vec3 blur4 = (sampleAt(clamp(cuv + vec2( off.x, 0.0), 0.0, 1.0))
                    + sampleAt(clamp(cuv - vec2( off.x, 0.0), 0.0, 1.0))
                    + sampleAt(clamp(cuv + vec2(0.0,  off.y), 0.0, 1.0))
                    + sampleAt(clamp(cuv + vec2(0.0, -off.y), 0.0, 1.0))) * 0.25;
        float corner = smoothstep(0.55, 0.85, length(v_uv - 0.5));
        col = mix(col, blur4, corner * 0.35);
    }

    // ---- 屏幕空间：固定不动的磷粉栅、扫描线（真玻璃结构）----
    vec2 sp = v_uv * ubuf.texSize; // 屏幕物理像素
    // 磷粉栅：束斑越宽（亮处）暗带越宽；束斑水平偏转（内容水平梯度）
    // 让栅相位微移，斜边出摩尔纹。
    // C64（flags.y=2）真彩管：红/绿/蓝三条荧光粉栅按 3px 周期错相排列
    // ——逐通道调制，白字出 RGB 栅纹；单色机保持单栅
    {
        const float px = 1.0 / ubuf.texSize.x;
        float lumL = dot(sampleAt(clamp(cuv - vec2(px, 0.0), 0.0, 1.0)), vec3(0.333));
        float lumR = dot(sampleAt(clamp(cuv + vec2(px, 0.0), 0.0, 1.0)), vec3(0.333));
        float beamW = mix(0.12, 0.42, smoothstep(0.05, 0.9, lum));
        float grad = (lumL - lumR) * 0.5;
        bool c64 = ubuf.flags.y > 1.5 && ubuf.flags.y < 2.5;
        if (c64) {
            float hw = mix(0.08, 0.20, smoothstep(0.05, 0.9, lum));
            col.r *= stripe(sp.x, 0.0, 3.0, hw, grad);
            col.g *= stripe(sp.x, 1.0, 3.0, hw, grad);
            col.b *= stripe(sp.x, 2.0, 3.0, hw, grad);
        } else {
            col *= stripe(sp.x, 0.0, 1.0, beamW, grad);
        }
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

    // 滚动刷新带。单色机：暗带 3 秒扫一周（屏幕空间，气氛）。
    // C64（flags.y=2）：真扫描时序——激励线 0.7s 扫一场的慢放镜头：
    // 电子束逐点逐行轰击、先亮后灭——线上束流过冲（1.30），线后
    // ~6% 屏高内指数熄灭（P22 快分量），线下方本场未扫到、靠上一场
    // 余晖微暗（0.86）。波面清晰可辨但不闪眼。
    {
        bool c64 = ubuf.flags.y > 1.5 && ubuf.flags.y < 2.5;
        if (c64) {
            float H = ubuf.texSize.y;
            float scan = fract(ubuf.timeInfo.x * (1.0 / 0.7)); // 自上而下
            float d = sp.y - scan * H; // >0 未扫到；<0 刚扫过
            float tail = exp(max(d, -H) * (20.0 / H)); // 熄灭指数
            float pulse = smoothstep(-H * 0.06, 0.0, d); // 束流在线上
            float exc = clamp(tail * pulse, 0.0, 1.0);
            col *= 0.86 + 0.44 * exc; // 0.86 → 1.30 过冲 → 回落
        } else {
            float phase = fract(ubuf.timeInfo.x * 0.333);
            float band = 1.0 - smoothstep(0.0, 0.035, abs(v_uv.y - phase));
            col *= 1.0 - 0.22 * (1.0 - band);
        }
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

    // 暗角（屏幕空间——玻璃固定）；实验·屏幕实体：暗角略强 + 边框
    // 阴影带（压暗但不裁字——文字仍可见）+ 一道固定对角玻璃反光
    float d = length(v_uv - 0.5) * 1.5;
    col *= 1.0 - mix(0.22, 0.30, ent) * smoothstep(0.4, 1.0, d);
    if (ent > 0.5) {
        vec2 ed = abs(v_uv - 0.5) * 2.0; // 0 中心 → 1 边缘
        float bezel = smoothstep(0.86, 1.0, max(ed.x, ed.y));
        col *= 1.0 - 0.55 * bezel;
        float gl = pow(max(0.0, 1.0 - abs(v_uv.x * 0.6 + v_uv.y * 0.8 - 0.55) * 3.0), 2.0);
        col += ubuf.refl.rgb * gl * 0.03;
    }

    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
