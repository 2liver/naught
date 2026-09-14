// crt.cpp —— 「显」显像管层实现。全部效果同生命周期，随关闭零残留。
// 架构（二期修复后）：视口不透明，本层为文字上方唯一效果层，纯 SourceOver。
#include "crt.h"

#include "editor.h"

#include <random>

#include <QElapsedTimer>
#include <QPainter>
#include <QScrollBar>

CrtOverlay::CrtOverlay(Editor *editor)
    : QWidget(editor)
    , m_editor(editor)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);

    // 扫描线掩膜：1×3 行，中间半暗、末行最暗（磷粉行结构）
    m_scanMask = QImage(1, Crt::kScanPeriod, QImage::Format_ARGB32);
    const int rows[Crt::kScanPeriod] = {0, 58, 92};
    for (int y = 0; y < Crt::kScanPeriod; ++y)
        m_scanMask.setPixelColor(0, y, QColor(0, 0, 0, rows[y]));

    // 噪声两帧：行亮度抖动（真实 CRT 的噪声是扫描线明暗起伏，不是撒白点）。
    // 全尺寸预生成、1:1 绘制——此前 1×256 拉伸到全窗的极端缩放是
    // 真机红竖条纹的元凶（引擎对 1px 宽图像的插值产生垃圾列）

    m_noiseTimer.setInterval(120);
    connect(&m_noiseTimer, &QTimer::timeout, this, [this] {
        m_noiseFrame = 1 - m_noiseFrame;
        m_bandPhase += 0.04; // 120ms / 3000ms：刷新带 3 秒扫一周
        if (m_bandPhase >= 1.0)
            m_bandPhase -= 1.0;
        m_exciteAge *= 0.76; // 磷粉激发回落（约 500ms 衰减包络）
        update();
    });
    m_warmTimer.setInterval(24);
    connect(&m_warmTimer, &QTimer::timeout, this, [this] {
        m_warm = qMax(0.0, m_warm - 0.055);
        update();
        if (m_warm <= 0.0)
            m_warmTimer.stop();
    });
    // 残影渐暗：30ms 一拍，约 250ms 熄灭——渐变包络，不是开关
    m_fadeTimer.setInterval(30);
    connect(&m_fadeTimer, &QTimer::timeout, this, [this] {
        if (m_ghostAlpha <= 0.0) {
            m_fadeTimer.stop();
            m_ghost = QImage();
            return;
        }
        m_ghostAlpha -= 0.06;
        update();
    });
}

void CrtOverlay::warmUp()
{
    m_warm = 1.0;
    m_warmTimer.start();
    m_noiseTimer.start();
}

void CrtOverlay::stop()
{
    m_warm = 0.0;
    m_exciteAge = 0.0;
    m_warmTimer.stop();
    m_noiseTimer.stop();
}

// 磷粉激发：新敲入字符所在矩形（视口坐标）
void CrtOverlay::excite(const QRect &viewportRect)
{
    if (viewportRect.isNull())
        return;
    m_exciteRect = viewportRect;
    m_exciteAge = 1.0;
    update();
}

// 内容/缩放变化：标记脏，等间隔过后重拍
void CrtOverlay::invalidateGlow()
{
    m_dirty = true;
    update();
}

// 缩放等场景：下一次绘制立即重拍
void CrtOverlay::forceGlow()
{
    m_forceRefresh = true;
    m_dirty = true;
    update();
}

