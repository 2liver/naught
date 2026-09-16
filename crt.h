// crt.h —— 「显」显像管层：零 UI、单一琥珀磷光模式、纯画面拟真。
// 架构（二期修复后）：视口不透明（Base=kBg），所有光效为文字上方
// 的纯 SourceOver 层。真机的窗口合成器对透明视口会产生未初始化
// 内存的"绿洞"（render/grab 亦全空），透明架构已整体废除。
// 文字快照由 Editor::paintTextSnapshot 自绘（块走查几何，引擎无关）。
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>

class Editor;

namespace Crt {
// 调色板（M2）：一台机器一套磷光——文字、行号、笔迹、块光标、底色，
// 以及 shader 侧的扫描线掺色、玻璃反光色、灰尘色。加机器 = 加预设。
struct Palette {
    QColor ink;         // 磷粉核心亮色
    QColor inkDim;      // 暗磷（行号等次要元素）
    QColor cursorBlock; // 炽磷块光标（满格激发）
    QColor bg;          // 近黑底
    QColor scanTint;    // 扫描线暗行掺色（归一化 RGB 送 shader）
    QColor refl;        // 玻璃反光色
    QColor dust;        // 灰尘点色
    qreal glowAlpha;    // 辉光叠加强度（1a7s39ge 实例参考）
    // 余晖实测标定：双指数快/慢分量的逐通道帧间残留（按各磷粉
    // P1/P3/P4/P22 的实测衰减时标换算到 80ms 快照帧率——真机余晖
    // 一两帧内散尽，旧版拖尾是风格化不是拟真）
    qreal persist1[3] = { 0.08f, 0.05f, 0.02f }; // 快分量（B,G,R 旧约定）
    qreal persist2[3] = { 0.12f, 0.08f, 0.03f }; // 慢分量
};

// 琥珀（Osborne Executive 1982）：出厂机器
inline const Palette kAmber{
    QColor(0xFF, 0xB0, 0x00), // ink
    QColor(0x8C, 0x5E, 0x00), // inkDim
    QColor(0xFF, 0xE2, 0xA0), // cursorBlock
    QColor(0x0C, 0x09, 0x03), // bg
    QColor(0x59, 0x66, 0x40), // scanTint (0.35,0.4,0.25)
    QColor(0xFF, 0xE0, 0x9E), // refl (1.0,0.88,0.62)
    QColor(0xE6, 0xCC, 0x99), // dust (0.9,0.8,0.6)
    0.42,                     // glowAlpha
    { 0.35f, 0.25f, 0.10f },  // P3 琥珀余晖：中余晖，10% 点 ~60ms
    { 0.15f, 0.10f, 0.04f },
};

// 绿磷（IBM 5100 1975）：按 1a7s39ge 实例「终端绿」主题校准
// （bg #0a0a0a / text #33ff33 / textDim #1a8a1a / cursor #00ff00 /
//  glow rgba(0,255,0,0.35)）
inline const Palette kGreen{
    QColor(0x33, 0xFF, 0x33), // ink
    QColor(0x1A, 0x8A, 0x1A), // inkDim（行号暗绿）
    QColor(0x00, 0xFF, 0x00), // cursorBlock（纯绿，实例同款）
    QColor(0x0A, 0x0A, 0x0A), // bg（中性近黑，实例同款）
    QColor(0x40, 0x66, 0x59), // scanTint (0.25,0.4,0.35)
    QColor(0xBF, 0xEA, 0xCC), // refl (0.75,0.92,0.8)
    QColor(0xB3, 0xE6, 0xBF), // dust (0.7,0.9,0.75)
    0.35,                     // glowAlpha（实例同款）
    { 0.18f, 0.22f, 0.12f },  // P1 绿磷余晖：中短，10% 点 ~30ms
    { 0.06f, 0.09f, 0.04f },
};

// C64（Commodore 64 1982）：真彩机型——蓝屏 + 16 色逐字符前景色
// （chrome 调色板按子代理调研：默认浅蓝字 #6F7FDC 于蓝屏 #2A1C6E）。
// 字符画的颜色来自 kC64Colors 的 16 色量化。
inline const Palette kC64{
    QColor(0x6F, 0x7F, 0xDC), // ink（默认浅蓝字）
    QColor(0x4A, 0x54, 0x90), // inkDim
    QColor(0x7C, 0x8D, 0xFF), // cursorBlock
    QColor(0x2A, 0x1C, 0x6E), // bg（蓝屏）
    QColor(0x1B, 0x14, 0x50), // scanTint
    QColor(0x40, 0x40, 0xE0), // refl
    QColor(0x5A, 0x5A, 0x6E), // dust
    0.38,                     // glowAlpha
    { 0.06f, 0.06f, 0.05f },  // P22 彩管余晖：快分量 µs 级（帧级几乎不可见），慢分量短
    { 0.03f, 0.03f, 0.02f },
};

// C64 标准 16 色（字符画逐字符前景色的量化目标）
inline const QColor kC64Colors[16] = {
    QColor(0x00, 0x00, 0x00), QColor(0xFF, 0xFF, 0xFF), QColor(0x88, 0x39, 0x32),
    QColor(0x67, 0xB6, 0xBD), QColor(0x8B, 0x3F, 0x96), QColor(0x55, 0xA0, 0x49),
    QColor(0x40, 0x31, 0x8D), QColor(0xBF, 0xCE, 0x72), QColor(0x8B, 0x54, 0x29),
    QColor(0x57, 0x42, 0x00), QColor(0xB8, 0x69, 0x62), QColor(0x50, 0x50, 0x50),
    QColor(0x78, 0x78, 0x78), QColor(0x94, 0xE0, 0x89), QColor(0x78, 0x69, 0xC4),
    QColor(0x9F, 0x9F, 0x9F),
};

// IBM PC 5150（1981）：CGA 白字模式——白磷真机（80×25）。
// 字形用 Fixedsys Excelsior（CC0 公有领域，经典字库已捆绑）精确重绘。
inline const Palette kWhite{
    QColor(0xE8, 0xE8, 0xE0), // ink（暖白磷光）
    QColor(0x8A, 0x8A, 0x80), // inkDim
    QColor(0xFF, 0xFF, 0xFF), // cursorBlock（纯白满束流）
    QColor(0x05, 0x05, 0x05), // bg（CGA 黑）
    QColor(0x4A, 0x4A, 0x50), // scanTint（中性微冷）
    QColor(0xE0, 0xE0, 0xD8), // refl
    QColor(0xC0, 0xC0, 0xB8), // dust
    0.38,                     // glowAlpha
    { 0.22f, 0.22f, 0.20f },  // P4 白磷余晖：中短
    { 0.09f, 0.09f, 0.08f },
};

// 兼容别名（出厂琥珀；自检黄金参考沿用）
inline const QColor kInk = kAmber.ink;
inline const QColor kInkDim = kAmber.inkDim;
inline const QColor kCursorBlock = kAmber.cursorBlock;
inline const QColor kBg = kAmber.bg;
inline constexpr int kScanPeriod = 3;            // 扫描线周期 px
inline constexpr int kGlowMinScroll = 24;        // 滚动超过该位移才追辉光
inline constexpr qint64 kGlowMinIntervalMs = 80; // 辉光刷新最小间隔
inline constexpr qreal kDiffAlpha = 0.30;        // 衍射彩边强度

// 竖直边差分：bright(x) − bright(x±1) > 0 处即竖直亮边。
// 磷粉三色栅是 N=3 的竖直二元光栅：只竖直边衍射，sinc 包络把彩边
// 压到 1~2px，R/B 错位 ±1px、色相相反（0/π 相位）。返回 alpha 掩膜。
// 注意：ARGB32 内存布局为 BGRA，直接用字节寻址（QRgb* 重解释会错位）。
inline QImage edgeDiff(const QImage &src, int dx)
{
    QImage out(src.size(), QImage::Format_Alpha8);
    out.fill(Qt::transparent);
    if (src.isNull() || src.width() < 2)
        return out;
    for (int y = 0; y < src.height(); ++y) {
        const uchar *line = src.constScanLine(y);
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            const int x2 = qBound(0, x + dx, src.width() - 1);
            const int a = line[x * 4 + 3];
            const int b = line[x2 * 4 + 3];
            dst[x] = (a > b) ? (a - b) : 0;
        }
    }
    return out;
}

