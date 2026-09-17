#version 450
// crt.frag —— 「显」主着色器：内容（余晖合成结果纹理，最近邻采样=
// 与旧存储缓冲逐纹素同值）→ 衍射/束斑/聚焦 → 辉光叠加（小图线性
// 采样，与 Crt::phosphorBloom 的 max 叠加同模型）→ 屏幕空间栅网/
// 扫描线/灰尘/玻璃/扫描时序/暖机/暗角。
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag;
layout(std140, binding = 0) uniform buf {
    vec2 view;
    vec2 texSize;
    vec2 timeInfo;   // x = 运行秒数，y = 暖机毫秒（-1 = 非暖机期）
    vec2 flags;      // x = 实验·屏幕实体，y = 机型
    vec4 scanTint;   // 扫描线暗行掺色（调色板）
    vec4 refl;       // 玻璃反光色（调色板）
    vec4 dustCol;    // 灰尘点色（调色板）
    vec4 persist1;   // 余晖快分量（主着色器不用，块布局一致）
    vec4 persist2;   // 余晖慢分量
    vec4 glowInfo;   // x = 辉光 alpha，y/z = 1/小图宽高，w = 帧时差 ms
} ubuf;
layout(binding = 1) uniform sampler2D content; // 余晖合成结果（最近邻）
layout(binding = 2) uniform sampler2D glow;    // 辉光小图（线性）

vec3 sampleAt(vec2 uv)
{
    return texture(content, clamp(uv, 0.0, 1.0)).rgb;
}

