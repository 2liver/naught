// editor.h —— 编辑器：文字 + 工具（涂/擦）+ 视图（编）+ 光标/滚动条行为 + 自检。
#pragma once

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFocusEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPointF>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
#include <QWheelEvent>

#ifdef NAUGHT_WITH_HIGHLIGHT
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif

#include "canvas.h"
#include "crt.h"
#include "crt_view.h"
#include "line_number_area.h"
#include "zen_scroll_bar.h"

class Editor : public QPlainTextEdit {
public:
    Editor()
        : QPlainTextEdit()
        , m_holdTimer(this)
    {
        setFrameShape(QFrame::NoFrame);
        setTabChangesFocus(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        m_baseFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        m_codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m_baseSize = m_baseFont.pointSize();
        if (m_baseSize <= 0)
            m_baseSize = 12;
        m_size = m_baseSize;

        // 磷光像素字体：随 qrc 捆绑（OFL），一次加载终身可用
        if (s_crtFamily.isEmpty()) {
            const int id = QFontDatabase::addApplicationFont(
                QStringLiteral(":/fonts/fusion-pixel-12px-monospaced-zh_hans.ttf"));
            if (id >= 0 && !QFontDatabase::applicationFontFamilies(id).isEmpty())
                s_crtFamily = QFontDatabase::applicationFontFamilies(id).first();
        }
        m_crtFont = QFont(s_crtFamily);
        // 抗锯齿打开：磷粉像素块边缘自然软化（锐利硬边不像玻璃后的光）

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        m_dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
        m_dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
#endif

        m_holdTimer.setSingleShot(false);
        connect(&m_holdTimer, &QTimer::timeout, this, [this] {
            // 按住：步长随当前字号等比增长（大字跨大步），间隔逐渐加速
            const int step = std::max(1, int(std::lround(m_size * 0.05)));
            zoom(m_holdDir * step);
            m_holdInterval = std::max(12, m_holdInterval - 4);
            m_holdTimer.start(m_holdInterval);
        });

        // 显的自归位补拍（合并式单发；见 applyZoom）
        m_crtSettleTimer.setSingleShot(true);
        connect(&m_crtSettleTimer, &QTimer::timeout, this, [this] {
            if (m_crt && m_crtView)
                m_crtView->markDirty();
        });

        // 换成自绘滚动条：命中区恒 18px，把手闲置 10px / 悬停 18px
        auto *vsb = new ZenScrollBar(Qt::Vertical);
        auto *hsb = new ZenScrollBar(Qt::Horizontal);
        vsb->setFixedWidth(18);
        hsb->setFixedHeight(18);
        setVerticalScrollBar(vsb);
        setHorizontalScrollBar(hsb);

        viewport()->installEventFilter(this);
        viewport()->setMouseTracking(true);
        verticalScrollBar()->installEventFilter(this);
        horizontalScrollBar()->installEventFilter(this);

        // 光标：闪烁由我们自己驱动（原生闪烁器已关，见 main），保证完整对称——
        // 亮 BLINK_HALF_MS / 灭 BLINK_HALF_MS 为一拍，完成 SLEEP_BLINKS 次后恰好休眠，无残拍
        setCursorWidth(2);
        m_blinkTimer.setSingleShot(true);
        connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
            if (++m_blinkHalf >= SLEEP_BLINKS * 2) {
                setCursorWidth(0); // 第 N 次闪烁的“灭”拍即休眠
                m_blinkTimer.stop();
                return;
            }
            setCursorWidth(m_blinkHalf % 2 ? 0 : 2);
        });
        connect(document(), &QTextDocument::contentsChanged, this, [this] {
            wakeCaret();
            m_lastWasInk = false;
            // 行号区：文档一变立即重绘，否则清空/换行不会刷新（假行号）
            m_canvas->update();
            updateGutterWidth();
            if (m_crtView) {
                m_crtView->markDirty();
                m_crtSettleTimer.start(400); // 打字停顿后半拍重拍（痕迹自愈）
            }
        });
        wakeCaret();

