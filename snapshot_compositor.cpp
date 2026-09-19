// snapshot_compositor.cpp —— 快照合成器实现（自 editor.h 迁出）。
// 全部逻辑与 editor.h 中原文一致，仅把 this 换为 m_e（friend 访问）。
#include "snapshot_compositor.h"

#include "editor.h"
#include "zen_scroll_bar.h"

#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>

SnapshotCompositor::SnapshotCompositor(Editor &editor)
    : m_e(editor)
{
}

void SnapshotCompositor::paint(QImage &img) const
{
    // 块状反相光标：只在显模式、有焦点、眨眼"亮"拍时画（休眠 = 隐去，
    // 与原生光标同一节拍；见 syncNativeCaretWidth）。
    const bool cursorBlock = m_e.m_crt && m_e.hasFocus()
        && (m_e.textCursor().hasSelection() // 选区期间常亮（见 cursorVisible 注释）
            || (m_e.m_blinkTimer.isActive() && m_e.m_blinkHalf % 2 == 0));
    {
        QPainter p(&img);
        if (!p.isActive())
            return;
        p.fillRect(img.rect(), m_e.crtPalette().bg); // 磷光底：行号列条/滚动条槽也同色
        // DPR：调用方把图像按物理像素建好并 setDevicePixelRatio(dpr)。
        // Qt 6.8+ 的 QImage 画笔会在引擎层自动应用图像 DPR（经验证：
        // 有效缩放 = 画笔变换 × 图像 DPR），所以这里绝不能手动再
        // p.scale(dpr)——二次相乘会把内容放大推出画面（右侧滚动条把手
        // 完全消失、文字只剩左上象限的根源），逻辑坐标交给引擎映射。
        // 强制文档布局就绪：QPlainTextEdit 的布局是惰性的——帧重拍
        // 可能赶在布局完成前，视口渲染为空（快照失字、"黑屏一会"的
        // 残因）。documentSize() 触发布局计算后再渲染视口
        m_e.document()->documentLayout()->documentSize();
        if (m_e.viewport())
            m_e.viewport()->render(&p, m_e.viewport()->pos());
        paintExcitation(p); // 磷粉激发：新字符在文字之上加色增亮（900ms 内）
        if (m_e.m_canvas && m_e.m_canvas->isVisible())
            m_e.m_canvas->render(&p, m_e.m_canvas->pos());
        // 行号区：编模式下真实组件已隐藏（防双层），合成仍渲染它——
        // 行号只存在于光栅内；非编模式不渲染（残留的隐藏组件会在
        // 旧位置叠在字上）
        if (m_e.m_codeMode && m_e.m_lineNumberArea)
            m_e.m_lineNumberArea->render(&p, m_e.m_lineNumberArea->pos());
        if (m_e.m_fadeOpacity > 0.02) {
            p.save();
            p.setOpacity(m_e.m_fadeOpacity);
            // 滚动条直接画把手几何：不走 QWidget::render。Qt 6.9 起滚动条
            // 住在 QAbstractScrollArea 的私有容器 QWidget 里，pos() 只是
            // 容器内坐标（恒 (0,0)）——按 pos() 平移 = 把手画到窗口左缘
            // （"左侧镜像滚动条"）。必须 mapTo 换算到编辑器坐标；
            // 隐藏的滚动条不画（防隐藏条的把手残留在窗口左上角）。
            if (auto *v = qobject_cast<ZenScrollBar *>(m_e.verticalScrollBar()))
                if (v->isVisible())
                    v->paintOnto(p, v->mapTo(&m_e, QPoint(0, 0)));
            if (auto *h = qobject_cast<ZenScrollBar *>(m_e.horizontalScrollBar()))
                if (h->isVisible())
                    h->paintOnto(p, h->mapTo(&m_e, QPoint(0, 0)));
            p.restore();
        }
    } // 画家析构后直接回写像素，避免与光栅引擎缓存交错
    // 光标回快照（审查结论：顶层叠加在已辉光帧上反相 → 光晕环窄线 +
    // 阈值失准；快照内反相经辉光整体调制 = 稳定版观感）。残影改由
    // persist 着色器的"光标区不进历史"掩膜根治
    if (cursorBlock)
        paintCursor(img);
}

