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
    c += ubuf.view * 0.032;
    float r2 = dot(c, c);
    return c * (1.0 + 0.06 * r2) + 0.5;
}

// 逐像素伪随机（确定性）：磷光颗粒的烧制纹理由它驱动
float hash2(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
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

    // 荧光粉三元组竖纹：逐像素相位抖动 + 竖纹随屏微弯——打破纯人工的
    // 左右对称，颗粒像是烧进玻璃的
    vec2 pxpos = uv * ubuf.texSize;
    vec2 cell = floor(pxpos);
    float jitter = (hash2(cell) - 0.5) * 0.38;
    float wob = sin(cell.y * 0.137 + cell.x * 0.009) * 0.008;
    float sub = fract(pxpos.x * 3.0 + jitter + wob);
    float rMask = smoothstep(0.0, 0.333, sub) * (1.0 - smoothstep(0.333, 0.667, sub));
    float gMask = smoothstep(0.333, 0.667, sub) * (1.0 - smoothstep(0.667, 1.0, sub));
    float bMask = smoothstep(0.667, 1.0, sub);
    vec3 texcol = sampleAt(uv);
    vec3 phos = vec3(texcol.r, texcol.g * 0.69, texcol.b * 0.06);
    vec3 col = vec3(phos.r * rMask + phos.g * gMask + phos.b * bMask) * 3.0;

    // 扫描线：行相位带颗粒抖动，逐行残辉呼吸——暗行不是均匀黑
    float rowJ = (hash2(cell + vec2(7.0, 3.0)) - 0.5) * 0.3;
    float row = fract(pxpos.y + rowJ);
    col *= 1.0 - 0.22 * step(0.5, fract(row * 0.5));
    col *= 0.955 + 0.045 * hash2(vec2(cell.y, 3.7));

    // 玻璃反光（鼠标即观察者）+ 暗角
    vec2 n = normalize(vec2(ubuf.view.x * 0.8, 0.6));
    float refl = pow(max(0.0, 1.0 - abs(dot(n, vec2(0.35, 0.94)) - 0.62) * 3.2), 2.0);
    col += vec3(1.0, 0.88, 0.62) * refl * 0.055;
    float d = length(uv - 0.5) * 1.5;
    col *= 1.0 - 0.32 * smoothstep(0.4, 1.0, d);

    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
