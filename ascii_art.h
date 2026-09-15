// ascii_art.h —— 图片 → 字符画（M3）：灰度 + 边缘密度 → 字符梯度。
// 隐藏功能：拖图片进「無」即换页（README 不提及，快捷键入「项」）。
// 梯度选择与 ~/Projects/AsciiTools 同源：边缘密集的图用粗梯度
// ' .:*#@'，平滑的图用细梯度 ' .,-~:;=!*#$@'（阈值 0.15×256≈38）。
#pragma once

#include <QImage>
#include <QString>
#include <QVector>

namespace Ascii {

// 把图片渲染成 cols×rows 的字符网格文本（每行 cols 个字符 + 换行）。
// 纯函数：自检直接以合成图验证。
inline QString imageToText(const QImage &src, int cols, int rows)
{
    if (src.isNull() || cols < 2 || rows < 2)
        return QString();
    const QImage g = src.convertToFormat(QImage::Format_ARGB32);
    QVector<int> lum(cols * rows);
    QVector<int> edge(cols * rows);
    for (int r = 0; r < rows; ++r) {
        const int y0 = r * g.height() / rows;
        const int y1 = (r + 1) * g.height() / rows;
        for (int c = 0; c < cols; ++c) {
            const int x0 = c * g.width() / cols;
            const int x1 = (c + 1) * g.width() / cols;
            long s = 0, n = 0, e = 0, en = 0;
            for (int y = y0; y < y1; ++y) {
                const QRgb *line = reinterpret_cast<const QRgb *>(g.constScanLine(y));
                const QRgb *prev = y > y0
                    ? reinterpret_cast<const QRgb *>(g.constScanLine(y - 1))
                    : line;
                for (int x = x0; x < x1; ++x) {
                    s += qGray(line[x]);
                    ++n;
                    if (x > x0) {
                        e += qAbs(qGray(line[x]) - qGray(line[x - 1]));
                        ++en;
                    }
                    if (y > y0) {
                        e += qAbs(qGray(line[x]) - qGray(prev[x]));
                        ++en;
                    }
                }
            }
            lum[r * cols + c] = n ? int(s / n) : 0;
            edge[r * cols + c] = en ? int(e / en) : 0;
        }
    }
    long edgeSum = 0;
    for (int v : edge)
        edgeSum += v;
    const bool coarse = (edgeSum / (cols * rows)) > 38; // 0.15 × 256 ≈ 38
    const char *ramp = coarse ? " .:*#@" : " .,-~:;=!*#$@";
    const int levels = int(qstrlen(ramp)) - 1;
    QString out;
    out.reserve(cols * (rows + 1));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int idx = levels * lum[r * cols + c] / 256;
            out += QLatin1Char(ramp[qBound(0, idx, levels)]);
        }
        out += QLatin1Char('\n');
    }
    return out;
}

} // namespace Ascii
