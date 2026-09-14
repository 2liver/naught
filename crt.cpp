// crt.cpp —— 「显」显像管层实现。全部效果同生命周期，随关闭零残留。
#include "crt.h"

#include "editor.h"

#include <random>

#include <QElapsedTimer>
#include <QPainter>
#include <QScrollBar>

// ---------- CrtBackdrop ----------

CrtBackdrop::CrtBackdrop(Editor *editor)
    : QWidget(editor)
    , m_editor(editor)
{
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
}

// 内容/缩放变化：标记脏，等间隔过后重拍
void CrtBackdrop::invalidateGlow()
{
    m_dirty = true;
    update();
}

// 缩放等场景：下一次绘制立即重拍（光晕必须与当前字号严格一致，
// 否则旧字号的光晕会残留在新文字之外——"影子"）
void CrtBackdrop::forceGlow()
{
    m_forceRefresh = true;
    m_dirty = true;
    update();
}

void CrtBackdrop::refreshGlow()
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
    QImage snap(vp->size(), QImage::Format_ARGB32);
    snap.fill(Qt::transparent);
    vp->render(&snap);
    const QSize small(qMax(1, vp->width() / 4), qMax(1, vp->height() / 4));
    QImage glow = snap.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                      .scaled(vp->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // 磷粉余晖：同一滚动位置下，旧帧 15% 混入（打字/编辑留下短暂残影）；
    // 滚动位置变化时不混，避免错位鬼影
    const QPoint cur(m_editor->horizontalScrollBar()->value(),
                     m_editor->verticalScrollBar()->value());
    if (!m_glow.isNull() && cur == m_glowScroll) {
        QPainter pg(&glow);
        pg.setOpacity(0.15);
        pg.drawImage(0, 0, m_glow);
    }
    m_glow = glow;
    m_glowScroll = cur;
    m_sinceRefresh.restart();
    m_dirty = false;
    update();
}

void CrtBackdrop::paintEvent(QPaintEvent *)
{
    QWidget *par = parentWidget();
    if (par && size() != par->size())
        setGeometry(par->rect());
    QPainter p(this);
    if (!p.isActive())
        return;
    p.fillRect(rect(), Crt::kBg);
    QWidget *vp = m_editor->viewport();
    if (!vp)
        return;
    if (m_glow.isNull()) {
        refreshGlow();
    } else {
        const QPoint cur(m_editor->horizontalScrollBar()->value(),
                         m_editor->verticalScrollBar()->value());
        const QPoint delta = cur - m_glowScroll;
        if (m_forceRefresh || delta.manhattanLength() >= Crt::kGlowMinScroll)
            refreshGlow();
        else if (m_dirty && m_sinceRefresh.elapsed() > Crt::kGlowMinIntervalMs)
            refreshGlow();
    }
    if (!m_glow.isNull()) {
        // 环境底光：弱化的快照垫底（日冕由效果层 Plus 叠加在文字上方）
        const QPoint cur(m_editor->horizontalScrollBar()->value(),
                         m_editor->verticalScrollBar()->value());
        p.setOpacity(0.35);
        p.drawImage(QPointF(vp->pos()) - QPointF(cur - m_glowScroll), m_glow);
    }
}

// ---------- CrtOverlay ----------

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

    // 噪声两帧：1×256 的行亮度抖动条（真实 CRT 的噪声是扫描线明暗起伏，
    // 不是撒白点）；拉伸到全屏 = 逐行 ±几级灰度，固定种子 = 同一台机器
    for (int f = 0; f < 2; ++f) {
        m_noise[f] = QImage(1, 256, QImage::Format_ARGB32);
        m_noise[f].fill(Qt::transparent);
        std::mt19937 rng(0x5C4E00u + f);
        for (int y = 0; y < 256; ++y)
            m_noise[f].setPixelColor(0, y, QColor(0, 0, 0, int(rng() % 19)));
    }

    m_noiseTimer.setInterval(120);
    connect(&m_noiseTimer, &QTimer::timeout, this, [this] {
        m_noiseFrame = 1 - m_noiseFrame;
        m_bandPhase += 0.04; // 120ms / 3000ms：刷新带 3 秒扫一周
        if (m_bandPhase >= 1.0)
            m_bandPhase -= 1.0;
        update();
    });
    m_warmTimer.setInterval(24);
    connect(&m_warmTimer, &QTimer::timeout, this, [this] {
        m_warm = qMax(0.0, m_warm - 0.055);
        update();
        if (m_warm <= 0.0)
            m_warmTimer.stop();
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
    m_warmTimer.stop();
    m_noiseTimer.stop();
}

void CrtOverlay::paintEvent(QPaintEvent *)
{
    QWidget *par = parentWidget();
    if (par && size() != par->size())
        setGeometry(par->rect());
    QPainter p(this);
    if (!p.isActive())
        return;
    // 磷粉日冕：模糊快照叠在文字上方——表面与光晕同源，锐利的字获得
    // 真实管子的光晕包络。磷底近黑，alpha 混合与加法混合视觉等价但快数倍
    const QImage glow = m_editor->crtGlowImage();
    if (!glow.isNull()) {
        const QPoint cur(m_editor->horizontalScrollBar()->value(),
                         m_editor->verticalScrollBar()->value());
        const QPointF at = QPointF(m_editor->viewport()->pos())
            - QPointF(cur - m_editor->crtGlowScroll());
        // 裁剪到视口：滚动滞后时旧快照的边缘不许溢出到行号区/边界之外
        // （否则文字外面会残留一圈影子）
        p.save();
        p.setClipRect(m_editor->viewport()->geometry());
        p.setOpacity(0.42);
        p.drawImage(at, glow);
        p.restore();
        p.setOpacity(1.0);
    }
    // 暗角 + 反光 + 扫描线（烘进同一层，一次 blit）
    if (m_glass.size() != size())
        rebuildGlass();
    if (!m_glass.isNull())
        p.drawImage(0, 0, m_glass);
    // 行亮度抖动（两帧交替：扫描线起伏，机器活着）
    p.setOpacity(0.55);
    p.drawImage(rect(), m_noise[m_noiseFrame]);
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
