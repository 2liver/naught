// analyze_png.cpp —— 临时取证工具：统计 PNG 的颜色分布与异常区域（诊断用）
#include <QCoreApplication>
#include <QImage>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    for (int i = 1; i < argc; ++i) {
        QImage img(argv[i]);
        if (img.isNull()) {
            printf("%s: 无法读取\n", argv[i]);
            continue;
        }
        img = img.convertToFormat(QImage::Format_ARGB32);
        long green = 0, red = 0, blue = 0, amber = 0, dark = 0, bright = 0, other = 0;
        long sumR = 0, sumG = 0, sumB = 0, sumA = 0, n = 0;
        int gMinX = 1 << 30, gMinY = 1 << 30, gMaxX = -1, gMaxY = -1;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const QRgb px = row[x];
                const int r = qRed(px), g = qGreen(px), b = qBlue(px), a = qAlpha(px);
                sumR += r; sumG += g; sumB += b; sumA += a; ++n;
                if (g > r + 40 && g > b + 40 && g > 80) {
                    ++green;
                    gMinX = qMin(gMinX, x); gMinY = qMin(gMinY, y);
                    gMaxX = qMax(gMaxX, x); gMaxY = qMax(gMaxY, y);
                } else if (r > g + 40 && r > b + 40 && r > 80) {
                    ++red;
                } else if (b > r + 40 && b > g + 40 && b > 80) {
                    ++blue;
                } else if (r > 140 && g > 70 && b < 90) {
                    ++amber;
                } else if (r < 50 && g < 50 && b < 50) {
                    ++dark;
                } else if (r > 180 && g > 180 && b > 180) {
                    ++bright;
                } else {
                    ++other;
                }
            }
        }
        printf("%s (%dx%d)\n", argv[i], img.width(), img.height());
        printf("  均值 R=%ld G=%ld B=%ld A=%ld (n=%ld)\n",
               n ? sumR / n : 0, n ? sumG / n : 0, n ? sumB / n : 0, n ? sumA / n : 0, n);
        printf("  amber=%ld red=%ld blue=%ld green=%ld dark=%ld bright=%ld other=%ld\n",
               amber, red, blue, green, dark, bright, other);
        if (green > 0)
            printf("  绿色区域 bbox: (%d,%d)-(%d,%d)\n", gMinX, gMinY, gMaxX, gMaxY);
    }
    return 0;
}