// 分离盒式模糊一轮（水平）；三轮 H+V 近似高斯。同样用字节寻址。
inline void boxBlurH(const QImage &src, QImage &dst, int radius)
{
    const int w = src.width(), h = src.height(), denom = 2 * radius + 1;
    for (int y = 0; y < h; ++y) {
        const uchar *row = src.constScanLine(y);
        uchar *out = dst.scanLine(y);
        int r = 0, g = 0, b = 0, a = 0;
        // 边缘钳制窗口 [-r..r]：0 号像素计 (r+1) 次——少计会让滑动和
        // 在亮块尾缘失衡变负，uchar() 回绕成 253 涂满整行
        for (int x = -radius; x <= radius; ++x) {
            const int i = qBound(0, x, w - 1) * 4;
            r += row[i + 2]; g += row[i + 1]; b += row[i]; a += row[i + 3];
        }
        for (int x = 0; x < w; ++x) {
            out[x * 4 + 2] = uchar(r / denom);
            out[x * 4 + 1] = uchar(g / denom);
            out[x * 4] = uchar(b / denom);
            out[x * 4 + 3] = uchar(a / denom);
            const int addI = qMin(x + radius + 1, w - 1) * 4;
            const int subI = qMax(0, x - radius) * 4;
            r += row[addI + 2] - row[subI + 2];
            g += row[addI + 1] - row[subI + 1];
            b += row[addI] - row[subI];
            a += row[addI + 3] - row[subI + 3];
        }
    }
}