vec2 curve(vec2 uv) {
    vec2 c = uv - 0.5;
    // 视差（鼠标即观察者）：左右——观察者侧的图像后退；上下——已确认为直觉同向。
    // 铁律：不做桶形畸变（毁打字）——视差只平移电子图像，不弯曲字形。
    c.x += ubuf.view.x * 0.032;
    c.y -= ubuf.view.y * 0.032;
    return c + 0.5;
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

// 阴罩圆点（C64 的 1702 真结构）：3px 水平间距的六角点阵，
// 行奇偶错位 1.5px——每通道在其所属点的位置采样亮度
float triadDot(vec2 sp, float phase)
{
    float rowParity = mod(floor(sp.y), 2.0);
    float px = mod(sp.x - phase - rowParity * 1.5, 3.0) - 1.5;
    float py = mod(sp.y, 2.0) - 1.0;
    return smoothstep(0.78, 0.30, sqrt(px * px + py * py));
}

void main()
{
    // ---- 内容空间：电子图像（含视差平移）。栅网/扫描线/玻璃都在
    // 屏幕空间——真机上它们是固定在玻璃上的，不随视差移动 ----
    // 铁律：无桶形畸变 → 内容 1:1 采样、零内缩。旧内缩（0.965 缩放 +
    // 0.0175 偏移）是桶形时代的防越界补丁：它把内容放大 3.6% 并把
    // 顶部/左侧 ~5px 裁出屏外——顶部第一行文字被吃、整幅偏大（用户报）。
    float ent = step(0.5, ubuf.flags.x);
    vec2 cuv = clamp(curve(v_uv), 0.0, 1.0);
    vec3 col = sampleAt(cuv);

    // 真衍射。单色机：亮边 ±1px R/B 微彩边（bright(x)−bright(x±1) 差分，
    // 与 Crt::edgeDiff 同模型，强度 kDiffAlpha=0.30）——单色光衍射出
    // 单色条纹（物理），不硬造彩虹；白磷机（flags.y=3）衍射无彩，
    // 改中性白边。C64（flags.y=2）：光栅色散——白边按波长散开，
    // 红偏转最大（±2px）、绿居中（±1px）、蓝最小（±0.5px），
    // 亮度（能量）随距离衰减（二阶更弱）
    {
        const float px = 1.0 / ubuf.texSize.x;
        vec3 lm = sampleAt(cuv - vec2(px, 0.0));
        vec3 rp = sampleAt(cuv + vec2(px, 0.0));
        const vec3 w = vec3(0.333);
        bool c64disp = ubuf.flags.y > 1.5 && ubuf.flags.y < 2.5;
        if (c64disp) {
            vec3 lm2 = sampleAt(cuv - vec2(px * 2.0, 0.0));
            vec3 rp2 = sampleAt(cuv + vec2(px * 2.0, 0.0));
            vec3 lmh = sampleAt(cuv - vec2(px * 0.5, 0.0));
            vec3 rph = sampleAt(cuv + vec2(px * 0.5, 0.0));
            float eR2 = max(0.0, dot(col, w) - dot(rp2, w));
            float eL2 = max(0.0, dot(col, w) - dot(lm2, w));
            float eR1 = max(0.0, dot(col, w) - dot(rp, w));
            float eL1 = max(0.0, dot(col, w) - dot(lm, w));
            float eRh = max(0.0, dot(col, w) - dot(rph, w));
            float eLh = max(0.0, dot(col, w) - dot(lmh, w));
            col.r += (eR2 + eL2) * 0.30 + (eR1 + eL1) * 0.12; // 红：2px 为主
            col.g += (eR1 + eL1) * 0.30;                     // 绿：1px
            col.b += (eRh + eLh) * 0.30;                     // 蓝：0.5px
        } else {
            float eR = max(0.0, dot(col, w) - dot(rp, w));
            float eB = max(0.0, dot(col, w) - dot(lm, w));
            float white = step(3.5, ubuf.flags.y); // 机器 3 = 白磷（苹果 II 已归档）
            float frg = mix(0.30, 0.22, white);
            vec3 tR = mix(vec3(1.0, 0.15, 0.02), vec3(0.90, 0.92, 1.0), white);
            vec3 tB = mix(vec3(0.02, 0.15, 1.0), vec3(0.90, 0.92, 1.0), white);
            col += tR * eR * frg;
            col += tB * eB * frg;
        }
    }

    // 亮度（束斑宽度与栅条调制的共同输入）
    float lum = dot(col, vec3(0.333));

    // 束斑物理：水平扫描把束斑沿扫描方向拉长（不对称核：水平 6 抽头
    // 重、垂直 2 抽头轻），亮度越高束斑越宽——加法溢出：亮字核心
    // 饱和、四周变软变晕，暗处不动
    {
        const vec2 off = 1.0 / ubuf.texSize;
        vec3 blur = (sampleAt(cuv + vec2( off.x, 0.0))
                   + sampleAt(cuv - vec2( off.x, 0.0))) * 0.24
                  + (sampleAt(cuv + vec2( off.x * 2.0, 0.0))
                   + sampleAt(cuv - vec2( off.x * 2.0, 0.0))) * 0.14
                  + (sampleAt(cuv + vec2(0.0,  off.y))
                   + sampleAt(cuv + vec2(0.0, -off.y))) * 0.12;
        col = clamp(col + blur * smoothstep(0.12, 0.85, lum) * 0.40, 0.0, 1.0);
    }

    // 聚焦漂移（M4）：四角轻微散焦——真机边缘聚焦变差，角落内容
    // 微微发糊（屏幕空间锚定，随玻璃固定）
    {
        const vec2 off = 1.0 / ubuf.texSize;
        vec3 blur4 = (sampleAt(cuv + vec2( off.x, 0.0))
                    + sampleAt(cuv - vec2( off.x, 0.0))
                    + sampleAt(cuv + vec2(0.0,  off.y))
                    + sampleAt(cuv + vec2(0.0, -off.y))) * 0.25;
        float corner = smoothstep(0.55, 0.85, length(v_uv - 0.5));
        col = mix(col, blur4, corner * 0.35);
    }

    // 辉光叠加：max(col, 小图线性采样 × alpha)——与 Crt::phosphorBloom
    // 的 max 叠加同模型（CPU 版最近邻放大，此处线性放大更平滑）。
    // 叠加发生在栅网之前：辉光与内容一同被掩膜/扫描线调制（与 CPU
    // 烘拍顺序一致）
    col = max(col, texture(glow, cuv).rgb * ubuf.glowInfo.x);

    // ---- 屏幕空间：固定不动的磷粉栅、扫描线（真玻璃结构）----
    vec2 sp = v_uv * ubuf.texSize; // 屏幕物理像素
    // 磷粉栅：束斑越宽（亮处）暗带越宽；束斑水平偏转（内容水平梯度）
    // 让栅相位微移，斜边出摩尔纹。
    // C64（flags.y=2）真彩管：红/绿/蓝三条荧光粉栅按 3px 周期错相排列
    // ——逐通道调制，白字出 RGB 栅纹；单色机保持单栅
    {
        const float px = 1.0 / ubuf.texSize.x;
        float lumL = dot(sampleAt(cuv - vec2(px, 0.0)), vec3(0.333));
        float lumR = dot(sampleAt(cuv + vec2(px, 0.0)), vec3(0.333));
        float beamW = mix(0.12, 0.42, smoothstep(0.05, 0.9, lum));
        float grad = (lumL - lumR) * 0.5;
        bool c64 = ubuf.flags.y > 1.5 && ubuf.flags.y < 2.5;
        if (c64) {
            // 1702 阴罩圆点三色组（旧版竖条栅是 Trinitron 结构，
            // 与真机不符——用户拍板改为点阵）。注意：真机电子束强度
            // 已补偿遮罩损耗，调色板是"穿罩后的亮度"——点阵只提供
            // 纹理（0.70 底 + 点 1.0），不得二次压暗（旧版点太小，
            // 透光 ~5%，把 C64 亮度砍到琥珀的一半）
            // 阴罩物理：纹理可见度 ∝ 束流强度——亮字显出三色点，
            // 暗底几乎无点纹（磷粉几乎未激发，点纹对比趋于零）；
            // 且束流已补偿遮罩，平均透射保持高位（字不暗）
            float texStrength = mix(0.04, 0.35, smoothstep(0.05, 0.6, lum)); // 正常视距三色点细密，振幅 0.35 足够
            col.r *= 1.0 - texStrength * (1.0 - triadDot(sp, 0.0));
            col.g *= 1.0 - texStrength * (1.0 - triadDot(sp, 1.0));
            col.b *= 1.0 - texStrength * (1.0 - triadDot(sp, 2.0));
        } else {
            col *= stripe(sp.x, 0.0, 1.0, beamW, grad);
        }
    }
    // 扫描线：2 物理像素周期柔波（正弦）。旧版硬阶梯 step() 与像素格
    // 拍频形成摩尔纹——静态的宽窄黑条（用户报）；正弦无锯齿、无拍频
    float sl = 0.5 + 0.5 * sin(sp.y * 3.14159265);
    col *= 1.0 - 0.14 * sl;
    col *= 1.0 - 0.05 * sl * ubuf.scanTint.rgb;

    // 噪声与灰尘：细颗粒闪烁 + 稀疏灰尘点（时间驱动，极克制）
    {
        float t = ubuf.timeInfo.x;
        float grain = (hash21(sp + fract(t) * 61.7) - 0.5) * 0.02; // 0.05→0.02：静场颗粒太闪（打字时扰眼）
        float dust = step(0.9992, hash21(floor(sp * 0.05) + floor(t * 8.0)))
                     * (0.5 + 0.5 * hash21(floor(sp * 0.05)));
        col += grain + dust * ubuf.dustCol.rgb * 0.10;
    }

    // 玻璃反光带：一道对角淡白反光（charter：一次性预渲染覆盖层）。
    // 反光带 = n·p ≈ 0.62 的斜线（±1/3.2 距离渐散）。基准角必须让
    // 带落在屏内：n = normalize(0.30, 0.60)——锁定正对视角下带从
    // (1, 0.19) 斜穿到 (0, 0.69)，+0.045 强度 ≈ +2.8 亮度（极淡，
    // 玻璃微光；旧版误用 n=normalize(0.35,0.94) 使 dot=1.0 → 带
    // 飞出屏外，反光凭空消失）
    {
        vec2 n = normalize(vec2(0.30 + ubuf.view.x * 0.10, 0.60));
        float refl = pow(max(0.0, 1.0 - abs(dot(n, vec2(0.35, 0.94)) - 0.62) * 3.2), 2.0);
        col += ubuf.refl.rgb * refl * 0.045;
    }

    // 扫描时序：真机 60Hz 场扫描肉眼不可见（视觉暂留）——日常状态
    // 无慢带。慢放镜头（C64 0.7s 激励线 / 单色 3s 暗带）只在解锁追随
    // 视角（flags.x=1，风格化现场）时出现——用户拍板：日常打字不要，
    // 留在 Cmd+Shift+T 的现场里
    if (ubuf.flags.x > 0.5) {
        bool c64 = ubuf.flags.y > 1.5 && ubuf.flags.y < 2.5;
        if (c64) {
            float H = ubuf.texSize.y;
            float scan = fract(ubuf.timeInfo.x * (1.0 / 0.7)); // 自上而下
            float d = sp.y - scan * H; // >0 未扫到；<0 刚扫过
            float tail = exp(max(d, -H) * (14.0 / H)); // 熄灭指数（更长更软）
            float pulse = smoothstep(-H * 0.08, 0.0, d); // 束流在线上
            float exc = clamp(tail * pulse, 0.0, 1.0);
            col *= 0.96 + 0.07 * exc; // 柔和：波面存在但不闪
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

    // 暗角（屏幕空间——玻璃固定）：从半程起渐暗的连续径向渐变——
    // 四角与边缘之间无接缝（旧版起步过早 + 边框带过窄过硬，观感
    // 是"四个暗角之间有明显缝隙、像黑边框"——用户报）。
    float d = length(v_uv - 0.5) * 1.35;
    col *= 1.0 - mix(0.14, 0.17, ent) * smoothstep(0.50, 1.0, d);
    if (ent > 0.5) {
        // 边框阴影带：宽 15% 的柔和渐散（旧版 7% 窄带 55% 压暗 = 硬黑边）。
        // 压暗但不裁字——文字仍可见
        vec2 ed = abs(v_uv - 0.5) * 2.0; // 0 中心 → 1 边缘
        float bezel = smoothstep(0.70, 1.0, max(ed.x, ed.y));
        col *= 1.0 - 0.30 * bezel;
        // 旧版这里还有一道 ent 专属斜向玻璃反光 gl（0.03 浮点强度 =
        // +19 亮度）——在暗底上是 +105% 的刺眼斜亮带，与四角暗角
        // 拼出"黑边框感"（用户报：暗角之间有缝隙）。主反光带已
        // 覆盖 charter 的"对角淡白反光"，此处删除
    }

    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
