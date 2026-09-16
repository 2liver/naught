// ascii_art.h —— 图片 → 字符画（M3）：灰度 + 边缘密度 → 字符梯度。
// 隐藏功能：拖图片进「無」即换页（README 不提及，快捷键入「项」）。
// 梯度选择与 ~/Projects/AsciiTools 同源：边缘密集的图用粗梯度
// ' .:*#@'，平滑的图用细梯度 ' .,-~:;=!*#$@'（阈值 0.15×256≈38）。
// 真彩机型（C64）另有逐字符 16 色量化路径（chafa 式：亮度选字符、
// 颜色承载色相）。
#pragma once

#include <QImage>
#include <QString>
#include <QVector>

namespace Ascii {

// 把图片渲染成 cols×rows 的字符网格文本（每行 cols 个字符，行间换行）。
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
        if (r + 1 < rows)
            out += QLatin1Char('\n');
    }
    return out;
}

// 图片 → 字符网格 + 每格颜色（16 色调色板最近邻量化）——真彩机型用。
// colors 与返回文本逐字符对应（不含换行）。字符仍按亮度选梯度，
// 颜色承载色相——chafa 式"亮度字符 + 量化前景色"的简化版。
inline QString imageToTextColors(const QImage &src, int cols, int rows,
                                 QVector<QRgb> &colors,
                                 const QColor palette[16])
{
    colors.clear();
    if (src.isNull() || cols < 2 || rows < 2)
        return QString();
    const QImage g = src.convertToFormat(QImage::Format_ARGB32);
    QVector<int> lum(cols * rows);
    QVector<int> edge(cols * rows);
    QVector<QRgb> cellCol(cols * rows);
    for (int r = 0; r < rows; ++r) {
        const int y0 = r * g.height() / rows;
        const int y1 = (r + 1) * g.height() / rows;
        for (int c = 0; c < cols; ++c) {
            const int x0 = c * g.width() / cols;
            const int x1 = (c + 1) * g.width() / cols;
            long s = 0, n = 0, e = 0, en = 0;
            long sr = 0, sg = 0, sb = 0;
            for (int y = y0; y < y1; ++y) {
                const QRgb *line = reinterpret_cast<const QRgb *>(g.constScanLine(y));
                const QRgb *prev = y > y0
                    ? reinterpret_cast<const QRgb *>(g.constScanLine(y - 1))
                    : line;
                for (int x = x0; x < x1; ++x) {
                    s += qGray(line[x]);
                    sr += qRed(line[x]);
                    sg += qGreen(line[x]);
                    sb += qBlue(line[x]);
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
            // 16 色最近邻量化（欧氏距离）
            const int ar = n ? int(sr / n) : 0;
            const int ag = n ? int(sg / n) : 0;
            const int ab = n ? int(sb / n) : 0;
            int best = 0;
            long bestD = 1L << 60;
            for (int p = 0; p < 16; ++p) {
                const int dr = ar - palette[p].red();
                const int dg = ag - palette[p].green();
                const int db = ab - palette[p].blue();
                const long d = long(dr) * dr + long(dg) * dg + long(db) * db;
                if (d < bestD) {
                    bestD = d;
                    best = p;
                }
            }
            cellCol[r * cols + c] = palette[best].rgb();
        }
    }
    long edgeSum = 0;
    for (int v : edge)
        edgeSum += v;
    const bool coarse = (edgeSum / (cols * rows)) > 38;
    const char *ramp = coarse ? " .:*#@" : " .,-~:;=!*#$@";
    const int levels = int(qstrlen(ramp)) - 1;
    QString out;
    colors.reserve(cols * rows);
    out.reserve(cols * (rows + 1));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int idx = levels * lum[r * cols + c] / 256;
            out += QLatin1Char(ramp[qBound(0, idx, levels)]);
            colors.append(cellCol[r * cols + c]);
        }
        if (r + 1 < rows)
            out += QLatin1Char('\n'); // colors 与字符一一对应，不含换行
    }
    return out;
}

} // namespace Ascii
