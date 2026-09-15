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
inline const QColor kInk(0xFF, 0xB0, 0x00);      // 磷粉核心亮色（琥珀）
inline const QColor kInkDim(0x8C, 0x5E, 0x00);   // 暗磷（行号等次要元素）
inline const QColor kCursorBlock(0xFF, 0xE2, 0xA0); // 炽磷块光标：满格激发，比文字更亮更白
inline const QColor kBg(0x0C, 0x09, 0x03);       // 近黑暖底
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

// 磷粉余晖（一期·回接）：上一帧内容以加法混入新帧——光是叠加的，
// 滚动/打字留下原位渐暗的残影，静态画面微微增亮（磷粉永不完全
// 熄灭），随每帧重建指数衰减。分通道权重：红磷拖尾最长、绿次之、
// 蓝最快（琥珀磷粉的真实余热色调），残影因此偏暖。
inline void phosphorPersistence(QImage &img, const QImage &prev)
{
    if (prev.isNull() || prev.size() != img.size())
        return;
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        uchar *dst = img.scanLine(y);
        const uchar *src = prev.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const int i = x * 4; // BGRA
            dst[i] = qMin(255, dst[i] + int(src[i] * 0.05));
            dst[i + 1] = qMin(255, dst[i + 1] + int(src[i + 1] * 0.12));
            dst[i + 2] = qMin(255, dst[i + 2] + int(src[i + 2] * 0.18));
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
    QImage glow = img.scaled(img.size() / 4, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation)
                      .convertToFormat(QImage::Format_ARGB32);
    glow = gaussianBlur(glow, 2, 3); // 半径 2（1/4 图 = 全图 8px 光晕）
    glow = glow.scaled(img.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
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
