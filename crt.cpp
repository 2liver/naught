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

void CrtBackdrop::refreshGlow()
{
    if (m_sinceRefresh.isValid()
        && m_sinceRefresh.elapsed() < Crt::kGlowMinIntervalMs) {
        m_dirty = true; // 打字连发时别每键重拍：漏掉的内容由脏标记兜底
        return;
    }
    QWidget *vp = m_editor->viewport();
    if (!vp || vp->width() <= 0 || vp->height() <= 0)
        return;
    QImage snap(vp->size(), QImage::Format_ARGB32);
    snap.fill(Qt::transparent);
    vp->render(&snap);
    const QSize small(qMax(1, vp->width() / 4), qMax(1, vp->height() / 4));
    m_glow = snap.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                 .scaled(vp->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_glowScroll = QPoint(m_editor->horizontalScrollBar()->value(),
                          m_editor->verticalScrollBar()->value());
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
        if (delta.manhattanLength() >= Crt::kGlowMinScroll)
            refreshGlow();
        else if (m_dirty && m_sinceRefresh.elapsed() > Crt::kGlowMinIntervalMs)
            refreshGlow();
    }
    if (!m_glow.isNull()) {
        // 快照对齐当前滚动（脏快照也照画：差一两个字的辉光不可见）
        const QPoint cur(m_editor->horizontalScrollBar()->value(),
                         m_editor->verticalScrollBar()->value());
        p.setOpacity(0.9);
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

    // 噪声两帧（128×128 稀疏亮点，固定种子 = 每次开机同一台机器）
    for (int f = 0; f < 2; ++f) {
        m_noise[f] = QImage(128, 128, QImage::Format_ARGB32);
        m_noise[f].fill(QColor(0, 0, 0, 0));
        std::mt19937 rng(0xC0FFEEu + f);
        for (int i = 0; i < 700; ++i) {
            const int x = int(rng() % 128);
            const int y = int(rng() % 128);
            const int a = 26 + int(rng() % 58);
            m_noise[f].setPixelColor(x, y, QColor(255, 255, 255, a));
        }
    }
    m_noiseTimer.setInterval(120);
    connect(&m_noiseTimer, &QTimer::timeout, this, [this] {
        m_noiseFrame = 1 - m_noiseFrame;
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
    // 暗角 + 反光 + 扫描线（烘进同一层，一次 blit）
    if (m_glass.size() != size())
        rebuildGlass();
    if (!m_glass.isNull())
        p.drawImage(0, 0, m_glass);
    // 噪声（两帧交替，机器活着）
    p.setOpacity(0.5);
    p.drawTiledPixmap(rect(), QPixmap::fromImage(m_noise[m_noiseFrame]));
    p.setOpacity(1.0);
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
