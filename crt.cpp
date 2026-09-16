// crt.cpp —— 磷光管线像素函数实现（自 crt.h 拆分：头部只留调色板
// 常量与声明，像素热循环不再随头部被 editor.h/crt_view.cpp 两个 TU
// 重编译）。crt.h 结构未动，纯编译边界调整。
#include "crt.h"

namespace Crt {

QImage edgeDiff(const QImage &src, int dx)
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

void boxBlurH(const QImage &src, QImage &dst, int radius)
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

void boxBlurV(const QImage &src, QImage &dst, int radius)
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

QImage gaussianBlur(const QImage &src, int radius, int iterations)
{
    QImage a = src;
    QImage b(src.size(), src.format());
    for (int i = 0; i < iterations; ++i) {
        boxBlurH(a, b, radius);
        boxBlurV(b, a, radius);
    }
    return a;
}

void phosphorPersistence(QImage &img, const QImage &prev1, const QImage &prev2, const Palette &pal)
{
    if (prev1.isNull() || prev1.size() != img.size())
        return;
    const bool have2 = !prev2.isNull() && prev2.size() == img.size();
    const int w = img.width(), h = img.height();
    // lighten(max) 模型：磷光余晖只照亮"内容已离开"的像素——
    // 仍亮着的像素保持激发亮度（物理：磷光的余晖不叠加在还在激发
    // 的像素上）。旧版加法模型把静态文字稳态放大到 1/(1-α)≈1.5-2×
    // （虚胖亮度），重印时新旧画叠加 = 过曝闪光。
    const qreal *p1 = pal.persist1;
    const qreal *p2 = pal.persist2;
    for (int y = 0; y < h; ++y) {
        uchar *dst = img.scanLine(y);
        const uchar *s1 = prev1.constScanLine(y);
        const uchar *s2 = have2 ? prev2.constScanLine(y) : nullptr;
        for (int x = 0; x < w; ++x) {
            const int i = x * 4;
            dst[i] = qMax(dst[i], uchar(s1[i] * p1[0]));
            dst[i + 1] = qMax(dst[i + 1], uchar(s1[i + 1] * p1[1]));
            dst[i + 2] = qMax(dst[i + 2], uchar(s1[i + 2] * p1[2]));
            if (s2) {
                dst[i] = qMax(dst[i], uchar(s2[i] * p2[0]));
                dst[i + 1] = qMax(dst[i + 1], uchar(s2[i + 1] * p2[1]));
                dst[i + 2] = qMax(dst[i + 2], uchar(s2[i + 2] * p2[2]));
            }
        }
    }
}

void phosphorBloom(QImage &img, qreal alpha)
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