inline void boxBlurV(const QImage &src, QImage &dst, int radius)
{
    const int w = src.width(), h = src.height(), denom = 2 * radius + 1;
    for (int x = 0; x < w; ++x) {
        int r = 0, g = 0, b = 0, a = 0;
        for (int y = -radius; y <= radius; ++y) { // 边缘钳制窗口，同上
            const uchar *row = src.constScanLine(qBound(0, y, h - 1));
            const int i = x * 4;
            r += row[i + 2]; g += row[i + 1]; b += row[i]; a += row[i + 3];
        }
        for (int y = 0; y < h; ++y) {
            uchar *out = dst.scanLine(y);
            const int i = x * 4;
            out[i + 2] = uchar(r / denom);
            out[i + 1] = uchar(g / denom);
            out[i] = uchar(b / denom);
            out[i + 3] = uchar(a / denom);
            const uchar *addRow = src.constScanLine(qMin(y + radius + 1, h - 1));
            const uchar *subRow = src.constScanLine(qMax(0, y - radius));
            r += addRow[i + 2] - subRow[i + 2];
            g += addRow[i + 1] - subRow[i + 1];
            b += addRow[i] - subRow[i];
            a += addRow[i + 3] - subRow[i + 3];
        }
    }
}

inline QImage gaussianBlur(const QImage &src, int radius, int iterations)
{
    QImage a = src;
    QImage b(src.size(), src.format());
    for (int i = 0; i < iterations; ++i) {
        boxBlurH(a, b, radius);
        boxBlurV(b, a, radius);
    }
    return a;
}

