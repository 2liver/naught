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
    c -= ubuf.view * 0.032; // 观察者在鼠标侧：屏幕随之微倾（与直觉同向）
    float r2 = dot(c, c);
    return c * (1.0 + 0.06 * r2) + 0.5;
}

// 栅线亮度：孔径栅格的金属线不是等亮（结构级差异，非噪点）
float wireShade(float wireIdx)
{
    return 0.97 + 0.03 * fract(sin(wireIdx * 12.9898) * 43758.5453);
}

void main()
{
    // 曲率 + 鼠标视差；输入先内缩 1.75%：曲率外扩（桶形）后采样仍留在
    // [0,1] 内，边缘自然成显像管壳边，顶部行不再被推出可见区
    vec2 uv = curve(v_uv * 0.965 + 0.0175);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        frag = vec4(0.012, 0.009, 0.004, 1.0);
        return;
    }

    vec2 pxpos = uv * ubuf.texSize;

    // 栅条随玻璃曲率微弯（真 CRT 的 mask 与玻壳共曲率；幅度克制，
    // 过大即两侧对称"漩涡"）
    float bow = 0.4 * (uv.y - 0.5) * (uv.y - 0.5);

    // 荧光粉三元组：窗口函数——每个像素位恰属一个子像素（三者和=1）。
    // 琥珀文字的 R 分量点燃 R 子像素、G 分量点燃 G 子像素，眼合成即磷光。
    // （旧版乘积掩膜只在 1/3、2/3 处有窄尖峰，输出退化成灰度——真光学修复）
    float phase = pxpos.x * 3.0 + bow;
    float sub = fract(phase);
    float rMask = 1.0 - smoothstep(0.30, 0.34, sub);
    float gMask = smoothstep(0.30, 0.34, sub) * (1.0 - smoothstep(0.63, 0.67, sub));
    float bMask = smoothstep(0.63, 0.67, sub);
    vec3 texcol = sampleAt(uv);
    vec3 phos = vec3(texcol.r, texcol.g * 0.69, texcol.b * 0.06);
    // 逐通道合成（逗号！）：R 分量只走 R 窗口、G 只走 G——RGB 子像素
    // 各自独立点燃。旧版是三个标量求和后广播（+），整个光栅被强制成
    // 灰度 = 所有"底色不对/纹路丑"的根源。
    vec3 col = vec3(phos.r * rMask, phos.g * gMask, phos.b * bMask) * 3.0;

    // 扫描线：隔行暗带（结构），暗行掺一丝上行残辉
    float scanline = step(0.5, fract(floor(pxpos.y) * 0.5));
    col *= 1.0 - 0.2 * scanline;
    col *= 1.0 - 0.05 * scanline * vec3(0.35, 0.4, 0.25);

    // 栅线亮度（每 3px 一条）
    col *= wireShade(floor(phase / 3.0) + 0.5);

    // 玻璃高光：定位光斑跟随观察者（鼠标）——指哪亮哪，
    // 左右上下都与直觉同向（旧版是全局亮度随点积变化，方向感错误）
    vec2 glint = vec2(0.5 + ubuf.view.x * 0.30, 0.5 + ubuf.view.y * 0.22);
    float refl = pow(max(0.0, 1.0 - length((uv - glint) * vec2(1.15, 0.9)) * 1.55), 3.0);
    col += vec3(1.0, 0.88, 0.62) * refl * 0.07;
    float d = length(uv - 0.5) * 1.5;
    col *= 1.0 - 0.32 * smoothstep(0.4, 1.0, d);

    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