void SnapshotCompositor::paintRegion(QImage &img, const QRect &dirty) const
{
    if (dirty.isEmpty())
        return;
    const bool cursorBlock = m_e.m_crt && m_e.hasFocus()
        && (m_e.textCursor().hasSelection() // 选区期间常亮（见 cursorVisible 注释）
            || (m_e.m_blinkTimer.isActive() && m_e.m_blinkHalf % 2 == 0));
    {
        QPainter p(&img);
        if (!p.isActive())
            return;
        p.setClipRect(dirty);
        p.fillRect(dirty, m_e.crtPalette().bg); // 磷光底：先清脏区
        // 渲染整个视口、由画家 clip 限范围：source-region 语义在
        // DPR 引擎下与 clip 的坐标映射存在偏差（实测边缘像素漏画）
        if (m_e.viewport())
            m_e.viewport()->render(&p, m_e.viewport()->pos());
        paintExcitation(p); // clip 限范围：只重画脏区内的激发
        if (m_e.m_canvas && m_e.m_canvas->isVisible())
            m_e.m_canvas->render(&p, m_e.m_canvas->pos());
        if (m_e.m_codeMode && m_e.m_lineNumberArea)
            m_e.m_lineNumberArea->render(&p, m_e.m_lineNumberArea->pos());
        if (m_e.m_fadeOpacity > 0.02) {
            p.save();
            p.setOpacity(m_e.m_fadeOpacity);
            if (auto *v = qobject_cast<ZenScrollBar *>(m_e.verticalScrollBar()))
                if (v->isVisible())
                    v->paintOnto(p, v->mapTo(&m_e, QPoint(0, 0)));
            if (auto *h = qobject_cast<ZenScrollBar *>(m_e.horizontalScrollBar()))
                if (h->isVisible())
                    h->paintOnto(p, h->mapTo(&m_e, QPoint(0, 0)));
            p.restore();
        }
    }
    if (cursorBlock)
        paintCursor(img); // 光标回快照（掩膜防残影，见 paint()）
}

QRect SnapshotCompositor::computeDirty(int from, int removed, int added) const
{
    const int cc = m_e.document()->characterCount();
    QRect dirty;
    QTextBlock blk = m_e.document()->findBlock(qMin(from, qMax(0, cc - 1)));
    const QTextBlock endBlk = m_e.document()->findBlock(
        qMin(from + qMax(added, removed), qMax(0, cc - 1)));
    QRect firstR;
    for (;;) {
        QRect r = m_e.blockBoundingGeometryPub(blk)
                      .translated(m_e.contentOffsetPub()).toAlignedRect()
                      .translated(m_e.viewport()->pos());
        r = r.intersected(m_e.viewport()->rect().translated(m_e.viewport()->pos()));
        dirty |= r;
        if (firstR.isNull() && !r.isNull())
            firstR = r;
        if (blk == endBlk)
            break;
        blk = blk.next();
    }
    // 下方至视口底全宽纳入：任何编辑（尤其删除/换行）都会让下方整体位移
    if (!firstR.isNull()) {
        QRect below = m_e.viewport()->rect().translated(m_e.viewport()->pos());
        below.setTop(firstR.top());
        dirty |= below;
    }
    // 行号槽补全（用户报：⌘B 换行后的行号不显示，得再换一行前一行的
    // 才出现）：块几何与"下方至底"都以视口为锚（x ≥ 槽宽），行号槽
    // 本身永远不在脏区内——换行/插入后下方行号永不重绘，只有光标激发
    // 光环偶尔擦进槽内，行号才"晚一步"出现。左缘拉到 0、垂直跨度
    // 不变：行号槽随文字一起重绘（视口/画布在槽内本就无像素，无副作用）
    if (!dirty.isNull())
        dirty.setLeft(0);
    return dirty;
}

void SnapshotCompositor::paintExcitation(QPainter &p) const
{
    if (!m_e.m_exciteClock.isValid())
        return;
    const qint64 t = m_e.m_exciteClock.elapsed();
    if (t < 0 || t >= 900)
        return;
    const qreal a = 0.6 * qExp(-qreal(t) / 500.0); // 激发峰值 0.6，500ms 指数回落
    if (a < 0.02)
        return;
    const int pos = qBound(0, m_e.m_excitePos, m_e.document()->characterCount() - 1);
    QTextCursor cc = m_e.textCursor();
    cc.setPosition(pos);
    QRect cell;
    if (cc.atEnd()) {
        cell = m_e.cursorRect(cc);
        cell.setWidth(qCeil(m_e.fontMetrics().horizontalAdvance(QLatin1Char('M'))));
    } else {
        QTextCursor sel(cc);
        sel.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        cell = m_e.cursorRect(sel);
    }
    cell.translate(m_e.viewport()->pos()); // 视口坐标 → 编辑器坐标（编模式有行号槽偏移）
    if (cell.isNull())
        return;
    p.save();
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    p.setPen(Qt::NoPen);
    const Crt::Palette &pp = m_e.crtPalette();
    // 软径向渐变（用户报"字符色高光窄线"：旧版三个嵌套圆角矩形的
    // 硬边在辉光衰减期退成字符色的锐利窄线，落在光标前刚打的字旁；
    // 渐变无边 = 无窄线，且更接近磷粉的连续衰减）
    const QPointF center = cell.center();
    const qreal radius = qMax(cell.width(), cell.height()) * 0.5 + 8.0;
    QRadialGradient grad(center, radius);
    const auto glowCol = [&](qreal amp) {
        return QColor(pp.cursorBlock.red(), pp.cursorBlock.green(),
                      pp.cursorBlock.blue(), qRound(255.0 * amp));
    };
    grad.setColorAt(0.0, glowCol(a));
    grad.setColorAt(0.35, glowCol(a * 0.4));
    grad.setColorAt(0.7, glowCol(a * 0.12));
    grad.setColorAt(1.0, glowCol(0.0));
    p.setBrush(grad);
    p.drawEllipse(center, radius, radius);
    p.restore();
}

