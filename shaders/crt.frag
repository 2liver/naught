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
    return c * (1.0 + 0.06 * r2) + 0.5;
}

void main()
{
    // 曲率 + 视差；输入先内缩 1.75%：曲率外扩后采样留在 [0,1] 内
    vec2 uv = curve(v_uv * 0.965 + 0.0175);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        frag = vec4(0.012, 0.009, 0.004, 1.0);
        return;
    }

    vec2 pxpos = uv * ubuf.texSize;

    // 荧光粉竖纹：笔直三尖峰亮度纹理（灰版时代验证过的纹理，无弓弯、
    // 无漩涡），颜色原样透出——琥珀磷光不再被求和广播压成灰
    float sub = fract(pxpos.x * 3.0);
    float rBump = smoothstep(0.0, 0.333, sub) * (1.0 - smoothstep(0.333, 0.667, sub));
    float gBump = smoothstep(0.333, 0.667, sub) * (1.0 - smoothstep(0.667, 1.0, sub));
    float bBump = smoothstep(0.667, 1.0, sub);
    float stripe = rBump + gBump + bBump; // 0.5~1 的竖纹亮度
    vec3 col = sampleAt(uv) * (0.4 + 1.2 * stripe);

    // 扫描线：隔行暗带
    float scanline = step(0.5, fract(floor(pxpos.y) * 0.5));
    col *= 1.0 - 0.2 * scanline;

    // 暗角
    float d = length(uv - 0.5) * 1.5;
    col *= 1.0 - 0.32 * smoothstep(0.4, 1.0, d);

    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
