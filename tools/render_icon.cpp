// 「無」(naught) 图标生成工具
// 用思源宋体 Heavy（SIL OFL 1.1）渲染「無」字，产出：
//   iconset/*.png —— iconutil 输入
//   naught.icns —— macOS 应用图标
//   naught.ico —— Windows 可执行文件图标（多尺寸 PNG 条目）
//   naught.png —— 256px 运行时窗口图标（QRC 引用）
// 用法：naught_icon_tool <SourceHanSerifSC-Heavy.otf> <输出目录>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QProcess>

static QImage render(int px, const QFont &font)
{
    QImage img(px, px, QImage::Format_ARGB32);
    img.fill(QColor(0, 0, 0)); // 黑底白字（阴）
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f = font;
    f.setPixelSize(int(px * 0.72));
    QFontMetricsF fm(f);
    const QRectF br = fm.tightBoundingRect(QStringLiteral("無"));
    const qreal baseline = px / 2.0 - (br.top() + br.bottom()) / 2.0;
    const qreal x = (px - br.width()) / 2.0 - br.left();
    p.setFont(f);
    p.setPen(QColor(255, 255, 255));
    p.drawText(QPointF(x, baseline), QStringLiteral("無"));
    return img;
}

static void writeIco(const QDir &out, const QFont &font)
{
    const int sizes[] = {256, 128, 64, 48, 32, 16};
    const int n = int(sizeof(sizes) / sizeof(sizes[0]));
    QByteArray header = QByteArray(6, '\0');
    header[2] = 1; // type: icon
    header[4] = char(n & 0xff);
    header[5] = char((n >> 8) & 0xff);
    QByteArray dir;
    QList<QByteArray> pngs;
    int offset = 6 + 16 * n;
    for (int s : sizes) {
        QImage img = render(s, font);
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        dir.append(char(s >= 256 ? 0 : s)); // 0 表示 256
        dir.append(char(s >= 256 ? 0 : s));
        dir.append(char(0));
        dir.append(char(0));
        dir.append(char(1));
        dir.append(char(0)); // planes
        dir.append(char(32));
        dir.append(char(0)); // bpp
        const quint32 sz = quint32(png.size());
        for (int i = 0; i < 4; ++i)
            dir.append(char((sz >> (8 * i)) & 0xff));
        for (int i = 0; i < 4; ++i)
            dir.append(char((offset >> (8 * i)) & 0xff));
        offset += int(sz);
        pngs.append(png);
    }
    QFile f(out.filePath(QStringLiteral("naught.ico")));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(header);
        f.write(dir);
        for (const QByteArray &p : pngs)
            f.write(p);
    } else {
        qWarning("cannot write naught.ico");
    }
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        qWarning("usage: naught_icon_tool <SourceHanSerifSC-Heavy.otf> <outdir>");
        return 2;
    }
    const QString otf = QString::fromLocal8Bit(argv[1]);
    QDir out(QString::fromLocal8Bit(argv[2]));
    if (!out.exists())
        out.mkpath(QStringLiteral("."));
    out.mkpath(QStringLiteral("naught.iconset"));

    const int id = QFontDatabase::addApplicationFont(otf);
    if (id < 0) {
        qWarning("cannot load font: %s", qPrintable(otf));
        return 3;
    }
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty())
        return 3;
    const QFont font(families.first());

    struct Sz {
        int px;
        const char *name;
    };
    const Sz sizes[] = {
        {16, "icon_16x16.png"}, {32, "icon_16x16@2x.png"},
        {32, "icon_32x32.png"}, {64, "icon_32x32@2x.png"},
        {128, "icon_128x128.png"}, {256, "icon_128x128@2x.png"},
        {256, "icon_256x256.png"}, {512, "icon_256x256@2x.png"},
        {512, "icon_512x512.png"}, {1024, "icon_512x512@2x.png"},
    };
    for (const Sz &s : sizes)
        render(s.px, font).save(out.filePath(QStringLiteral("naught.iconset/") + QLatin1String(s.name)));

    render(256, font).save(out.filePath(QStringLiteral("naught.png")));

#ifdef Q_OS_MACOS
    QProcess iconutil;
    iconutil.start(QStringLiteral("iconutil"),
                   {QStringLiteral("-c"), QStringLiteral("icns"),
                    out.filePath(QStringLiteral("naught.iconset")),
                    QStringLiteral("-o"), out.filePath(QStringLiteral("naught.icns"))});
    iconutil.waitForFinished(30000);
    if (iconutil.exitStatus() != QProcess::NormalExit || iconutil.exitCode() != 0)
        qWarning("iconutil failed");
#endif

    writeIco(out, font);
    qInfo("icons written to %s", qPrintable(out.absolutePath()));
    return 0;
}