void SnapshotCompositor::paintCursor(QImage &img) const
{
    const QTextCursor c = m_e.textCursor();
    QRect cell = m_e.cursorRect(c); // 插入位（宽度为 0 的落点矩形）
    cell.translate(m_e.viewport()->pos()); // 视口坐标 → 编辑器坐标（编模式有行号槽偏移）
    if (cell.isNull())          // 零宽合法（isValid 要求宽高>0，会误拒）
        return;
    const QChar ch = m_e.document()->characterAt(c.position());
    const qreal adv = (ch.isNull() || ch == QChar::ParagraphSeparator)
                          ? m_e.fontMetrics().horizontalAdvance(QLatin1Char('M'))
                          : m_e.fontMetrics().horizontalAdvance(ch);
    cell.setWidth(qMax(1, qCeil(adv)));
    // 逻辑 → 物理像素（引擎层 DPR 映射：物理 = 逻辑 × dpr）
    const qreal dpr = img.devicePixelRatio();
    const int x0 = qFloor(cell.x() * dpr), y0 = qFloor(cell.y() * dpr);
    const int x1 = qCeil((cell.x() + cell.width()) * dpr);
    const int y1 = qCeil((cell.y() + cell.height()) * dpr);
    const int w = img.width(), h = img.height();
    // IBM PC（机型 3）：真机 BIOS 文本光标 = 下划线（单元格底缘 2~3
    // 扫描线的亮条，字形保持可见、无反相）。其余机型 = 整格反相块
    //（Osborne 的块光标 / C64 的闪烁块——charter 记录；用户四轮考据
    // 指正：并非所有机型都是块状）
    const Crt::Palette &pp = m_e.crtPalette();
    if (m_e.machine() == 3 && !m_e.cursorOnGlyph()) {
        const QColor ucol = m_e.m_codeMode ? QColor(0xE8, 0xE8, 0xE0) : pp.cursorBlock;
        const int bandH = qMax(2, qCeil(cell.height() * dpr * 0.18));
        for (int y = qMax(0, y1 - bandH); y < y1 && y < h; ++y) {
            uchar *line = img.scanLine(y);
            for (int x = qMax(0, x0); x < x1 && x < w; ++x) {
                const int i = x * 4;
                line[i + 2] = uchar(ucol.red());
                line[i + 1] = uchar(ucol.green());
                line[i] = uchar(ucol.blue());
            }
        }
        return;
    }
    // 双色反相：t = 像素亮度在 底→墨 间的归一位置；out = lerp(块, 底, t)
    // 编模式（多彩语法高亮）下块光标用中性暖白：绿磷块在代码里太突兀
    const QColor block = m_e.m_codeMode ? QColor(0xE8, 0xE8, 0xE0) : pp.cursorBlock;
    const int bgSum = pp.bg.red() + pp.bg.green() + pp.bg.blue();
    const int inkSum = pp.ink.red() + pp.ink.green() + pp.ink.blue();
    const int span = qMax(1, inkSum - bgSum); // 防御除零（底=墨时）
    // 注意：ARGB32 内存布局为 BGRA，字节直接寻址（见 edgeDiff 同款注释）。
    // 光标可能滚出视口（cursorRect 变负）——循环必须裁剪到图像内，
    // 否则 scanLine(负y) 段错误（用户"插入图片后缩放闪退"的真凶）
    for (int y = qMax(0, y0); y < y1 && y < h; ++y) {
        uchar *line = img.scanLine(y);
        for (int x = qMax(0, x0); x < x1 && x < w; ++x) {
            const int i = x * 4;
            const int sum = line[i] + line[i + 1] + line[i + 2];
            const qreal t = qBound(0.0, qreal(sum - bgSum) / qreal(span), 1.0);
            line[i + 2] = uchar(block.red() + (pp.bg.red() - block.red()) * t);
            line[i + 1] = uchar(block.green() + (pp.bg.green() - block.green()) * t);
            line[i] = uchar(block.blue() + (pp.bg.blue() - block.blue()) * t);
        }
    }
}