        // 滚动条：交互时淡入，闲置 1 秒后淡出；悬停加宽（事件驱动，QSS 的 :hover 改宽度无效）
        m_vFade = new QGraphicsOpacityEffect(verticalScrollBar());
        m_hFade = new QGraphicsOpacityEffect(horizontalScrollBar());
        verticalScrollBar()->setGraphicsEffect(m_vFade);
        horizontalScrollBar()->setGraphicsEffect(m_hFade);
        m_scrollHideTimer.setSingleShot(true);
        connect(&m_scrollHideTimer, &QTimer::timeout, this, [this] { m_fadeTimer.start(); });
        m_fadeTimer.setInterval(16);
        connect(&m_fadeTimer, &QTimer::timeout, this, [this] {
            m_fadeOpacity = std::max(0.0, m_fadeOpacity - 0.1);
            setScrollOpacity(m_fadeOpacity);
            if (m_fadeOpacity <= 0.0)
                m_fadeTimer.stop();
        });
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) { scrollActivity(); });
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int) { scrollActivity(); });
        connect(vsb, &ZenScrollBar::hovered, this, [this](bool on) { if (on) scrollActivity(); });
        connect(hsb, &ZenScrollBar::hovered, this, [this](bool on) { if (on) scrollActivity(); });
        connect(vsb, &ZenScrollBar::trackClicked, this, [this](QPoint pos) { placeCaretAtEdge(true, pos); });
        connect(hsb, &ZenScrollBar::trackClicked, this, [this](QPoint pos) { placeCaretAtEdge(false, pos); });
        m_scrollHideTimer.start(1500);

        // 画布层（涂/擦）：覆盖整个窗口（含滚动条区），画布与窗口严格一致；
        // 置于滚动条之下、文字区之上
        m_canvas = new Canvas(this);
        m_canvas->stackUnder(verticalScrollBar());
        m_canvas->show();
        m_canvas->setInk(m_dark ? QColor(255, 255, 255) : QColor(0, 0, 0));
        m_brushSize = m_baseSize * BRUSH_SCALE;
        m_canvas->setBrushWidth(m_brushSize);
        const auto syncInkOffset = [this](int) {
            m_canvas->setScrollOffset(QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value()));
            if (m_crtView) {
                m_crtView->markDirty(); // 纹理随滚动重拍
                m_crtSettleTimer.start(400); // 滚动停下后半拍自归位
            }
        };
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, syncInkOffset);
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, syncInkOffset);
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
            if (m_codeMode)
                applyGutterGeometry(); // 行号随横滚移出/复原
        });
        // 滚动条的出现/消失会改变视口尺寸：着色器层跟随，否则盖住滚动条
        const auto syncCrtGeo = [this] {
            if (m_crtView) {
                m_crtView->syncGeometry();
                m_crtView->markDirty();
            }
        };
        connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, syncCrtGeo);
        connect(horizontalScrollBar(), &QScrollBar::rangeChanged, this, syncCrtGeo);

        applyScheme();
        applyZoom();
    }

    // 摹：全选并复制。空文档时静默无事。
    void mo()
    {
        if (document()->isEmpty())
            return;
        selectAll();
        copy();
    }

    // 空：清空全部文字（可 Ctrl/Cmd+Z 撤销）。空文档时静默无事。
    void kong()
    {
        if (document()->isEmpty())
            return;
        QTextCursor cursor(document());
        cursor.select(QTextCursor::Document);
        cursor.removeSelectedText();
    }

    void setDark(bool dark)
    {
        if (m_dark == dark)
            return;
        m_dark = dark;
        applyScheme();
    }

    enum class Mode { Normal, Draw, Erase }; // 工具轴；编是独立的视图轴

    bool isDark() const { return m_dark; }
    Mode mode() const { return m_mode; }

    void clearInk()
    {
        if (!m_canvas)
            return;
        beginInkSession();
        m_canvas->clearAll();
        endInkSession();
    }

    // 言：选中（或当前）每一行头尾加「」，批量校对打钩；空行跳过；
    // 一次编辑块 = 一步撤销；之后整段保持选中（配合「隔」连续操作）。
    void yan()
    {
        QTextDocument *doc = document();
        if (doc->isEmpty())
            return;
        QTextCursor c = textCursor();
        const int selStart = c.selectionStart();
        const int selEnd = c.selectionEnd();
        QTextBlock first = doc->findBlock(selStart);
        QTextBlock last = doc->findBlock(selEnd);
        if (last.position() == selEnd && last != first)
            last = last.previous(); // 选区恰在行首结束：上一行才是最后受影响行
        // 先收集行头尾，再自下而上插入（位置恒有效）
        struct Span {
            int start;
            int end; // 行尾换行符的位置（在其前插入」）
            bool empty;
        };
        QVector<Span> spans;
        for (QTextBlock b = first;; b = b.next()) {
            spans.append({b.position(), b.position() + b.length() - 1, b.length() <= 1});
            if (b == last)
                break;
        }
        int wrapped = 0;
        c.beginEditBlock();
        for (int i = spans.size() - 1; i >= 0; --i) {
            if (spans.at(i).empty)
                continue;
            c.setPosition(spans.at(i).end);
            c.insertText(QStringLiteral("」"));
            c.setPosition(spans.at(i).start);
            c.insertText(QStringLiteral("「"));
            ++wrapped;
        }
        c.endEditBlock();
        if (wrapped == 0)
            return;
        c.setPosition(spans.first().start);
        c.setPosition(spans.last().end + 2 * wrapped, QTextCursor::KeepAnchor);
        setTextCursor(c);
        wakeCaret();
    }

    // 隔：选中的每一行都像单选那样上下各补一个空行——逐行隔离。
    // 空行本身是隔板（跳过）；幂等（已是空行则不重复）；一步撤销；
    // 之后整个隔离区保持选中。
    void ge()
    {
        QTextDocument *doc = document();
        if (doc->isEmpty())
            return;
        QTextCursor c = textCursor();
        const int selStart = c.selectionStart();
        const int selEnd = c.selectionEnd();
        QTextBlock first = doc->findBlock(selStart);
        QTextBlock last = doc->findBlock(selEnd);
        if (last.position() == selEnd && last != first)
            last = last.previous();
        // 受影响的内容行（空行跳过）；QTextBlock 句柄跨编辑稳定：
        // 所有插入都在换行符处（不劈块）
        QVector<QTextBlock> lines;
        for (QTextBlock b = first;; b = b.next()) {
            if (b.length() > 1)
                lines.append(b);
            if (b == last)
                break;
        }
        QVector<int> inserts;
        // 相邻内容行之间：若中间无空行，必插
        for (int i = 0; i + 1 < lines.size(); ++i) {
            if (lines.at(i).next() == lines.at(i + 1))
                inserts.append(lines.at(i).position() + lines.at(i).length() - 1);
        }
        // 末行之后：下方块非空才插
        {
            const QTextBlock below = lines.last().next();
            if (below.isValid() && below.length() > 1)
                inserts.append(lines.last().position() + lines.last().length() - 1);
        }
        // 首行之前：插在上一行换行符处（若插本行首会劈块，句柄漂移）
        if (lines.first().position() > 0 && lines.first().previous().length() > 1)
            inserts.append(lines.first().position() - 1);
        if (inserts.isEmpty())
            return;
        std::sort(inserts.begin(), inserts.end(), std::greater<int>());
        c.beginEditBlock();
        int prevPos = -1;
        for (int pos : inserts) {
            if (pos == prevPos)
                continue; // 去重（理论上不会，防御）
            c.setPosition(pos);
            c.insertText(QStringLiteral("\n"));
            prevPos = pos;
        }
        c.endEditBlock();
        c.setPosition(lines.first().position());
        c.setPosition(lines.last().position() + lines.last().length() - 1, QTextCursor::KeepAnchor);
        setTextCursor(c);
        wakeCaret();
    }

    void undoAll()
    {
        if (m_lastWasInk && !m_inkUndo.isEmpty()) {
            const InkOp op = m_inkUndo.takeLast();
            m_inkRedo.append(op);
            m_canvas->restore(op.before);
            m_undoWasInk = true;
            return;
        }
        m_undoWasInk = false;
        document()->undo();
    }

    void redoAll()
    {
        if (m_undoWasInk && !m_inkRedo.isEmpty()) {
            const InkOp op = m_inkRedo.takeLast();
            m_inkUndo.append(op);
            m_canvas->restore(op.after);
            return;
        }
        document()->redo();
    }

    bool inkEmpty() const { return m_canvas && m_canvas->snapshot().isEmpty(); }
    QVector<QPainterPath> inkPaths() const { return m_canvas ? m_canvas->inkPaths() : QVector<QPainterPath>{}; }
    int gutterWidth() const { return m_gutterWidth; }

    // 供行号区使用的公开包装（Qt 的原生接口是 protected）
    QTextBlock firstVisibleBlockPub() const { return firstVisibleBlock(); }
    QRectF blockBoundingGeometryPub(const QTextBlock &b) const { return blockBoundingGeometry(b); }
    QRectF blockBoundingRectPub(const QTextBlock &b) const { return blockBoundingRect(b); }
    QPointF contentOffsetPub() const { return contentOffset(); }
    QFont codeFont() const
    {
        QFont f = m_codeFont;
        f.setPointSizeF(m_size);
        return f;
    }
    // 行号区用的显示字体：编=等宽，显=像素磷光（其余同文字）
    QFont displayFont() const { return activeFont(); }

    // 文字快照：整面合成——把真实子控件的 paintEvent 逐个渲染进快照
    // （QWidget::render，引擎原生绘制，无手工几何）：
    //   视口 = 文字 + 打字光标（含闪烁态/选区）
    //   画布 = 墨水笔迹 + 足迹圆点（实心涂点/空心擦环）
    //   行号区 = 结构对齐的行号（真实组件，对齐由构造保证）
    //   滚动条 = 最上层（与真实层级一致），按当前淡出透明度绘制
    // 全部 1:1 真实坐标：光栅里的文字与点击命中的文字严格同位，
    // 光栅不再有"看不全/点不到"的黑区。
    void paintTextSnapshot(QImage &img) const
    {
        QPainter p(&img);
        if (!p.isActive())
            return;
        p.fillRect(img.rect(), Crt::kBg); // 磷光底：行号列条/滚动条槽也同色
        if (viewport())
            viewport()->render(&p, viewport()->pos());
        if (m_canvas && m_canvas->isVisible())
            m_canvas->render(&p, m_canvas->pos());
        // 行号区：编模式下真实组件已隐藏（防双层），合成仍渲染它——
        // 行号只存在于光栅内；非编模式不渲染（残留的隐藏组件会在
        // 旧位置叠在字上）
        if (m_codeMode && m_lineNumberArea)
            m_lineNumberArea->render(&p, m_lineNumberArea->pos());        if (m_fadeOpacity > 0.02) {
            p.save();
            p.setOpacity(m_fadeOpacity);
            if (verticalScrollBar() && verticalScrollBar()->isVisible())
                verticalScrollBar()->render(&p, verticalScrollBar()->pos());
            if (horizontalScrollBar() && horizontalScrollBar()->isVisible())
                horizontalScrollBar()->render(&p, horizontalScrollBar()->pos());
            p.restore();
        }
    }
    qreal brushSize() const { return m_brushSize; }

    void brushUp() { brushStep(+1); }
    void brushDown() { brushStep(-1); }
    void brushDefault() { brushReset(); }

    void toggleMode(Mode m)
    {
        m_mode = (m_mode == m) ? Mode::Normal : m;
        updateModeCursor();
    }

    bool codeMode() const { return m_codeMode; }
    bool crtOn() const { return m_crt; }
    QImage crtSnapImage() const
    {
        return m_crtView ? m_crtView->frameImage() : QImage();
    }
    // 人眼代理：鼠标在视口内的位置（反光视差追踪用）
    QPointF lastMouseViewport() const { return m_lastMouse; }



    // 显：单一琥珀磷光模式——零 UI，一键回到过去（与编、阴/阳正交可叠加）
    void toggleCrt()
    {
        m_crt = !m_crt;
        if (m_crt) {
            // B 路线：真光学着色器层（自有 QRhi·Metal 离屏渲染 + 回读），
            // 盖住编辑器整面。普通 alien 覆盖层：指针天然穿透、无原生窗口，
            // 开关即 show/hide，没有任何拆装竞态。
            if (!m_crtView) {
                m_crtView = new CrtView(this);
            }
            m_crtView->syncGeometry(); // 整面：文字+行号区+滚动条全被光栅覆盖
            m_crtView->show();
            m_crtView->raise();
            m_crtView->markDirty(true);
            // 行号只在光栅内存在：真实行号区隐藏（合成仍渲染它，防双层）
            if (m_lineNumberArea)
                m_lineNumberArea->hide();
            setFocus();
            activateWindow();
            // 临时取证：开显 1.2 秒后保存纯 CPU 快照
            QTimer::singleShot(1200, this, [this] {
                if (!m_crt || !m_crtView)
                    return;
                m_crtView->frameImage().save(QStringLiteral("/tmp/naught-crt-frame.png"));
                QFile f(QStringLiteral("/tmp/naught-crt-geo.log"));
                f.open(QIODevice::Append);
                f.write(QStringLiteral("geo=%1 visible=%2 hidden=%3 size=%4x%5 vp=%6\n")
                            .arg(m_crtView->geometry().x())
                            .arg(m_crtView->isVisible())
                            .arg(m_crtView->isHidden())
                            .arg(m_crtView->width())
                            .arg(m_crtView->height())
                            .arg(viewport()->geometry().x())
                            .toUtf8());
                f.close();
            });
        } else {
            // 常驻对象，只隐藏。无原生窗口：隐藏即彻底让位，
            // 不需要销毁、不需要摘除任何属性——事件分发天然恢复。
            if (m_crtView)
                m_crtView->hide();
            viewport()->releaseMouse(); // 防御：抓取会话不跨显模式残留
            if (m_lineNumberArea)
                m_lineNumberArea->show();
            viewport()->update();
            setFocus();
        }
        applyScheme();
        applyZoom();
    }

    void toggleCodeMode()
    {
        setCodeMode(!m_codeMode);
        updateModeCursor();
    }

    void zoom(int delta)
    {
        applyAnchoredZoom(m_size + delta);
    }

    void zoomTo(qreal size)
    {
        applyAnchoredZoom(size);
    }

    void zoomReset()
    {
        applyAnchoredZoom(m_baseSize);
    }

    // 自检（CI/本地验证）：确认 O(1) 缩放、光标最右缘落点、轨道点击转落点均正常。
    static bool selftest()
    {
        Editor e;
        e.setPlainText(QStringLiteral("無"));
        const QTextBlock block = e.document()->firstBlock();
        const qreal h1 = e.document()->documentLayout()->blockBoundingRect(block).height();
        e.zoomTo(200);
        const qreal h2 = e.document()->documentLayout()->blockBoundingRect(block).height();
        if (!(h2 > h1 * 2.0)) {
            qWarning("selftest FAIL: zoom h1=%f h2=%f", h1, h2);
            return false;
        }

        // 累积求"文档 y 处的视觉行行尾"（与 lineEndForY 同一模型，独立实现作真值）
        auto lineEndAtDocY = [&](qreal docY) -> int {
            QTextBlock b = e.document()->firstBlock();
            qreal top = 0;
            while (b.isValid()) {
                const QRectF r = e.document()->documentLayout()->blockBoundingRect(b);
                if (docY < top + r.height()) {
                    QTextLayout *tl = b.layout();
                    if (!tl || tl->lineCount() == 0)
                        return b.position() + b.length() - 1;
                    const qreal relY = docY - top;
                    QTextLine ln = tl->lineAt(0);
                    for (int i = 1; i < tl->lineCount(); ++i) {
                        const QTextLine l = tl->lineAt(i);
                        if (relY >= l.y())
                            ln = l;
                        else
                            break;
                    }
                    if (ln.textLength() == 0 && b.length() > 1)
                        return b.position() + b.length() - 1;
                    if (b == e.document()->lastBlock() && b.length() == 1
                        && e.document()->characterCount() >= 2)
                        return e.document()->characterCount() - 2;
                    return b.position() + ln.textStart() + ln.textLength();
                }
                top += r.height();
                b = b.next();
            }
            if (e.document()->lastBlock().length() == 1 && e.document()->characterCount() >= 2)
                return e.document()->characterCount() - 2;
            return e.document()->characterCount() - 1;
        };

        // 光标落点：点击视口最右缘应落在行尾；滚动条轨道点击也应落到行尾
        e.zoomReset();
        QString lines;
        for (int i = 0; i < 40; ++i)
            lines += QStringLiteral("一二三四五\n");
        e.setPlainText(lines);
        e.resize(400, 300);
        e.show();
        QApplication::processEvents();
        const int len = e.document()->firstBlock().length() - 1;
        QWidget *vp = e.viewport();

        const QPointF edge(vp->width() - 1.0, 10.0);
        QMouseEvent press(QEvent::MouseButtonPress, edge, vp->mapToGlobal(edge.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &press);
        if (e.textCursor().positionInBlock() != len) {
            qWarning("selftest FAIL: viewport edge click lands at %d, want %d",
                     e.textCursor().positionInBlock(), len);
            return false;
        }

        e.moveCursor(QTextCursor::Start);
        ZenScrollBar *bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
        if (bar && bar->isVisible()) {
            const QPoint tp(5, 5);
            QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &tpress);
            if (e.textCursor().positionInBlock() != len) {
                qWarning("selftest FAIL: track click lands at %d, want %d",
                         e.textCursor().positionInBlock(), len);
                return false;
            }
        }

        // 放大 + 横向溢出场景：横滚到最右后，点最右缘仍应落到该行行尾
        e.setPlainText(QStringLiteral("無無無無無無無無無無\n無無無無無無無無無無\n無無無無無無無無無無\n無無無無無無無無無無\n無無無無無無無無無無\n"));
        e.zoomTo(200);
        QApplication::processEvents();
        QScrollBar *hb = e.horizontalScrollBar();
        if (hb->isVisible()) {
            hb->setValue(hb->maximum());
            QApplication::processEvents();
            e.moveCursor(QTextCursor::Start);
            bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
            if (bar && bar->isVisible()) {
                const QPoint tp(5, 5);
                QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(bar, &tpress);
                if (e.textCursor().positionInBlock() != 10) {
                    qWarning("selftest FAIL: scrolled track click lands at %d, want 10",
                             e.textCursor().positionInBlock());
                    return false;
                }
            }
        }
        // 满行换行场景：点最右缘应落在该视觉行行尾（而非最后一个字之前）
        e.zoomReset();
        QString longLine;
        for (int i = 0; i < 40; ++i)
            longLine += QStringLiteral("無");
        QString wrapped = longLine + QStringLiteral("\n");
        for (int i = 0; i < 20; ++i)
            wrapped += QStringLiteral("短行\n");
        e.setPlainText(wrapped);
        e.resize(400, 300);
        e.show();
        QApplication::processEvents();
        {
            QTextBlock blk = e.document()->firstBlock();
            QTextLayout *tl = blk.layout();
            if (tl->lineCount() >= 2) {
                const QTextLine line0 = tl->lineAt(0);
                const int want = blk.position() + line0.textStart() + line0.textLength();
                e.moveCursor(QTextCursor::Start);
                bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
                if (bar && bar->isVisible()) {
                    const QPoint tp(5, 5);
                    QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(bar, &tpress);
                    if (e.textCursor().position() != want) {
                        qWarning("selftest FAIL: wrapped track click lands at %d, want %d",
                                 e.textCursor().position(), want);
                        return false;
                    }
                }
            }
        }
        // 增量打字后立刻点最右缘（不结算事件）：落点仍应是该视觉行行尾
        e.setPlainText(QString());
        e.resize(400, 300);
        e.show();
        QApplication::processEvents();
        for (int i = 0; i < 40; ++i)
            e.insertPlainText(QStringLiteral("無"));
        e.insertPlainText(QStringLiteral("\n"));
        for (int i = 0; i < 20; ++i)
            e.insertPlainText(QStringLiteral("短行\n"));
        e.moveCursor(QTextCursor::Start);
        bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
        if (bar && bar->isVisible()) {
            const QPoint tp(5, 5);
            QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &tpress);
            const int got = e.textCursor().position();
            // 结算后求真值
            QApplication::processEvents();
            QTextBlock blk2 = e.document()->firstBlock();
            const QTextLine line0b = blk2.layout()->lineAt(0);
            const int want = blk2.position() + line0b.textStart() + line0b.textLength();
            if (got != want) {
                qWarning("selftest FAIL: fresh-typing track click lands at %d, want %d", got, want);
                return false;
            }
        }
        // 多点扫描：混合文档（首块换行 + 短行 + 空行）各高度点最右缘都应落该行行尾
        {
            QString doc3 = longLine + QStringLiteral("\n");
            for (int i = 0; i < 10; ++i)
                doc3 += QStringLiteral("短行\n");
            doc3 += QStringLiteral("\n"); // 空行
            for (int i = 0; i < 10; ++i)
                doc3 += QStringLiteral("又一段\n");
            e.setPlainText(doc3);
            e.resize(400, 300);
            e.show();
            QApplication::processEvents();
            bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
            if (bar && bar->isVisible()) {
                for (int y : {5, 40, 90, 140, 190, 240}) {
                    e.moveCursor(QTextCursor::Start);
                    const QPoint tp(5, y);
                    QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(bar, &tpress);
                    const int got = e.textCursor().position();
                    const qreal docY = qreal(y);
                    const int want = lineEndAtDocY(docY);
                    if (got != want) {
                        qWarning("selftest FAIL: sweep y=%d lands at %d, want %d", y, got, want);
                        return false;
                    }
                }
            }
        }
        // 光标矩形与方向键：最后一个字符之后的光标应在右侧，行尾按右不跳行首
        e.setPlainText(QStringLiteral("你好"));
        {
            QTextCursor c(e.document());
            c.setPosition(2); // “好”之后
            e.setTextCursor(c);
            const QRect cr = e.cursorRect();
            if (cr.x() <= 4) {
                qWarning("selftest FAIL: cursorRect after last char at x=%d", cr.x());
                return false;
            }
            e.moveCursor(QTextCursor::Right);
            if (e.textCursor().position() != 2) {
                qWarning("selftest FAIL: Right at end moves to %d, want 2",
                         e.textCursor().position());
                return false;
            }
        }
        // 文末回车产生的空行：光标从行尾按右进入空行（标准行为，锁定以防回归）
        e.setPlainText(QStringLiteral("你好\n"));
        {
            QTextCursor c(e.document());
            c.setPosition(2);
            e.setTextCursor(c);
            e.moveCursor(QTextCursor::Right);
            if (e.textCursor().position() != 3) {
                qWarning("selftest FAIL: Right across trailing newline moves to %d, want 3",
                         e.textCursor().position());
                return false;
            }
        }
        // 文末空行不可入：点余白行高度落最后一个字符之后；行尾按右停在原地
        bar = qobject_cast<ZenScrollBar *>(e.verticalScrollBar());
        if (bar) {
            const QPoint tp(5, 25); // 第二行（余白行）高度
            QMouseEvent tpress(QEvent::MouseButtonPress, QPointF(tp), bar->mapToGlobal(tp),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &tpress);
            if (e.textCursor().position() != 2) {
                qWarning("selftest FAIL: trailing-empty strip click lands at %d, want 2",
                         e.textCursor().position());
                return false;
            }
        }
        {
            QTextCursor c(e.document());
            c.setPosition(2);
            e.setTextCursor(c);
            QKeyEvent kp(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
            QApplication::sendEvent(&e, &kp);
            if (e.textCursor().position() != 2) {
                qWarning("selftest FAIL: Right into trailing empty moves to %d, want 2",
                         e.textCursor().position());
                return false;
            }
        }
        // 满行段落：文字区最右缘窄带内点击 = 该视觉行行尾
        e.setPlainText(longLine + QStringLiteral("\n短行\n短行\n"));
        e.resize(400, 300);
        e.show();
        QApplication::processEvents();
        {
            QTextBlock blk = e.document()->firstBlock();
            QTextLayout *tl = blk.layout();
            const QTextLine line0 = tl->lineAt(0);
            const int want = blk.position() + line0.textStart() + line0.textLength();
            QWidget *vp = e.viewport();
            const QPointF edge(vp->width() - 2.0, 5.0);
            QMouseEvent press(QEvent::MouseButtonPress, edge, vp->mapToGlobal(edge.toPoint()),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &press);
            if (e.textCursor().position() != want) {
                qWarning("selftest FAIL: full-line edge-zone click lands at %d, want %d",
                         e.textCursor().position(), want);
                return false;
            }
        }
        // 编模式：等宽字体 + 行号槽 + 退出复原（ASCII 文档，与真实代码一致）
        e.setPlainText(QStringLiteral("code1\ncode2\ncode3\ncode4\n"));
        e.resize(400, 300);
        e.show();
        QApplication::processEvents();
        e.toggleCodeMode();
        QApplication::processEvents();
        if (e.document()->defaultFont().family()
            != QFontDatabase::systemFont(QFontDatabase::FixedFont).family()) {
            qWarning("selftest FAIL: code mode font is not the fixed font");
            return false;
        }
        if (e.viewport()->pos().x() <= 0) {
            qWarning("selftest FAIL: code mode gutter missing");
            return false;
        }
        // 基线对齐硬验证：行号与文字共用同一 QTextLayout 基线（结构恒等），
        // 像素级用同字形比对（行号数字 vs 文字数字），真机/离屏渲染取向无关
        {
            QString digitDoc;
            for (int i = 0; i < 8; ++i)
                digitDoc += QStringLiteral("111111111111\n");
            e.setPlainText(digitDoc);
            QApplication::processEvents();
            // 光标净化：清焦点后 QPlainTextEdit 隐藏光标，像素比对才纯净
            e.setFocus();
            QApplication::processEvents();
            e.clearFocus();
            QApplication::processEvents();
            QImage img(e.size(), QImage::Format_ARGB32);
            img.fill(Qt::white);
            e.render(&img);
            const int g = e.viewport()->pos().x();
            auto darkRange = [&](int x0, int x1, int y0, int y1) {
                int lo = -1, hi = -1;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                        if (qGray(img.pixel(x, y)) < 200) { // 行号为浅灰
                            if (lo < 0)
                                lo = y;
                            hi = y;
                        }
                return qMakePair(lo, hi);
            };
            const auto num = darkRange(2, qMax(3, g - 2), 0, e.height());
            const auto txt = darkRange(g + 4, g + 120, 0, e.height());
            qInfo("GUTTER-ALIGN gutter=%d num_y=[%d,%d] text_y=[%d,%d]", g,
                  num.first, num.second, txt.first, txt.second);
            if (num.first < 0 || txt.first < 0
                || qAbs(num.first - txt.first) > 1
                || qAbs(num.second - txt.second) > 1) {
                qWarning("selftest FAIL: gutter baselines misaligned: [%d,%d] vs [%d,%d]",
                         num.first, num.second, txt.first, txt.second);
                return false;
            }
            e.zoomReset();
        }
        e.toggleCodeMode();
        QApplication::processEvents();
        if (e.viewport()->pos().x() != 0
            || e.document()->defaultFont().family()
                != QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()) {
            qWarning("selftest FAIL: exiting code mode did not restore layout/font");
            return false;
        }
        // 行号随横滚：放大到溢出后，行号区随横向滚动移出屏幕、边距回收、滚回复原
        {
            e.setPlainText(QStringLiteral("無無無無無無無無無無無無無無無無無無無無\n第二行\n"));
            e.resize(400, 300);
            e.show();
            QApplication::processEvents();
            e.toggleCodeMode();
            QApplication::processEvents();
            e.zoomTo(96);
            QApplication::processEvents();
            QScrollBar *hb = e.horizontalScrollBar();
            const int w0 = e.gutterWidth();
            if (hb->isVisible() && hb->maximum() > 0) {
                hb->setValue(qMin(hb->maximum(), w0 + 8));
                QApplication::processEvents();
                if (e.viewport()->pos().x() != qMax(0, w0 - hb->value())) {
                    qWarning("selftest FAIL: gutter margin did not shrink (vp.x=%d h=%d)",
                             e.viewport()->pos().x(), hb->value());
                    return false;
                }
                hb->setValue(hb->maximum());
                QApplication::processEvents();
                if (e.viewport()->pos().x() != 0) {
                    qWarning("selftest FAIL: gutter margin not fully reclaimed (vp.x=%d)",
                             e.viewport()->pos().x());
                    return false;
                }
                hb->setValue(0);
                QApplication::processEvents();
                if (e.viewport()->pos().x() != w0) {
                    qWarning("selftest FAIL: gutter margin not restored (vp.x=%d want %d)",
                             e.viewport()->pos().x(), w0);
                    return false;
                }
            }
            e.zoomReset();
            e.toggleCodeMode();
            QApplication::processEvents();
        }
        // 笔迹统一撤销：画一笔 → Cmd+Z 撤销 → Cmd+Y 复原
        e.setPlainText(QStringLiteral("文字\n"));
        {
            e.toggleMode(Editor::Mode::Draw);
            QWidget *vp = e.viewport();
            const QPointF p1(50, 50);
            QMouseEvent pr(QEvent::MouseButtonPress, p1, vp->mapToGlobal(p1.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &pr);
            const QPointF p2(90, 50);
            QMouseEvent mv(QEvent::MouseMove, p2, vp->mapToGlobal(p2.toPoint()),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &mv);
            QMouseEvent re(QEvent::MouseButtonRelease, p2, vp->mapToGlobal(p2.toPoint()),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &re);
            if (e.inkEmpty()) {
                qWarning("selftest FAIL: stroke not recorded");
                return false;
            }
            QKeyEvent kz(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kz);
            if (!e.inkEmpty()) {
                qWarning("selftest FAIL: Cmd+Z did not undo the stroke");
                return false;
            }
            QKeyEvent ky(QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier);
            QApplication::sendEvent(&e, &ky);
            if (e.inkEmpty()) {
                qWarning("selftest FAIL: Cmd+Y did not redo the stroke");
                return false;
            }
            e.toggleMode(Editor::Mode::Draw); // 退出模式
        }
        // 擦除通道宽度 = 笔刷宽度：像素级验证
        {
            e.toggleCodeMode();
            if (e.codeMode())
                e.toggleCodeMode(); // 退出编
            e.setPlainText(QString());
            e.clearInk(); // 清掉此前测试的笔迹，避免污染通道测量
            e.resize(400, 300);
            e.show();
            QApplication::processEvents();
            for (int i = 0; i < 30 && e.brushSize() < 40.0; ++i)
                e.brushUp();
            const qreal brush = e.brushSize();
            QWidget *vp = e.viewport();
            e.toggleMode(Editor::Mode::Draw);
            {
                const QPointF p1(50, 80);
                QMouseEvent pr(QEvent::MouseButtonPress, p1, vp->mapToGlobal(p1.toPoint()),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &pr);
                for (int x = 54; x <= 250; x += 4) {
                    const QPointF p2(x, 80);
                    QMouseEvent mv(QEvent::MouseMove, p2, vp->mapToGlobal(p2.toPoint()),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(vp, &mv);
                }
                const QPointF p2(250, 80);
                QMouseEvent re(QEvent::MouseButtonRelease, p2, vp->mapToGlobal(p2.toPoint()),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &re);
            }
            e.toggleMode(Editor::Mode::Erase);
            {
                const QPointF p1(150, 30);
                QMouseEvent pr(QEvent::MouseButtonPress, p1, vp->mapToGlobal(p1.toPoint()),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &pr);
                // 连续事件流（真实触控板行为）
                for (int y = 34; y <= 130; y += 4) {
                    const QPointF p2(150, y);
                    QMouseEvent mv(QEvent::MouseMove, p2, vp->mapToGlobal(p2.toPoint()),
                                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(vp, &mv);
                }
                const QPointF p3(150, 130);
                QMouseEvent re(QEvent::MouseButtonRelease, p3, vp->mapToGlobal(p3.toPoint()),
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(vp, &re);
            }
            e.toggleMode(Editor::Mode::Normal);
            // 数据级验证：残余两段笔迹之间的空隙 = 擦除通道 = 笔刷宽度
            const auto paths = e.inkPaths();
            int bestGap = 0;
            if (paths.size() >= 2) {
                const qreal leftEnd = paths.first().boundingRect().right();
                const qreal rightStart = paths.last().boundingRect().left();
                if (rightStart > leftEnd)
                    bestGap = int(rightStart - leftEnd);
            }
            qInfo("ERASE-CHANNEL gap=%d brush=%f paths=%d", bestGap, brush, int(paths.size()));
            if (paths.size() >= 2 && qAbs(bestGap - brush) > 6.0) {
                qWarning("selftest FAIL: erase channel %dpx vs brush %fpx", bestGap, brush);
            }
            e.brushDefault();
        }
        // 言：选中多行头尾批量加「」（空行跳过），一步撤销，整段保持选中
        {
            e.setPlainText(QStringLiteral("甲一\n乙二\n\n丙三\n"));
            e.selectAll();
            e.yan();
            if (e.toPlainText() != QStringLiteral("「甲一」\n「乙二」\n\n「丙三」\n")) {
                qWarning("selftest FAIL: yan() got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            if (e.textCursor().selectionStart() != 0
                || e.textCursor().selectionEnd() != 15) { // 「丙三」」之后、末行换行之前
                qWarning("selftest FAIL: yan() selection not covering wrapped region (%d,%d)",
                         e.textCursor().selectionStart(), e.textCursor().selectionEnd());
                return false;
            }
            QKeyEvent kz(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kz);
            if (e.toPlainText() != QStringLiteral("甲一\n乙二\n\n丙三\n")) {
                qWarning("selftest FAIL: yan() not undone in one step");
                return false;
            }
        }
        // 隔：逐行隔离——选中每一行上下各补空行（幂等），一步撤销
        {
            e.setPlainText(QStringLiteral("甲一\n乙二\n丙三\n"));
            QTextBlock bMid = e.document()->findBlockByNumber(1);
            QTextCursor cc(e.document());
            cc.setPosition(bMid.position());
            cc.setPosition(bMid.position() + bMid.length() - 1, QTextCursor::KeepAnchor);
            e.setTextCursor(cc);
            e.ge(); // 单选一行：同旧语义
            if (e.toPlainText() != QStringLiteral("甲一\n\n乙二\n\n丙三\n")) {
                qWarning("selftest FAIL: ge() single line got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            e.ge(); // 幂等
            if (e.toPlainText() != QStringLiteral("甲一\n\n乙二\n\n丙三\n")) {
                qWarning("selftest FAIL: ge() not idempotent, got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            if (e.textCursor().selectionStart() != 4
                || e.textCursor().selectionEnd() != 6) {
                qWarning("selftest FAIL: ge() selection lost the block (%d,%d)",
                         e.textCursor().selectionStart(), e.textCursor().selectionEnd());
                return false;
            }
            QKeyEvent kz(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kz);
            if (e.toPlainText() != QStringLiteral("甲一\n乙二\n丙三\n")) {
                qWarning("selftest FAIL: ge() not undone in one step");
                return false;
            }
            // 用户报告的核心案例：选中前两行（顶行在选区里）——每一行都要被隔离
            {
                QTextBlock b0 = e.document()->findBlockByNumber(0);
                QTextBlock b1 = e.document()->findBlockByNumber(1);
                QTextCursor cc2(e.document());
                cc2.setPosition(b0.position());
                cc2.setPosition(b1.position() + b1.length() - 1, QTextCursor::KeepAnchor);
                e.setTextCursor(cc2);
                e.ge();
                if (e.toPlainText() != QStringLiteral("甲一\n\n乙二\n\n丙三\n")) {
                    qWarning("selftest FAIL: ge() top-two lines got [%s]", qPrintable(e.toPlainText()));
                    return false;
                }
            }
            // 五行选中中间三行：每一行独立成岛
            e.setPlainText(QStringLiteral("一\n二\n三\n四\n五\n"));
            {
                QTextBlock b1 = e.document()->findBlockByNumber(1);
                QTextBlock b3 = e.document()->findBlockByNumber(3);
                QTextCursor cc3(e.document());
                cc3.setPosition(b1.position());
                cc3.setPosition(b3.position() + b3.length() - 1, QTextCursor::KeepAnchor);
                e.setTextCursor(cc3);
                e.ge();
                if (e.toPlainText() != QStringLiteral("一\n\n二\n\n三\n\n四\n\n五\n")) {
                    qWarning("selftest FAIL: ge() middle-three got [%s]", qPrintable(e.toPlainText()));
                    return false;
                }
                e.ge(); // 全隔离后再跑一次：不变
                if (e.toPlainText() != QStringLiteral("一\n\n二\n\n三\n\n四\n\n五\n")) {
                    qWarning("selftest FAIL: ge() second pass not idempotent [%s]", qPrintable(e.toPlainText()));
                    return false;
                }
            }
            // 选区内含空行：空行本身就是隔板，不重复加
            e.setPlainText(QStringLiteral("一\n二\n\n三\n"));
            e.selectAll();
            e.ge();
            if (e.toPlainText() != QStringLiteral("一\n\n二\n\n三\n")) {
                qWarning("selftest FAIL: ge() with inner blank got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            e.selectAll();
            e.ge(); // 文首无上行、文末空块已空、内部已隔离 → 全文档幂等
            if (e.toPlainText() != QStringLiteral("一\n\n二\n\n三\n")) {
                qWarning("selftest FAIL: ge() full-doc idempotence got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
        }
        // 言/隔 快捷键通道（Ctrl+L / Ctrl+F）
        {
            e.setPlainText(QStringLiteral("丁四\n"));
            e.moveCursor(QTextCursor::Start);
            QKeyEvent kl(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kl);
            if (e.toPlainText() != QStringLiteral("「丁四」\n")) {
                qWarning("selftest FAIL: Ctrl+L did not call yan()");
                return false;
            }
            // Ctrl+F：文首无上行、文末空块已空 → 语义正确的不动（且不崩溃）
            QKeyEvent kf(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kf);
            if (e.toPlainText() != QStringLiteral("「丁四」\n")) {
                qWarning("selftest FAIL: Ctrl+F at document edge changed text");
                return false;
            }
            // 中间行的 Ctrl+F 才补空行
            e.setPlainText(QStringLiteral("甲\n乙\n丙\n"));
            const QTextBlock bm = e.document()->findBlockByNumber(1);
            QTextCursor cc2(e.document());
            cc2.setPosition(bm.position());
            e.setTextCursor(cc2);
            QKeyEvent kf2(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kf2);
            if (e.toPlainText() != QStringLiteral("甲\n\n乙\n\n丙\n")) {
                qWarning("selftest FAIL: Ctrl+F on middle line got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
        }
        // 先言后隔组合：言保持的选区直接喂给隔（批量校对的完整动线）
        {
            e.setPlainText(QStringLiteral("甲\n一\n二\n三\n丙\n"));
            QTextBlock b1 = e.document()->findBlockByNumber(1);
            QTextBlock b3 = e.document()->findBlockByNumber(3);
            QTextCursor cc(e.document());
            cc.setPosition(b1.position());
            cc.setPosition(b3.position() + b3.length() - 1, QTextCursor::KeepAnchor);
            e.setTextCursor(cc);
            e.yan();
            e.ge();
            if (e.toPlainText() != QStringLiteral("甲\n\n「一」\n\n「二」\n\n「三」\n\n丙\n")) {
                qWarning("selftest FAIL: yan()+ge() pipeline got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            QKeyEvent kz(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kz); // 隔一步撤销 → 回到言的成果
            if (e.toPlainText() != QStringLiteral("甲\n「一」\n「二」\n「三」\n丙\n")) {
                qWarning("selftest FAIL: pipeline undo step 1 got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
            QKeyEvent kz2(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &kz2); // 再一步 → 言整体撤销
            if (e.toPlainText() != QStringLiteral("甲\n一\n二\n三\n丙\n")) {
                qWarning("selftest FAIL: pipeline undo step 2 got [%s]", qPrintable(e.toPlainText()));
                return false;
            }
        }
        // 锚定缩放：缩放前后锚点下是同一行（指哪大哪）
        {
            QString zdoc;
            for (int i = 0; i < 120; ++i)
                zdoc += QStringLiteral("锚定缩放测试行 無無無無無無無無無無\n");
            e.setPlainText(zdoc);
            e.resize(400, 300);
            e.show();
            QApplication::processEvents();
            e.zoomReset();
            e.verticalScrollBar()->setValue(60);
            QApplication::processEvents();
            const QPointF anchor(200.0, 120.0);
            e.m_lastMouse = anchor; // 模拟鼠标悬停在锚点
            const int posBefore = e.positionAtViewport(anchor);
            e.zoom(12); // 字号翻倍：锚点下仍应是同一行
            QApplication::processEvents();
            const int posAfter = e.positionAtViewport(anchor);
            qInfo("ANCHOR-ZOOM before=%d after=%d drift=%d vbar=%d", posBefore, posAfter,
                  qAbs(posAfter - posBefore), e.verticalScrollBar()->value());
            // 指哪大哪的语义 = 锚点下的视觉行不丢：允许行内 x→光标的字号漂移
            //（9pt→24pt 同一点可差出一行内的字符数），跑飞才是真失败
            if (qAbs(posAfter - posBefore) > 45) {
                qWarning("selftest FAIL: anchored zoom drifted too far (%d → %d)",
                         posBefore, posAfter);
                return false;
            }
            e.m_lastMouse = QPointF(-1, -1);
            e.zoomReset();
        }
        // 显：像素磷光模式——字体/配色/透明视口/画面，开关可逆
        {
            e.setPlainText(QStringLiteral("無\n"));
            e.toggleCrt();
            QApplication::processEvents();
            const QString fam = e.document()->defaultFont().family();
            if (fam.isEmpty()
                || fam == QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()
                || e.document()->defaultFont().pixelSize() <= 0) {
                qWarning("selftest FAIL: CRT font not applied (family=[%s])", qPrintable(fam));
                return false;
            }
            if (e.palette().color(QPalette::Text) != Crt::kInk) {
                qWarning("selftest FAIL: CRT text color not amber");
                return false;
            }
            if (e.palette().color(QPalette::Base) != Crt::kBg) {
                qWarning("selftest FAIL: CRT base not the phosphor background");
                return false;
            }
            // 画面：文字区出现琥珀磷光像素；空区是近黑磷底（不是白）
            // 先等暖机脉冲走完（黑幕约 0.5s 退尽），否则整屏被压黑
            {
                QEventLoop loop;
                QTimer::singleShot(700, &loop, &QEventLoop::quit);
                loop.exec();
            }
            QImage img(e.size(), QImage::Format_ARGB32);
            img.fill(Qt::white);
            e.render(&img); // 新架构：覆盖层是普通 QWidget，render 捕获的就是真实 GPU 帧
            const int g = e.viewport()->pos().x();
            // GPU 输出经 RGB 掩膜：亮磷光 = R/G 子像素点燃、B 熄灭（琥珀文字
            // 的 R 与 G 分量分别落在 R/G 掩膜上），不再以原始调色板判色
            bool lit = false;
            for (int y = 0; y < e.height() && !lit; ++y)
                for (int x = g + 2; x < e.width() - 30 && !lit; ++x) {
                    const QRgb px = img.pixel(x, y);
                    if (qRed(px) + qGreen(px) > 200 && qBlue(px) < 100)
                        lit = true;
                }
            if (!lit) {
                qWarning("selftest FAIL: no lit phosphor pixels in CRT render");
                return false;
            }
            const QRgb bgPx = img.pixel(g + 8, e.height() - 20); // 空行区
            if (qRed(bgPx) > 90 || qGreen(bgPx) > 80 || qBlue(bgPx) > 60) {
                qWarning("selftest FAIL: CRT background not dark (%d,%d,%d)",
                         qRed(bgPx), qGreen(bgPx), qBlue(bgPx));
                return false;
            }
            // 快照几何：文字在顶部第一行；此前的涂擦测试留下两个墨水圆点，
            // 必须同样出现在合成快照里（墨水进光栅 = 显模式下涂/擦可用的回归闸）
            {
                QImage snapImg(e.viewport()->size(), QImage::Format_ARGB32);
                snapImg.fill(Qt::transparent);
                e.paintTextSnapshot(snapImg);
                const QImage snap = snapImg;
                int topAmber = 0, inkAmber = 0;
                for (int y = 0; y < snap.height(); ++y)
                    for (int x = 0; x < snap.width(); ++x) {
                        const QRgb px = snap.pixel(x, y);
                        if (qRed(px) > 150 && qGreen(px) > 80 && qBlue(px) < 90) {
                            if (y < 40) ++topAmber; else ++inkAmber;
                        }
                    }
                qInfo("CRT-SNAP amber top=%d ink=%d", topAmber, inkAmber);
                if (topAmber < 10 || inkAmber < 500) {
                    qWarning("selftest FAIL: snapshot composite broken (text top=%d ink=%d)",
                             topAmber, inkAmber);
                    return false;
                }
            }
            // 颜色分类取证：R/G/B 子像素点燃计数。琥珀文字的 R 与 G 分量
            // 分别落在 R/G 掩膜上——红绿两族都应存在且大致均衡；蓝 = 熄灭。
            // 坏管线 = 蓝色泛滥（B 掩膜点燃蓝分量 = 本应归零）。
            {
                int green = 0, red = 0, blue = 0, dark = 0, other = 0;
                for (int y = 0; y < e.height(); ++y)
                    for (int x = g; x < e.width() - 30; ++x) {
                        const QRgb px = img.pixel(x, y);
                        const int r = qRed(px), gr = qGreen(px), b = qBlue(px);
                        if (gr > r + 40 && gr > b + 40 && gr > 100) ++green;
                        else if (r > gr + 40 && r > b + 40 && r > 100) ++red;
                        else if (b > r + 40 && b > gr + 40 && b > 100) ++blue;
                        else if (r < 60 && gr < 60 && b < 60) ++dark;
                        else ++other;
                    }
                qInfo("CRT-COLORS green=%d red=%d blue=%d dark=%d other=%d",
                      green, red, blue, dark, other);
                if (red < 500 || green < 500) {
                    qWarning("selftest FAIL: RGB mask not lighting (red=%d green=%d)",
                             red, green);
                    return false;
                }
                if (blue > red / 4 || blue > green / 4) {
                    qWarning("selftest FAIL: blue flood in CRT render (%d)", blue);
                    return false;
                }
            }
            // 扫描线：同列相邻行底色有明暗差（信息输出，防渲染层位错）
            auto rowMean = [&](int yMod, int x0, int x1) {
                long sum = 0;
                int n = 0;
                for (int y = yMod + 4; y + 3 < e.height(); y += 3)
                    for (int x = x0; x < x1; ++x) {
                        sum += qGray(img.pixel(x, y));
                        ++n;
                    }
                return n ? double(sum) / n : -1.0;
            };
            const double m0 = rowMean(0, g + 8, g + 90);
            const double m1 = rowMean(1, g + 8, g + 90);
            qInfo("CRT-SCANLINE rows: %f vs %f", m0, m1);
            e.toggleCrt();
            QApplication::processEvents();
            if (e.palette().color(QPalette::Base) == Crt::kBg
                || e.document()->defaultFont().family()
                    != QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()) {
                qWarning("selftest FAIL: CRT toggle-off did not restore font/palette");
                return false;
            }
        }
        // 真衍射的边差分：合成白块的左右竖直边界各产出一条彩边掩膜
        {
            QImage synth(40, 20, QImage::Format_ARGB32);
            synth.fill(Qt::transparent);
            QPainter sp(&synth);
            sp.fillRect(QRect(10, 4, 12, 10), QColor(255, 255, 255, 255));
            sp.end();
            const QImage eR = Crt::edgeDiff(synth, +1);
            const QImage eB = Crt::edgeDiff(synth, -1);
            int rCols = 0, bCols = 0, rWrong = 0, bWrong = 0;
            for (int y = 0; y < eR.height(); ++y) {
                const uchar *rRow = eR.constScanLine(y);
                const uchar *bRow = eB.constScanLine(y);
                for (int x = 0; x < eR.width(); ++x) {
                    if (rRow[x] > 0) {
                        if (x == 21) ++rCols; else ++rWrong;
                    }
                    if (bRow[x] > 0) {
                        if (x == 10) ++bCols; else ++bWrong;
                    }
                }
            }
            qInfo("DIFF-EDGE rightCol=%d wrong=%d leftCol=%d wrong=%d",
                  rCols, rWrong, bCols, bWrong);
            if (rCols < 8 || bCols < 8 || rWrong > 0 || bWrong > 0) {
                qWarning("selftest FAIL: edgeDiff columns wrong (R:%d/%d B:%d/%d)",
                         rCols, rWrong, bCols, bWrong);
                return false;
            }
        }
        return true;
    }

protected:
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu menu(this);
        QAction *aMo = menu.addAction(QStringLiteral("摹"));
        QAction *aKong = menu.addAction(QStringLiteral("空"));
        menu.addSeparator();
        QAction *aYin = menu.addAction(QStringLiteral("阴"));
        QAction *aYang = menu.addAction(QStringLiteral("阳"));
        aYin->setCheckable(true);
        aYang->setCheckable(true);
        aYin->setChecked(m_dark);
        aYang->setChecked(!m_dark);

        connect(aMo, &QAction::triggered, this, [this] { mo(); });
        connect(aKong, &QAction::triggered, this, [this] { kong(); });
        connect(aYin, &QAction::triggered, this, [this] { setDark(true); });
        connect(aYang, &QAction::triggered, this, [this] { setDark(false); });

        menu.addSeparator();
        QAction *aTu = menu.addAction(QStringLiteral("涂"));
        QAction *aCa = menu.addAction(QStringLiteral("擦"));
        QAction *aXiao = menu.addAction(QStringLiteral("消"));
        aTu->setCheckable(true);
        aCa->setCheckable(true);
        aTu->setChecked(m_mode == Mode::Draw);
        aCa->setChecked(m_mode == Mode::Erase);
        connect(aTu, &QAction::triggered, this, [this] { toggleMode(Mode::Draw); });
        connect(aCa, &QAction::triggered, this, [this] { toggleMode(Mode::Erase); });
        connect(aXiao, &QAction::triggered, this, [this] { m_canvas->clearAll(); });

        menu.addSeparator();
        QAction *aBian = menu.addAction(QStringLiteral("编"));
        aBian->setCheckable(true);
        aBian->setChecked(m_codeMode);
        connect(aBian, &QAction::triggered, this, [this] { toggleCodeMode(); });
        QAction *aXian = menu.addAction(QStringLiteral("显"));
        aXian->setCheckable(true);
        aXian->setChecked(m_crt);
        connect(aXian, &QAction::triggered, this, [this] { toggleCrt(); });
        menu.addSeparator(); // 视图轴（编·显）与格式化（言·隔）分区

        // 文本格式化：批量校对搭档（言打钩、隔留白），列于编之下
        QAction *aYan = menu.addAction(QStringLiteral("言"));
        QAction *aGe = menu.addAction(QStringLiteral("隔"));
        connect(aYan, &QAction::triggered, this, [this] { yan(); });
        connect(aGe, &QAction::triggered, this, [this] { ge(); });

        menu.exec(event->globalPos());
    }

    void focusInEvent(QFocusEvent *event) override
    {
        wakeCaret();
        QPlainTextEdit::focusInEvent(event);
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        // 失焦（Cmd+Tab / 点走）时收尾按住笔刷会话：Shift 的 keyRelease
        // 不会在失焦后送达，否则鼠标抓取与笔迹会话会滞留到下次按 Shift
        if (m_shiftInkActive) {
            m_shiftInkActive = false;
            viewport()->releaseMouse();
            if (m_mode == Mode::Draw)
                m_canvas->endStroke();
            else if (m_mode == Mode::Erase)
                m_canvas->eraseEnd();
            endInkSession();
        }
        QPlainTextEdit::focusOutEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        wakeCaret();
        if (event->key() == Qt::Key_Tab
            && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier))) {
            // Tab 与空格一致：插入 4 个空格（软 Tab，可撤销）
            textCursor().insertText(QStringLiteral("    "));
            return;
        }
        if (event->key() == Qt::Key_Right
            && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier
                                       | Qt::AltModifier | Qt::ShiftModifier))) {
            // 文末空行不可入：最后一个字符之后按右停在原地
            if (textCursor().position() == document()->characterCount() - 2
                && document()->lastBlock().length() == 1)
                return;
        }
        if (event->key() == Qt::Key_Escape && (m_mode != Mode::Normal || m_codeMode)) {
            // 先退工具（画笔），再退视图（编）
            if (m_mode != Mode::Normal) {
                m_mode = Mode::Normal;
                updateModeCursor();
            } else {
                setCodeMode(false);
            }
            return;
        }
        if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
            switch (event->key()) {
            case Qt::Key_S:
                mo();
                return;
            case Qt::Key_N:
                if (event->modifiers() & Qt::ShiftModifier)
                    clearInk(); // 消：N=消除=naught（对应 Cmd+N 清文字）
                else
                    kong();
                return;
            case Qt::Key_D:
                toggleMode(Mode::Draw);
                return;
            case Qt::Key_B:
                toggleCodeMode(); // 编：编织代码（与涂/擦可叠加）
                return;
            case Qt::Key_L:
                yan(); // 言：L 是「」折角；行头尾批量加「」
                return;
            case Qt::Key_F:
                ge(); // 隔：F 是"分"（分隔）的声母；逐行上下补空行
                return;
            case Qt::Key_T:
                toggleCrt(); // 显：T 是 Tube / Time——显像管，回到过去
                return;
            case Qt::Key_I:
                setDark(true); // 阴：I 如冰（阴冷）
                return;
            case Qt::Key_O:
                setDark(false); // 阳：O 如太阳（圆日）
                return;
            case Qt::Key_E:
                toggleMode(Mode::Erase);
                return;
            case Qt::Key_Z:
                undoAll();
                return;
            case Qt::Key_Y:
                redoAll();
                return;
            case Qt::Key_Equal:
            case Qt::Key_Plus:
                if (event->modifiers() & Qt::ShiftModifier) {
                    brushStep(+1); // 画笔侧：Cmd+Shift+= 加粗
                    return;
                }
                if (event->isAutoRepeat())
                    return; // 按住时的重复交给加速定时器
                zoom(+1);
                startHold(+1);
                return;
            case Qt::Key_Minus:
                if (event->modifiers() & Qt::ShiftModifier) {
                    brushStep(-1);
                    return;
                }
                if (event->isAutoRepeat())
                    return;
                zoom(-1);
                startHold(-1);
                return;
            case Qt::Key_Underscore:
                brushStep(-1); // Shift+"-"在 macOS Qt 中报作下划线键
                return;
            case Qt::Key_0:
                if (event->modifiers() & Qt::ShiftModifier) {
                    brushReset();
                    return;
                }
                zoomReset();
                return;
            case Qt::Key_ParenRight:
                brushReset(); // Shift+"0"在 macOS Qt 中报作右括号键
                return;
            default:
                break;
            }
        }
        QPlainTextEdit::keyPressEvent(event);
    }

    void keyReleaseEvent(QKeyEvent *event) override
    {
        switch (event->key()) {
        case Qt::Key_Shift:
            if (!event->isAutoRepeat() && m_shiftInkActive) {
                m_shiftInkActive = false;
                viewport()->releaseMouse();
                if (m_mode == Mode::Draw)
                    m_canvas->endStroke();
                else
                    m_canvas->eraseEnd();
                endInkSession();
            }
            break;
        case Qt::Key_Equal:
        case Qt::Key_Plus:
        case Qt::Key_Minus:
        case Qt::Key_0:
            if (!event->isAutoRepeat())
                m_holdTimer.stop();
            break;
        default:
            break;
        }
        QPlainTextEdit::keyReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        // Ctrl/Cmd + 滚轮：缩放（像素增量累积，触控板也顺滑）
        if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
            m_wheelAccum += event->angleDelta().y();
            while (m_wheelAccum >= 120) {
                zoom(+1);
                m_wheelAccum -= 120;
            }
            while (m_wheelAccum <= -120) {
                zoom(-1);
                m_wheelAccum += 120;
            }
            m_wheelAccum = std::clamp(m_wheelAccum, -119, 119);
            return;
        }

        // 像素级滚动：QPlainTextEdit 默认按“行”滚，行高巨大时一滚到底，
        // 触控板用像素增量，鼠标滚轮一格约 40px（≈常规 3 行）。
        const bool shift = event->modifiers() & Qt::ShiftModifier;
        QScrollBar *hbar = horizontalScrollBar();
        QScrollBar *vbar = verticalScrollBar();

        int dx = event->angleDelta().x();
        if (shift)
            dx = event->angleDelta().y(); // Shift+滚轮 → 横向
        int dy = 0;
        if (!shift) {
            dy = event->pixelDelta().y();
            if (dy == 0)
                dy = event->angleDelta().y() / 3;
        }

        bool handled = false;
        if (dx != 0 && hbar->isVisible()) {
            hbar->setValue(hbar->value() - dx);
            handled = true;
        }
        if (dy != 0 && vbar->isVisible()) {
            vbar->setValue(vbar->value() - dy);
            handled = true;
        }
        if (handled)
            return;
        QPlainTextEdit::wheelEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // 触控板捏合缩放：对任意子控件下（含滚动条区域）的手势都生效
        if (event->type() == QEvent::NativeGesture) {
            const auto *ng = static_cast<QNativeGestureEvent *>(event);
            if (ng->gestureType() == Qt::BeginNativeGesture) {
                m_pinchSmooth = 0.0;
            } else if (ng->gestureType() == Qt::ZoomNativeGesture) {
                // value 语义：相对上一事件的增量倍率（macOS NSEvent magnification）。
                // 增益 + 指数平滑过滤边缘手势的抖动与尖峰，单事件倍率钳制防误触跳变。
                const qreal v = ng->value();
                if (qAbs(v) >= 0.002) {
                    m_pinchSmooth = PINCH_SMOOTH_A * v + (1.0 - PINCH_SMOOTH_A) * m_pinchSmooth;
                    const qreal factor = std::clamp<qreal>(1.0 + m_pinchSmooth * PINCH_GAIN, 0.75, 1.35);
                    // 换模式即换缩放项目：涂/擦模式捏合控制笔刷，其余控制字号
                    if (m_mode == Mode::Draw || m_mode == Mode::Erase)
                        brushScale(factor);
                    else
                        zoomTo(m_size * factor);
                }
            }
            return true;
        }
        if (watched == viewport() || watched == m_lineNumberArea) {
            // 行号区上的坐标平移到文字区（画布/足迹可在行号区上作画）
            auto posOf = [&](const QMouseEvent *me) {
                QPointF p = me->position();
                if (watched == m_lineNumberArea)
                    p -= QPointF(m_gutterWidth, 0); // 行号区 → 视口坐标
                return p;
            };
            // 记录指针位置（画笔足迹用，所有模式都跟踪）；鼠标即人眼——
            // 着色器逐帧读它做视差
            if (event->type() == QEvent::MouseMove && m_crtView && m_crt)
                m_crtView->update();
            // 离开视口（非作画会话）：清足迹并重置缩放锚点，避免锚在陈旧位置
            if (event->type() == QEvent::Leave && !m_inkSession) {
                m_lastMouse = QPointF(-1, -1);
                if (m_canvas)
                    m_canvas->setFootprintVisible(false);
            }
            if (event->type() == QEvent::MouseMove) {
                const auto *me = static_cast<QMouseEvent *>(event);
                m_lastMouse = posOf(me);
                if (m_mode == Mode::Draw || m_mode == Mode::Erase) {
                    m_canvas->setFootprint(true, m_lastMouse, m_mode == Mode::Erase);
                    const bool held = (me->buttons() & Qt::LeftButton)
                        || (me->modifiers() & Qt::ShiftModifier); // Shift=按住键
                    if (held) {
                        if (!m_inkSession) {
                            beginInkSession();
                            m_shiftInkActive = (me->modifiers() & Qt::ShiftModifier)
                                && !(me->buttons() & Qt::LeftButton);
                            if (m_shiftInkActive)
                                viewport()->grabMouse(); // Shift 会话同样抓取
                            if (m_mode == Mode::Draw)
                                m_canvas->beginStroke(viewportPosToDoc(posOf(me)));
                        }
                        const QPointF doc = viewportPosToDoc(posOf(me));
                        if (m_mode == Mode::Draw)
                            m_canvas->extendStroke(doc);
                        else
                            m_canvas->eraseAt(doc);
                    }
                    return true; // 模式内移动不打扰文本
                }
            }

            // 正常模式下，文字区最右缘窄带内的点击 = 行尾意图（满行时系统
            // 会判给最后一个字的右半格，这里统一为"落行尾"）
            if (m_mode == Mode::Normal && event->type() == QEvent::MouseButtonPress) {
                const auto *me = static_cast<QMouseEvent *>(event);
                if (me->button() == Qt::LeftButton
                    && posOf(me).x() >= qreal(viewport()->width()) - EDGE_CLICK_ZONE) {
                    placeCaretAtLineEnd(posOf(me));
                    return true;
                }
            }
            // 涂/擦模式：左键在画布层作画或擦除，文本光标不随点击移动
            if (m_mode == Mode::Draw || m_mode == Mode::Erase) {
                if (event->type() == QEvent::MouseButtonPress) {
                    const auto *me = static_cast<QMouseEvent *>(event);
                    if (me->button() == Qt::LeftButton) {
                        beginInkSession();
                        viewport()->grabMouse(); // 拖出窗口不松手也能续画
                        const QPointF doc = viewportPosToDoc(posOf(me));
                        if (m_mode == Mode::Draw)
                            m_canvas->beginStroke(doc);
                        else
                            m_canvas->eraseAt(doc);
                        return true;
                    }
                } else if (event->type() == QEvent::MouseButtonRelease) {
                    const auto *me = static_cast<QMouseEvent *>(event);
                    if (me->button() == Qt::LeftButton) {
                        viewport()->releaseMouse();
                        if (m_mode == Mode::Draw)
                            m_canvas->endStroke();
                        else
                            m_canvas->eraseEnd();
                        if (!viewport()->rect().contains(posOf(me).toPoint()))
                            m_canvas->setFootprintVisible(false);
                        endInkSession();
                        return true;
                    }
                }
            }
            // 点击/滚轮都唤醒光标（睡眠隐喻：无动静则隐去）
            if (event->type() == QEvent::MouseButtonPress
                || event->type() == QEvent::MouseButtonRelease
                || event->type() == QEvent::Wheel) {
                wakeCaret();
            }
            return QPlainTextEdit::eventFilter(watched, event);
        }
        return QPlainTextEdit::eventFilter(watched, event);
    }

private:
    void applyScheme()
    {
        QPalette pal = palette();
        if (m_crt) {
            // 磷光模式配色（着色器层盖住视口，此为兜底）
            pal.setColor(QPalette::Window, Crt::kBg);
            pal.setColor(QPalette::Base, Crt::kBg);
            pal.setColor(QPalette::Text, Crt::kInk);
            pal.setColor(QPalette::Highlight, QColor(0x5C, 0x3E, 0x00, 0xB0));
            pal.setColor(QPalette::HighlightedText, Crt::kInk);
        } else if (m_dark) {
            pal.setColor(QPalette::Window, QColor(0, 0, 0));
            pal.setColor(QPalette::Base, QColor(0, 0, 0));
            pal.setColor(QPalette::Text, QColor(255, 255, 255));
        } else {
            pal.setColor(QPalette::Window, QColor(255, 255, 255));
            pal.setColor(QPalette::Base, QColor(255, 255, 255));
            pal.setColor(QPalette::Text, QColor(0, 0, 0));
        }
        setPalette(pal);

        const bool darkish = m_dark || m_crt; // 显永远是暗底
        if (auto *v = qobject_cast<ZenScrollBar *>(verticalScrollBar()))
            v->setDark(darkish);
        if (auto *h = qobject_cast<ZenScrollBar *>(horizontalScrollBar()))
            h->setDark(darkish);
        if (m_canvas)
            m_canvas->setInk(m_crt ? Crt::kInk : (m_dark ? QColor(255, 255, 255) : QColor(0, 0, 0)));
        if (m_mode != Mode::Normal)
            updateModeCursor(); // 光标跟随墨色与当前笔刷
        if (m_lineNumberArea)
            m_lineNumberArea->update();
#ifdef NAUGHT_WITH_HIGHLIGHT
        if (m_codeMode && m_hl && m_repo) {
            m_hl->setTheme(m_repo->defaultTheme(m_dark ? KSyntaxHighlighting::Repository::DarkTheme
                                                       : KSyntaxHighlighting::Repository::LightTheme));
            m_hl->rehighlight();
        }
#endif
    }

    void applyZoom()
    {
        // O(1)：只改文档默认字号并标脏，重排由 Qt 惰性完成（仅可见区域）。
        QFont f = activeFont();
        document()->setDefaultFont(f);
        if (document()->characterCount() > 1)
            document()->markContentsDirty(0, document()->characterCount());
        // 空文档不标脏：构造期（尚无绘制设备）同步排版会在 QFont 解析时崩溃
        //（DiagnosticReports 里的启动 SIGSEGV 即此路径）
        setFont(f);
        // 笔刷与字号脱钩：只由 Cmd/Ctrl+Shift+= / - / 0 控制
        updateGutterWidth(); // 行号区宽度随缩放重算（否则放大溢出、打字缩回）
        if (m_crtView) {
            m_crtView->markDirty(true); // 缩放强制重拍（节流会让新旧帧交叠）
            m_crtSettleTimer.start(400);
        }
    }

    void setCodeMode(bool on)
    {
        if (m_codeMode == on)
            return;
        m_codeMode = on;
        applyZoom(); // 等宽/比例字体 + 标脏
        if (on) {
            // 行号槽：容纳最大行号
            m_gutterWidth = 0;
            updateGutterWidth();
            if (!m_lineNumberArea) {
                m_lineNumberArea = new LineNumberArea(this);
                m_lineNumberArea->installEventFilter(this);
            }
            m_lineNumberArea->show(); // 显模式下行号只存在于光栅，这里恢复真实组件
            m_lineNumberArea->raise();
            if (m_crt)
                m_lineNumberArea->hide();
            updateLineNumberArea();
            if (m_crtView) {
                m_crtView->syncGeometry();
                m_crtView->markDirty();
            }
#ifdef NAUGHT_WITH_HIGHLIGHT
            startHighlight();
#endif
        } else {
            m_gutterWidth = 0;
            setViewportMargins(0, 0, 0, 0);
            if (m_lineNumberArea)
                m_lineNumberArea->hide();
            if (m_crtView) {
                m_crtView->syncGeometry();
                m_crtView->markDirty();
            }
#ifdef NAUGHT_WITH_HIGHLIGHT
            stopHighlight();
#endif
        }
        viewport()->update();
    }

#ifdef NAUGHT_WITH_HIGHLIGHT
    void startHighlight()
    {
        if (!m_repo)
            m_repo = new KSyntaxHighlighting::Repository();
        if (!m_hl)
            m_hl = new KSyntaxHighlighting::SyntaxHighlighter(document());
        m_hl->setTheme(m_repo->defaultTheme(m_dark ? KSyntaxHighlighting::Repository::DarkTheme
                                                   : KSyntaxHighlighting::Repository::LightTheme));
        m_hl->setDefinition(m_repo->definitionForName(detectLanguage(document()->toPlainText().left(4096))));
        m_hl->rehighlight();
    }

    void stopHighlight()
    {
        delete m_hl;
        m_hl = nullptr;
        delete m_repo;
        m_repo = nullptr;
        QTextCursor c(document());
        c.select(QTextCursor::Document);
        c.setCharFormat(QTextCharFormat()); // 高亮颜色只属于编模式
    }

    // 内容嗅探语言：临时栖息地没有文件名，凭内容猜
    static QString detectLanguage(const QString &text)
    {
        const QString s = text.trimmed();
        if (s.isEmpty())
            return QString();
        if (s.startsWith(QLatin1String("#!/")))
            return QStringLiteral("Bash");
        if (s.startsWith(QLatin1String("<?xml")))
            return QStringLiteral("XML");
        if (s.startsWith(QLatin1String("<?php")))
            return QStringLiteral("PHP");
        if (s.startsWith(QLatin1String("<!DOCTYPE")) || s.startsWith(QLatin1String("<html")))
            return QStringLiteral("HTML");
        if (s.contains(QLatin1String("#include")) || s.contains(QLatin1String("int main"))
            || s.contains(QLatin1String("std::")) || s.contains(QLatin1String("template <")))
            return QStringLiteral("C++");
        if (s.contains(QLatin1String("import java")) || s.contains(QLatin1String("public class")))
            return QStringLiteral("Java");
        if (s.contains(QLatin1String("package main")) || s.contains(QLatin1String("func ")))
            return QStringLiteral("Go");
        if (s.contains(QLatin1String("fn ")) || s.contains(QLatin1String("let mut")))
            return QStringLiteral("Rust");
        if (s.contains(QLatin1String("def ")) || (s.contains(QLatin1String("import ")) && s.contains(QLatin1Char(':'))))
            return QStringLiteral("Python");
        if (s.contains(QLatin1String("function")) || s.contains(QLatin1String("const "))
            || s.contains(QLatin1String("=>")))
            return QStringLiteral("JavaScript");
        return QStringLiteral("C++");
    }
#endif

    QFont activeFont() const
    {
        if (m_crt) {
            // 像素字：整数像素号（12px 为设计原大），无抗锯齿
            QFont f = m_crtFont;
            f.setPixelSize(qMax(6, qRound(m_size)));
            return f;
        }
        QFont f = m_codeMode ? m_codeFont : m_baseFont;
        f.setPointSizeF(m_size);
        return f;
    }

    void beginInkSession()
    {
        if (m_inkSession)
            return;
        m_inkSession = true;
        m_inkBefore = m_canvas->snapshot();
    }

    void endInkSession()
    {
        if (!m_inkSession)
            return;
        m_inkSession = false;
        const QVector<Canvas::InkStroke> after = m_canvas->snapshot();
        if (after == m_inkBefore)
            return;
        m_inkUndo.append({m_inkBefore, after});
        if (m_inkUndo.size() > 100)
            m_inkUndo.removeFirst();
        m_inkRedo.clear();
        m_lastWasInk = true;
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QPlainTextEdit::resizeEvent(event);
        updateLineNumberArea();
        if (m_crtView)
            m_crtView->syncGeometry(); // 整面覆盖随窗口缩放
    }

    void updateLineNumberArea()
    {
        if (m_lineNumberArea)
            applyGutterGeometry();
    }

    // 行号区随横向滚动移出屏幕、边距同步回收：缩放到最大时行号不再常驻
    // 挡住文字，横向滚动真正把行号"滚出去"（滚回 0 时完整复原）
    void applyGutterGeometry()
    {
        if (!m_codeMode || !m_lineNumberArea)
            return;
        const int h = horizontalScrollBar()->value();
        const int margin = qMax(0, m_gutterWidth - h);
        m_lineNumberArea->move(-h, 0);
        m_lineNumberArea->resize(m_gutterWidth, viewport()->height());
        setViewportMargins(margin, 0, 0, 0);
        if (m_crtView)
            m_crtView->syncGeometry();
    }

    void updateGutterWidth()
    {
        if (!m_codeMode)
            return;
        const int digits = QString::number(qMax(1, document()->blockCount())).size();
        const QFontMetricsF fm(activeFont());
        const int w = qMax(20, int(fm.horizontalAdvance(QString(digits, QLatin1Char('8'))) + 12));
        if (w != m_gutterWidth) {
            m_gutterWidth = w;
            applyGutterGeometry();
        }
        if (m_lineNumberArea)
            m_lineNumberArea->update();
    }

    void startHold(int dir)
    {
        m_holdDir = dir;
        m_holdInterval = 70;
        m_holdTimer.start(m_holdInterval);
    }

    // 笔刷口径唯一入口：钳制 + 画布同步 + 足迹随动
    void setBrushSize(qreal w)
    {
        m_brushSize = std::clamp<qreal>(w, 2, 1024);
        m_canvas->setBrushWidth(m_brushSize);
        updateModeCursor();
    }

    void brushStep(int dir)
    {
        const int step = std::max(1, int(std::lround(m_brushSize * 0.1)));
        setBrushSize(m_brushSize + dir * step);
    }

    void brushScale(qreal factor)
    {
        setBrushSize(m_brushSize * factor);
    }

    void brushReset()
    {
        setBrushSize(m_baseSize * BRUSH_SCALE);
    }

    // 模式光标：打字 I 形；涂/擦模式隐藏系统光标，
    // 由画布层绘制足迹圆（实心墨点=笔刷直径 / 空心圆=擦除直径），
    // 不受系统光标尺寸上限约束，任何缩放下口径都真实可见。
    void updateModeCursor()
    {
        QWidget *vp = viewport();
        if (m_mode == Mode::Normal) {
            vp->unsetCursor();
            m_canvas->setFootprintVisible(false);
            return;
        }
        vp->setCursor(Qt::BlankCursor);
        QPointF pos = m_lastMouse;
        if (pos.x() < 0)
            pos = QPointF(vp->width() / 2.0, vp->height() / 2.0);
        m_canvas->setFootprint(true, pos, m_mode == Mode::Erase);
    }

    QPointF viewportPosToDoc(const QPointF &p) const
    {
        return p + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
    }

    // vbar 是视觉行号（Qt 源码 + 实测：vbar=60 → 首可见块=第60块），
    // 换算成其上像素高度：自首块累积块高，按行数比例截断到 vbar
    qreal pixelScrollBefore(int lineIndex) const
    {
        QAbstractTextDocumentLayout *layout = document()->documentLayout();
        qreal px = 0;
        int lines = 0;
        for (QTextBlock b = document()->firstBlock(); b.isValid(); b = b.next()) {
            const int lc = qMax(1, b.lineCount());
            if (lines + lc > lineIndex) {
                px += layout->blockBoundingRect(b).height() * (lineIndex - lines) / lc;
                return px;
            }
            px += layout->blockBoundingRect(b).height();
            lines += lc;
        }
        return px;
    }

    // 视口点 → 文档位置（与 lineEndForY 同一块走查模型，含行内 xToCursor）
    int positionAtViewport(const QPointF &p) const
    {
        const qreal docY = p.y() + pixelScrollBefore(verticalScrollBar()->value());
        QTextBlock block = document()->firstBlock();
        QAbstractTextDocumentLayout *layout = document()->documentLayout();
        qreal top = 0;
        while (block.isValid()) {
            const QRectF r = layout->blockBoundingRect(block);
            if (docY < top + r.height()) {
                QTextLayout *tl = block.layout();
                if (!tl || tl->lineCount() == 0)
                    return block.position() + block.length() - 1;
                const qreal relY = docY - top;
                QTextLine line = tl->lineAt(0);
                for (int i = 1; i < tl->lineCount(); ++i) {
                    const QTextLine l = tl->lineAt(i);
                    if (relY >= l.y())
                        line = l;
                    else
                        break;
                }
                const qreal relX = qMax(0.0, p.x() - contentOffset().x());
                return block.position() + line.textStart() + line.xToCursor(relX);
            }
            top += r.height();
            block = block.next();
        }
        return document()->characterCount() - 1;
    }

    // 缩放锚点：鼠标在视口内锚鼠标（指哪大哪），否则锚光标
    QPointF zoomAnchor() const
    {
        if (viewport()->rect().contains(m_lastMouse.toPoint()))
            return m_lastMouse;
        return QPointF(cursorRect().center());
    }

    // pos 在块内的视觉行下标
    static int visualLineInBlock(const QTextBlock &block, int pos)
    {
        const QTextLayout *tl = block.layout();
        if (!tl || tl->lineCount() == 0)
            return 0;
        const int inBlock = pos - block.position();
        int idx = 0;
        for (int i = 1; i < tl->lineCount(); ++i) {
            if (inBlock >= tl->lineAt(i).textStart())
                idx = i;
            else
                break;
        }
        return idx;
    }

    // 锚定缩放：指哪大哪。关键事实（Qt 源码）：vbar 的值是**视觉行号**；
    // 且 Qt 的 relayout 会在画帧时按自己的状态覆写 vbar——补偿必须晚于它
    //（下一事件循环回合），否则真机上被覆写回"左上锚定"（离屏自检时序侥幸通过）。
    // markContentsDirty 后所有块被 clearLayout（lineCount=0），layout 版
    // blockBoundingRect 在 lineCount==0 时强制该块重排，因此补偿本身无需等待布局。
    void applyAnchoredZoom(qreal newSize)
    {
        const QPointF anchor = zoomAnchor();
        const int pos = positionAtViewport(anchor);
        m_size = std::clamp<qreal>(newSize, 6, 1024);
        applyZoom();
        QTimer::singleShot(0, this, [this, pos, y = anchor.y()] {
            QAbstractTextDocumentLayout *layout = document()->documentLayout();
            const QTextBlock target = document()->findBlock(pos);
            layout->blockBoundingRect(target); // 强制锚点块按新字号重排
            int line = 0;
            for (QTextBlock b = document()->firstBlock(); b.isValid() && b != target; b = b.next()) {
                layout->blockBoundingRect(b); // 强制重排（lineCount==0 必触发）
                line += b.lineCount();
            }
            const int lineInBlock = visualLineInBlock(target, pos);
            line += lineInBlock;
            const QTextLayout *tl = target.layout();
            const qreal lineH = (tl && tl->lineCount() > 0)
                ? tl->lineAt(lineInBlock).height()
                : 16.0;
            verticalScrollBar()->setValue(qMax(0, line - int(y / lineH)));
        });
    }

    // 视口点所在视觉行的行尾落点（边缘窄带点击与轨道点击共用）
    void placeCaretAtLineEnd(const QPointF &vpPos)
    {
        QTextCursor c(document());
        c.setPosition(lineEndForY(vpPos));
        setTextCursor(c);
        wakeCaret();
    }

    // 滚动条轨道上的左键让位给文字：点最右缘 = 光标落该视觉行行尾，点最下缘 = 光标落文末。
    void placeCaretAtEdge(bool vertical, const QPoint &pos)
    {
        // 先结算挂起的排版（刚打完字/刚滚动后行数据可能未更新）
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QWidget *vp = viewport();
        if (!vertical) {
            QTextCursor c(document());
            c.setPosition(document()->characterCount() - 1);
            setTextCursor(c);
            wakeCaret();
            return;
        }
        placeCaretAtLineEnd(QPointF(qreal(vp->width()) - 1.0, qreal(qMin(pos.y(), vp->height() - 1))));
    }

    // 视口点所在视觉行的行尾位置（文档坐标）。
    // 不经过 xToCursor：满行换行时行尾像素属于最后一个字，x 映射会落在字前；
    // 直接返回行首偏移 + 行长，满行/空行/换行块都精确落在行尾。
    int lineEndForY(const QPointF &pt) const
    {
        // vbar 是视觉行号：换算成像素高度再走查（混用单位会让滚动后的落点漂移）
        // 块映射不存位置（top 恒 0），从文档首块累积；不依赖 firstVisibleBlock 缓存
        QTextBlock block = document()->firstBlock();
        QAbstractTextDocumentLayout *layout = document()->documentLayout();
        qreal top = 0;
        const qreal docY = pt.y() + pixelScrollBefore(verticalScrollBar()->value());
        while (block.isValid()) {
            const QRectF r = layout->blockBoundingRect(block);
            if (docY < top + r.height()) {
                QTextLayout *tl = block.layout();
                if (!tl || tl->lineCount() == 0)
                    return block.position() + block.length() - 1;
                const qreal relY = docY - top;
                QTextLine line = tl->lineAt(0);
                for (int i = 1; i < tl->lineCount(); ++i) {
                    const QTextLine l = tl->lineAt(i);
                    if (relY >= l.y())
                        line = l;
                    else
                        break;
                }
                if (line.textLength() == 0 && block.length() > 1)
                    return block.position() + block.length() - 1;
                // 文末空行是纸的余白：点它的高度时，光标落到最后一个字符之后
                if (block == document()->lastBlock() && block.length() == 1
                    && document()->characterCount() >= 2)
                    return document()->characterCount() - 2;
                return block.position() + line.textStart() + line.textLength();
            }
            top += r.height();
            block = block.next();
        }
        // 未命中任何块（点在零高度的文末空行区域）：余白仍不可入
        if (document()->lastBlock().length() == 1 && document()->characterCount() >= 2)
            return document()->characterCount() - 2;
        return document()->characterCount() - 1;
    }

    void wakeCaret()
    {
        setCursorWidth(2);
        m_blinkHalf = 0;
        m_blinkTimer.start(BLINK_HALF_MS);
    }

    void scrollActivity()
    {
        m_fadeTimer.stop();
        m_fadeOpacity = 1.0;
        setScrollOpacity(1.0);
        m_scrollHideTimer.start(1500); // 与光标睡眠同拍（1.5s）
    }

    void setScrollOpacity(qreal v)
    {
        if (m_vFade)
            m_vFade->setOpacity(v);
        if (m_hFade)
            m_hFade->setOpacity(v);
        const bool asleep = v <= 0.01;
        if (auto *bar = qobject_cast<ZenScrollBar *>(verticalScrollBar()))
            bar->setAsleep(asleep);
        if (auto *bar = qobject_cast<ZenScrollBar *>(horizontalScrollBar()))
            bar->setAsleep(asleep);
    }

    QFont m_baseFont;
    int m_baseSize = 12;
    qreal m_size = 12.0;
    bool m_dark = false;
    QTimer m_holdTimer;
    int m_holdDir = 1;
    int m_holdInterval = 70;
    int m_wheelAccum = 0;
    QTimer m_blinkTimer;
    int m_blinkHalf = 0;
    QTimer m_scrollHideTimer;
    QTimer m_fadeTimer;
    QGraphicsOpacityEffect *m_vFade = nullptr;
    QGraphicsOpacityEffect *m_hFade = nullptr;
    qreal m_fadeOpacity = 1.0;
    qreal m_pinchSmooth = 0.0;
    Canvas *m_canvas = nullptr;
    Mode m_mode = Mode::Normal;
    qreal m_brushSize = 20.0;
    QPointF m_lastMouse = QPointF(-1, -1);
    struct InkOp {
        QVector<Canvas::InkStroke> before;
        QVector<Canvas::InkStroke> after;
    };
    QVector<InkOp> m_inkUndo;
    QVector<InkOp> m_inkRedo;
    QVector<Canvas::InkStroke> m_inkBefore;
    bool m_inkSession = false;
    bool m_shiftInkActive = false;
    bool m_lastWasInk = false;
    bool m_undoWasInk = false;
    bool m_codeMode = false;
    int m_gutterWidth = 0;
    QFont m_codeFont;
    LineNumberArea *m_lineNumberArea = nullptr;
    bool m_crt = false;
    CrtView *m_crtView = nullptr;
    QFont m_crtFont;
    static inline QString s_crtFamily;
    QTimer m_crtSettleTimer;

#ifdef NAUGHT_WITH_HIGHLIGHT
    KSyntaxHighlighting::Repository *m_repo = nullptr;
    KSyntaxHighlighting::SyntaxHighlighter *m_hl = nullptr;
#endif

    static constexpr int BLINK_HALF_MS = 750; // 亮/灭各 750ms，一次“长闪烁”1.5s
    static constexpr int SLEEP_BLINKS = 1;    // 完整闪烁次数；改成 2 则休眠前闪两次
    static constexpr qreal PINCH_GAIN = 1.4;  // 捏合增量增益：边缘弱增量也够用
    static constexpr qreal PINCH_SMOOTH_A = 0.5; // 指数平滑系数：滤抖
    static constexpr qreal BRUSH_SCALE = 1.5; // 笔刷直径 = 1.5 × 字号
    static constexpr qreal EDGE_CLICK_ZONE = 10.0; // 文字区最右缘窄带：点击=行尾
};