// 磷粉余晖（一期·回接，M4 双指数）：上一帧与上上帧以快慢两个分量
// 加法混入新帧——快分量 = 1 帧内的亮回响，慢分量 = 长尾余热（真磷粉
// 的双指数衰减近似）。分通道权重：红磷拖尾最长、绿次之、蓝最快，
// 残影因此偏暖。静态画面微微增亮（磷粉永不完全熄灭）。
inline void phosphorPersistence(QImage &img, const QImage &prev1, const QImage &prev2,
                                 const Palette &pal)
{
    if (prev1.isNull() || prev1.size() != img.size())
        return;
    const bool have2 = !prev2.isNull() && prev2.size() == img.size();
    const int w = img.width(), h = img.height();
    // BGRA 权重：按调色板的实测标定（各磷粉 P1/P3/P4/P22 的衰减时标
    // 换算到快照帧率）——真机余晖一两帧内散尽
    const qreal *p1 = pal.persist1;
    const qreal *p2 = pal.persist2;
    for (int y = 0; y < h; ++y) {
        uchar *dst = img.scanLine(y);
        const uchar *s1 = prev1.constScanLine(y);
        const uchar *s2 = have2 ? prev2.constScanLine(y) : nullptr;
        for (int x = 0; x < w; ++x) {
            const int i = x * 4;
            int b = dst[i] + int(s1[i] * p1[0]);
            int g = dst[i + 1] + int(s1[i + 1] * p1[1]);
            int r = dst[i + 2] + int(s1[i + 2] * p1[2]);
            if (s2) {
                b += int(s2[i] * p2[0]);
                g += int(s2[i + 1] * p2[1]);
                r += int(s2[i + 2] * p2[2]);
            }
            dst[i] = qMin(255, b);
            dst[i + 1] = qMin(255, g);
            dst[i + 2] = qMin(255, r);
        }
    }
}

// 磷光辉光（二期三件套·回接）：1/4 降采样往返 + 三轮分离盒式模糊
// （真高斯形状），再以 lighten（max）叠回——文字核心保持全亮、
// 四周长出磷粉光晕。整图字节直写，绕开 QImage 画笔的引擎层 DPR
// 二次缩放；只在快照重建（80ms 节流）时执行。
inline void phosphorBloom(QImage &img, qreal alpha = 0.42)
{
    if (img.isNull() || img.width() < 8 || img.height() < 8)
        return;
    // P2 降载：4×4 平均降采样（Smooth 滤镜在 1/4 图上是 CPU 大户，
    // 平均采样对辉光完全足够）+ 最近邻放大（辉光本就平滑，4×4 块
    // 不可见；Smooth 放大到全图是最大的单笔开销）
    QImage glow(qMax(1, img.width() / 4), qMax(1, img.height() / 4),
                QImage::Format_ARGB32);
    for (int y = 0; y < glow.height(); ++y) {
        uchar *dst = glow.scanLine(y);
        const int y0 = qMin(y * 4, img.height() - 1);
        for (int x = 0; x < glow.width(); ++x) {
            long b = 0, g = 0, r = 0;
            const int x0 = qMin(x * 4, img.width() - 1);
            for (int dy = 0; dy < 4; ++dy) {
                const uchar *src = img.constScanLine(qMin(y0 + dy, img.height() - 1));
                for (int dx = 0; dx < 4; ++dx) {
                    const int i = qMin(x0 + dx, img.width() - 1) * 4;
                    b += src[i];
                    g += src[i + 1];
                    r += src[i + 2];
                }
            }
            dst[x * 4] = uchar(b / 16);
            dst[x * 4 + 1] = uchar(g / 16);
            dst[x * 4 + 2] = uchar(r / 16);
            dst[x * 4 + 3] = 255;
        }
    }
    glow = gaussianBlur(glow, 2, 3); // 半径 2（1/4 图 = 全图 8px 光晕）
    glow = glow.scaled(img.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    const int w = qMin(img.width(), glow.width());
    const int h = qMin(img.height(), glow.height());
    for (int y = 0; y < h; ++y) {
        uchar *dst = img.scanLine(y);
        const uchar *g = glow.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const int i = x * 4; // BGRA
            dst[i] = qMax(dst[i], uchar(g[i] * alpha));
            dst[i + 1] = qMax(dst[i + 1], uchar(g[i + 1] * alpha));
            dst[i + 2] = qMax(dst[i + 2], uchar(g[i + 2] * alpha));
        }
    }
}
} // namespace Crt