void CrtOverlay::refreshGlow()
{
    if (!m_forceRefresh && m_sinceRefresh.isValid()
        && m_sinceRefresh.elapsed() < Crt::kGlowMinIntervalMs) {
        m_dirty = true; // 打字连发时别每键重拍：漏掉的内容由脏标记兜底
        return;
    }
    m_forceRefresh = false;
    QWidget *vp = m_editor->viewport();
    if (!vp || vp->width() <= 0 || vp->height() <= 0)
        return;
    // 文字快照由 Editor 自绘（真机的视口 re-render 不可靠：透明架构下
    // render/grab 全空；块走查几何与落点自检同一模型，引擎无关）
    QImage snap(vp->size(), QImage::Format_ARGB32);
    snap.fill(Qt::transparent);
    m_editor->paintTextSnapshot(snap);
    m_snap = snap;
    // 真高斯辉光：半分辨率三轮盒式模糊（真高斯形状；降采样保持宽光晕）
    const QSize half(qMax(1, vp->width() / 2), qMax(1, vp->height() / 2));
    QImage glow = Crt::gaussianBlur(
                      snap.scaled(half, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
                      4, 3)
                      .scaled(vp->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // 磷粉余晖：同一滚动位置下旧帧 15% 混入；位置变化时晋升为屏幕固定
    // 的残影（渐暗熄灭，不跟着内容跑）
    const QPointF shift = m_editor->crtGlowShift();
    if (!m_glow.isNull() && shift.manhattanLength() <= 0.5) {
        QPainter pg(&glow);
        pg.setOpacity(0.15);
        pg.drawImage(0, 0, m_glow);
    } else if (!m_glow.isNull()) {
        m_ghost = m_glow;
        m_ghostPos = QPointF(vp->pos()) - shift;
        m_ghostAlpha = qMin(0.5, m_ghostAlpha + 0.45);
        m_fadeTimer.start();
    }
    m_glow = glow;
    m_glowScroll = QPoint(m_editor->horizontalScrollBar()->value(),
                          m_editor->verticalScrollBar()->value());
    // 真衍射：竖直亮边 R 右 / B 左。着色用纯字节写入（合成模式在真机
    // 引擎上产生未初始化通道垃圾：黑轮廓/花屏绿块的元凶）
    const auto tintEdge = [](const QImage &mask, const QColor &c) {
        QImage out(mask.size(), QImage::Format_ARGB32);
        for (int y = 0; y < mask.height(); ++y) {
            const uchar *m = mask.constScanLine(y);
            uchar *o = out.scanLine(y);
            for (int x = 0; x < mask.width(); ++x) {
                o[x * 4 + 0] = uchar(c.blue());
                o[x * 4 + 1] = uchar(c.green());
                o[x * 4 + 2] = uchar(c.red());
                o[x * 4 + 3] = uchar(m[x] * c.alpha() / 255);
            }
        }
        return out;
    };
    m_edgeR = tintEdge(Crt::edgeDiff(snap, +1), QColor(255, 70, 20, int(255 * Crt::kDiffAlpha)));
    m_edgeB = tintEdge(Crt::edgeDiff(snap, -1), QColor(50, 90, 255, int(255 * Crt::kDiffAlpha)));
    m_sinceRefresh.restart();
    m_dirty = false;
    update();
}

void CrtOverlay::paintEvent(QPaintEvent *)
{
    QWidget *par = parentWidget();
    if (par && size() != par->size())
        setGeometry(par->rect());
    QPainter p(this);
    if (!p.isActive())
        return;
    QWidget *vp = m_editor->viewport();
    if (!vp)
        return;
    // 辉光刷新判定（像素位移）
    if (m_glow.isNull()) {
        refreshGlow();
    } else {
        const QPointF shift = m_editor->crtGlowShift();
        if (m_forceRefresh || shift.manhattanLength() >= Crt::kGlowMinScroll)
            refreshGlow();
        else if (m_dirty && m_sinceRefresh.elapsed() > Crt::kGlowMinIntervalMs)
            refreshGlow();
    }
    // 残影：屏幕固定、渐暗熄灭
    if (!m_ghost.isNull() && m_ghostAlpha > 0.01) {
        p.setOpacity(m_ghostAlpha);
        p.drawImage(m_ghostPos, m_ghost);
        p.setOpacity(1.0);
    }
    // 辉光（文字上方的日冕：模糊快照叠在字形上，柔化+发光同源）
    if (!m_glow.isNull()) {
        p.setClipRect(vp->geometry());
        p.setOpacity(0.5);
        p.drawImage(QPointF(vp->pos()) - m_editor->crtGlowShift(), m_glow);
        p.setOpacity(1.0);
        // 真衍射彩边（随同一像素位移）
        if (!m_edgeR.isNull())
            p.drawImage(QPointF(vp->pos()) - m_editor->crtGlowShift(), m_edgeR);
        if (!m_edgeB.isNull())
            p.drawImage(QPointF(vp->pos()) - m_editor->crtGlowShift(), m_edgeB);
        p.setClipping(false);
    }
    // 磷粉激发：单个椭圆径向渐变（柔和光斑，无轮廓）
    if (m_exciteAge > 0.02 && !m_exciteRect.isNull()) {
        const QRectF r = QRectF(QPointF(vp->pos()) + m_exciteRect.topLeft(),
                                m_exciteRect.size())
                             .adjusted(-8, -8, 8, 8);
        QRadialGradient g(r.center(), qMax(r.width(), r.height()));
        g.setColorAt(0.0, QColor(255, 176, 0, int(64 * m_exciteAge)));
        g.setColorAt(1.0, QColor(255, 176, 0, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(r);
    }
    // 暗角 + 反光 + 扫描线（烘进同一层，一次 blit）
    if (m_glass.size() != size())
        rebuildGlass();
    if (!m_glass.isNull())
        p.drawImage(0, 0, m_glass);
    // 行亮度抖动（两帧交替：扫描线起伏，机器活着；1:1 全尺寸，无缩放）
    p.setOpacity(0.55);
    if (!m_noise[m_noiseFrame].isNull())
        p.drawImage(0, 0, m_noise[m_noiseFrame]);
    p.setOpacity(1.0);
    // 滚动刷新带：3px 暗带 3 秒自上而下扫过（老式扫描的"呼吸"）
    const int bandY = int(m_bandPhase * height());
    p.fillRect(QRect(0, bandY, width(), 3), QColor(0, 0, 0, 14));
    // 暖机：黑幕由暗到亮
    if (m_warm > 0.0)
        p.fillRect(rect(), QColor(0, 0, 0, int(235 * m_warm)));
}

void CrtOverlay::rebuildGlass()
{
    if (width() <= 0 || height() <= 0)
        return;
    m_glass = QImage(size(), QImage::Format_ARGB32);
    m_glass.fill(Qt::transparent);
    // 噪声帧随尺寸重建（每行一个随机暗度，固定种子 = 同一台机器）
    for (int f = 0; f < 2; ++f) {
        m_noise[f] = QImage(size(), QImage::Format_ARGB32);
        m_noise[f].fill(Qt::transparent);
        std::mt19937 rng(0x5C4E00u + f);
        for (int y = 0; y < m_noise[f].height(); ++y) {
            uchar *row = m_noise[f].scanLine(y);
            const int a = int(rng() % 19);
            for (int x = 0; x < m_noise[f].width(); ++x) {
                row[x * 4 + 0] = 0;
                row[x * 4 + 1] = 0;
                row[x * 4 + 2] = 0;
                row[x * 4 + 3] = uchar(a);
            }
        }
    }
    QPainter p(&m_glass);
    // 管面中央微暖亮（磷粉底光从中心散开）
    QRadialGradient warm(rect().center(), qMax(width(), height()) * 0.62);
    warm.setColorAt(0.0, QColor(255, 176, 0, 14));
    warm.setColorAt(0.75, QColor(255, 176, 0, 4));
    warm.setColorAt(1.0, QColor(255, 176, 0, 0));
    p.fillRect(rect(), warm);
    QRadialGradient g(rect().center(), qMax(width(), height()) * 0.75);
    g.setColorAt(0.55, QColor(0, 0, 0, 0));
    g.setColorAt(1.0, QColor(0, 0, 0, 70));
    p.fillRect(rect(), g);
    QLinearGradient sheen(rect().topLeft(), rect().bottomRight());
    sheen.setColorAt(0.42, QColor(255, 255, 255, 0));
    sheen.setColorAt(0.5, QColor(255, 255, 255, 12));
    sheen.setColorAt(0.58, QColor(255, 255, 255, 0));
    p.fillRect(rect(), sheen);
    // 扫描线烘进本层：重绘时省掉一次平铺
    p.drawTiledPixmap(rect(), QPixmap::fromImage(m_scanMask));
}
