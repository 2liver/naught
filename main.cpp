// 「無」(naught) —— 空白。打开即写，关闭即无。
// 右键 / 双指点按：摹 · 空 ── 阴 · 阳
// Ctrl/Cmd+S：摹（全选并复制）   Ctrl/Cmd+N：空（清空，可撤销）
// Ctrl/Cmd+= / -：字号缩放（按住加速，步长随字号等比增长）  Ctrl/Cmd+0：复位
// Ctrl/Cmd+滚轮、触控板捏合：缩放；触控板横向平移 / Shift+滚轮：横向滚动

#include <algorithm>
#include <cmath>
#include <functional>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineF>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
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

// 画布层：独立于文本，浮于文字之上。笔迹存文档坐标——随滚动平移、
// 不随缩放变化（每笔在落笔瞬间锁定自己的笔宽）；颜色随阴/阳；事件全部穿透。
class Editor;

// 行号区：Qt 官方 CodeEditor 模式（声明在前，定义在 Editor 之后）
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(Editor *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Editor *m_editor;
};

class Canvas : public QWidget {
public:
    struct InkStroke {
        qreal width = 0;
        QPainterPath path; // 描边后的轮廓（QPainterPathStroker）
        bool operator==(const InkStroke &o) const { return width == o.width && path == o.path; }
    };

    QVector<InkStroke> snapshot() const { return m_strokes; }

    QVector<QPainterPath> inkPaths() const
    {
        QVector<QPainterPath> paths;
        for (const InkStroke &s : m_strokes)
            paths.append(s.path);
        return paths;
    }

    void restore(const QVector<InkStroke> &strokes)
    {
        m_strokes = strokes;
        m_activePts.clear();
        update();
    }

    explicit Canvas(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
    }

    void setInk(const QColor &c)
    {
        m_ink = c;
        update();
    }

    void setBrushWidth(qreal w)
    {
        m_brush = w;
        update(); // 足迹圆随笔刷实时刷新
    }

    void setFootprint(bool visible, const QPointF &pos, bool erase)
    {
        m_fpVisible = visible;
        m_fpPos = pos;
        m_fpErase = erase;
        update();
    }

    void setFootprintVisible(bool visible)
    {
        if (m_fpVisible == visible)
            return;
        m_fpVisible = visible;
        update();
    }

    void setScrollOffset(const QPointF &o)
    {
        m_offset = o;
        update();
    }

    void beginStroke(const QPointF &docPos)
    {
        m_activePts.clear();
        m_activePts.append(docPos);
        update();
    }

    void extendStroke(const QPointF &docPos)
    {
        if (m_activePts.isEmpty())
            return;
        if (QLineF(m_activePts.last(), docPos).length() >= 2.0) {
            m_activePts.append(docPos);
            update();
        }
    }

    void endStroke()
    {
        if (m_activePts.isEmpty())
            return;
        m_strokes.append(outlineOf(m_activePts, m_brush));
        m_activePts.clear();
    }

    void clearAll()
    {
        if (m_strokes.isEmpty() && m_activePts.isEmpty())
            return;
        m_strokes.clear();
        m_activePts.clear();
        update();
    }

    // 点列 → 描边轮廓（圆头圆角，与笔刷口径一致）
    static InkStroke outlineOf(const QVector<QPointF> &pts, qreal width)
    {
        InkStroke s{width, {}};
        if (pts.size() == 1) {
            s.path.addEllipse(pts.at(0), width / 2.0, width / 2.0);
            return s;
        }
        QPainterPath line;
        line.moveTo(pts.at(0));
        for (int i = 1; i < pts.size(); ++i)
            line.lineTo(pts.at(i));
        QPainterPathStroker stroker;
        stroker.setWidth(width);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        s.path = stroker.createStroke(line);
        return s;
    }

    void eraseAt(const QPointF &c)
    {
        // 增量管段：上次点→当前点描边成管（圆帽无缝相接），拖动再快也连续
        QPainterPath tube;
        if (m_eraseActive) {
            QPainterPath seg;
            seg.moveTo(m_eraseLast);
            seg.lineTo(c);
            QPainterPathStroker stroker;
            stroker.setWidth(m_brush);
            stroker.setCapStyle(Qt::RoundCap);
            stroker.setJoinStyle(Qt::RoundJoin);
            tube = stroker.createStroke(seg);
        } else {
            tube.addEllipse(c, m_brush / 2.0, m_brush / 2.0);
        }
        m_eraseLast = c;
        m_eraseActive = true;
        applyErase(tube);
    }

    void eraseEnd()
    {
        m_eraseActive = false;
    }

    void applyErase(const QPainterPath &tube)
    {
        // Qt 自带几何引擎：轮廓布尔相减，所见即所得，小口径也精确
        bool changed = false;
        for (int i = m_strokes.size() - 1; i >= 0; --i) {
            const qreal width = m_strokes.at(i).width;
            const QPainterPath before = m_strokes.at(i).path;
            const QPainterPath after = before.subtracted(tube);
            if (after == before)
                continue;
            changed = true;
            const QVector<QPainterPath> subs = splitSubpaths(after);
            m_strokes.removeAt(i);
            for (int k = subs.size() - 1; k >= 0; --k)
                m_strokes.insert(i, InkStroke{width, subs.at(k)});
        }
        if (changed)
            update();
    }

    // 保留曲线元素地把路径拆成子路径
    static QVector<QPainterPath> splitSubpaths(const QPainterPath &p)
    {
        QVector<QPainterPath> out;
        QPainterPath cur;
        for (int i = 0; i < p.elementCount(); ++i) {
            const QPainterPath::Element &e = p.elementAt(i);
            if (e.isMoveTo()) {
                if (cur.elementCount() > 0)
                    out.append(cur);
                cur = QPainterPath();
                cur.moveTo(e.x, e.y);
            } else if (e.isLineTo()) {
                cur.lineTo(e.x, e.y);
            } else if (e.isCurveTo() && i + 2 < p.elementCount()) {
                cur.cubicTo(e.x, e.y, p.elementAt(i + 1).x, p.elementAt(i + 1).y,
                            p.elementAt(i + 2).x, p.elementAt(i + 2).y);
                i += 2;
            }
        }
        if (cur.elementCount() > 0)
            out.append(cur);
        return out;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QWidget *par = parentWidget();
        if (auto *area = qobject_cast<QAbstractScrollArea *>(par))
            m_vpOffset = area->viewport()->pos();
        if (par && size() != par->size())
            setGeometry(par->rect());

        QPainter p(this);
        if (!p.isActive())
            return;
        p.setRenderHint(QPainter::Antialiasing);
        p.save();
        p.translate(QPointF(m_vpOffset) - m_offset); // 笔迹：文档坐标（随滚动）
        for (const InkStroke &s : m_strokes) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_ink);
            p.drawPath(s.path);
        }
        drawStroke(p, m_activePts, m_brush);
        p.restore();

        // 画笔足迹：窗口坐标（不随滚动）。涂=实心墨点，擦=空心圆；
        // 两者外缘均等于笔刷直径，擦除半径 0.5×笔刷，口径一致。
        const QPointF fp = m_fpPos + QPointF(m_vpOffset);
        if (m_fpVisible) {
            const qreal d = m_brush;
            if (m_fpErase) {
                const qreal stroke = std::clamp<qreal>(d * 0.08, 1.5, 8.0);
                p.setPen(QPen(m_ink, stroke));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(fp, d / 2 - stroke / 2, d / 2 - stroke / 2);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(m_ink);
                p.drawEllipse(fp, d / 2, d / 2);
            }
        }
    }

private:
    void drawStroke(QPainter &p, const QVector<QPointF> &pts, qreal width) const
    {
        if (pts.isEmpty())
            return;
        if (pts.size() == 1) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_ink);
            p.drawEllipse(pts.at(0), width / 2.0, width / 2.0);
            return;
        }
        QPainterPath path;
        path.moveTo(pts.at(0));
        for (int i = 1; i < pts.size(); ++i)
            path.lineTo(pts.at(i));
        QPen pen(m_ink, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    QColor m_ink = QColor(0, 0, 0);
    qreal m_brush = 20.0;
    QPointF m_offset;
    QPoint m_vpOffset;
    QVector<InkStroke> m_strokes;
    QVector<QPointF> m_activePts;
    QPointF m_eraseLast;
    bool m_eraseActive = false;
    bool m_fpVisible = false;
    bool m_fpErase = false;
    QPointF m_fpPos;
};

// 自绘滚动条：命中区恒为 18px（从任何一侧都容易接近），
// 闲置时把手 10px、悬停时长满 18px。QSS 无法控制把手宽度（实测），故自绘。
class ZenScrollBar : public QScrollBar {
    Q_OBJECT
public:
    using QScrollBar::QScrollBar;

    void setDark(bool dark)
    {
        if (m_dark == dark)
            return;
        m_dark = dark;
        update();
    }

    void setAsleep(bool a)
    {
        if (m_asleep == a)
            return;
        m_asleep = a;
    }

signals:
    void hovered(bool on);
    void trackClicked(QPoint pos);

protected:
    void enterEvent(QEnterEvent *event) override
    {
        if (!m_hover) {
            m_hover = true;
            update();
        }
        emit hovered(true);
        QScrollBar::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (m_hover) {
            m_hover = false;
            update();
        }
        QScrollBar::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        // 睡着（已淡出）的滚动条完全让位给文字：任何左键都落光标。
        // 醒着时只有轨道让位，把手仍可拖动滚动。
        if (event->button() == Qt::LeftButton
            && (m_asleep || !handleRect().contains(event->position().toPoint()))) {
            emit trackClicked(event->position().toPoint());
            return;
        }
        QScrollBar::mousePressEvent(event);
    }

    QRect handleRect() const
    {
        const bool vert = orientation() == Qt::Vertical;
        const QRect t = rect();
        const int L = vert ? t.height() : t.width();
        const int range = maximum() - minimum();
        const int page = pageStep();
        const qreal total = range + page;
        if (total <= 0)
            return QRect();
        const int sliderLen = qMax(32, int(L * page / total));
        const int pos = int((L - sliderLen) * (value() - minimum()) / qMax(1, range));
        const int thick = m_hover ? 18 : 10;
        if (vert)
            return QRect(t.right() - thick + 1, pos, thick, sliderLen);
        return QRect(pos, t.bottom() - thick + 1, sliderLen, thick);
    }

    void paintEvent(QPaintEvent *) override
    {
        const QRect h = handleRect();
        if (h.isEmpty())
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(m_dark ? QColor(0x4a, 0x4a, 0x4a) : QColor(0xb8, 0xb8, 0xb8));
        const int rad = qMin(h.width(), h.height()) / 2;
        p.drawRoundedRect(h, rad, rad);
    }

private:
    bool m_dark = false;
    bool m_hover = false;
    bool m_asleep = false;
};

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
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
            m_canvas->setScrollOffset(QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value()));
        });
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
            m_canvas->setScrollOffset(QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value()));
        });

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

    void toggleCodeMode()
    {
        setCodeMode(!m_codeMode);
        updateModeCursor();
    }

    void zoom(int delta)
    {
        m_size = std::clamp<qreal>(m_size + delta, 6, 1024);
        applyZoom();
    }

    void zoomTo(qreal size)
    {
        m_size = std::clamp<qreal>(size, 6, 1024);
        applyZoom();
    }

    void zoomReset()
    {
        m_size = m_baseSize;
        applyZoom();
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
        // 像素级对齐验证：把编模式渲染成图像，比较数字与文字的像素行范围
        {
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
            const auto num = darkRange(2, qMax(3, g - 2), 0, 60);
            const auto txt = darkRange(g + 4, g + 80, 0, 60);
            qInfo("PIXEL gutter=%d num_y=[%d,%d] text_y=[%d,%d]", g,
                  num.first, num.second, txt.first, txt.second);
            // 离屏与真机的字体度量取向相反（真机才作数），此项仅诊断输出
            if (num.first >= 0 && txt.first >= 0)
                qInfo("PIXEL-CHECK number/text top: %d vs %d", num.first, txt.first);
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

        menu.exec(event->globalPos());
    }

    void focusInEvent(QFocusEvent *event) override
    {
        wakeCaret();
        QPlainTextEdit::focusInEvent(event);
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
            Q_UNUSED(posOf);
            // 记录指针位置（画笔足迹用，所有模式都跟踪）
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
                    QTextCursor c(document());
                    c.setPosition(lineEndForY(posOf(me)));
                    setTextCursor(c);
                    wakeCaret();
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
        if (m_dark) {
            pal.setColor(QPalette::Window, QColor(0, 0, 0));
            pal.setColor(QPalette::Base, QColor(0, 0, 0));
            pal.setColor(QPalette::Text, QColor(255, 255, 255));
        } else {
            pal.setColor(QPalette::Window, QColor(255, 255, 255));
            pal.setColor(QPalette::Base, QColor(255, 255, 255));
            pal.setColor(QPalette::Text, QColor(0, 0, 0));
        }
        setPalette(pal);

        if (auto *v = qobject_cast<ZenScrollBar *>(verticalScrollBar()))
            v->setDark(m_dark);
        if (auto *h = qobject_cast<ZenScrollBar *>(horizontalScrollBar()))
            h->setDark(m_dark);
        if (m_canvas)
            m_canvas->setInk(m_dark ? QColor(255, 255, 255) : QColor(0, 0, 0));
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
        document()->markContentsDirty(0, document()->characterCount());
        setFont(f);
        // 笔刷与字号脱钩：只由 Cmd/Ctrl+Shift+= / - / 0 控制
        updateGutterWidth(); // 行号区宽度随缩放重算（否则放大溢出、打字缩回）
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
            m_lineNumberArea->show();
            m_lineNumberArea->raise();
            updateLineNumberArea();
#ifdef NAUGHT_WITH_HIGHLIGHT
            startHighlight();
#endif
        } else {
            m_gutterWidth = 0;
            setViewportMargins(0, 0, 0, 0);
            if (m_lineNumberArea)
                m_lineNumberArea->hide();
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
    }

    void updateLineNumberArea()
    {
        if (m_lineNumberArea)
            m_lineNumberArea->setGeometry(0, 0, m_gutterWidth, viewport()->height());
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
            setViewportMargins(m_gutterWidth, 0, 0, 0);
            updateLineNumberArea();
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

    void brushStep(int dir)
    {
        const int step = std::max(1, int(std::lround(m_brushSize * 0.1)));
        m_brushSize = std::clamp<qreal>(m_brushSize + dir * step, 2, 1024);
        m_canvas->setBrushWidth(m_brushSize);
        if (m_mode == Mode::Draw || m_mode == Mode::Erase)
            updateModeCursor();
    }

    void brushScale(qreal factor)
    {
        m_brushSize = std::clamp<qreal>(m_brushSize * factor, 2, 1024);
        m_canvas->setBrushWidth(m_brushSize);
        updateModeCursor(); // 足迹随动
    }

    void brushReset()
    {
        m_brushSize = m_baseSize * BRUSH_SCALE;
        m_canvas->setBrushWidth(m_brushSize);
        if (m_mode == Mode::Draw || m_mode == Mode::Erase)
            updateModeCursor();
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
        const QPointF vpPos(qreal(vp->width()) - 1.0, qreal(qMin(pos.y(), vp->height() - 1)));
        QTextCursor c(document());
        c.setPosition(lineEndForY(vpPos));
        setTextCursor(c);
        wakeCaret();
    }

    // 视口点所在视觉行的行尾位置（文档坐标）。
    // 不经过 xToCursor：满行换行时行尾像素属于最后一个字，x 映射会落在字前；
    // 直接返回行首偏移 + 行长，满行/空行/换行块都精确落在行尾。
    int lineEndForY(const QPointF &pt) const
    {
        const int vbar = verticalScrollBar()->value();
        // 块映射不存位置（top 恒 0），从文档首块累积；不依赖 firstVisibleBlock 缓存
        QTextBlock block = document()->firstBlock();
        QAbstractTextDocumentLayout *layout = document()->documentLayout();
        qreal top = 0;
        const qreal docY = pt.y() + vbar;
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

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("無"));
    app.setApplicationDisplayName(QStringLiteral("無"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/naught.png")));
    app.setCursorFlashTime(0); // 关闭原生闪烁器：闪烁与休眠由 Editor 自驱，保证完整对称无残拍

    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--selftest"))
        return Editor::selftest() ? 0 : 1;

    Editor editor;
    editor.setWindowTitle(QString());
    editor.resize(900, 600);
    editor.show();

#ifdef Q_OS_MACOS
    // 系统菜单栏上的「法」：快捷键说明随原生 key equivalent 显示。
    // Windows/Linux 不设菜单栏（无），其右键菜单为 Qt 自绘、自带快捷键列。
    {
        QMenuBar *menuBar = new QMenuBar(nullptr);
        QMenu *fa = menuBar->addMenu(QStringLiteral("项"));
        QAction *bMo = fa->addAction(QStringLiteral("摹"));
        bMo->setShortcut(QKeySequence(QStringLiteral("Ctrl+S")));
        QAction *bKong = fa->addAction(QStringLiteral("空"));
        bKong->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
        fa->addSeparator();
        QAction *bYin = fa->addAction(QStringLiteral("阴"));
        QAction *bYang = fa->addAction(QStringLiteral("阳"));
        bYin->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
        bYang->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
        bYin->setCheckable(true);
        bYang->setCheckable(true);
        fa->addSeparator();
        QAction *bTu = fa->addAction(QStringLiteral("涂"));
        bTu->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
        QAction *bCa = fa->addAction(QStringLiteral("擦"));
        bCa->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
        QAction *bXiao = fa->addAction(QStringLiteral("消"));
        bXiao->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
        bTu->setCheckable(true);
        bCa->setCheckable(true);
        QAction *bHold = fa->addAction(QStringLiteral("按住 Shift 拖动＝按住笔刷"));
        bHold->setEnabled(false);
        fa->addSeparator();
        QAction *bBian = fa->addAction(QStringLiteral("编"));
        bBian->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
        bBian->setCheckable(true);
        fa->addSeparator();
        // 字号/笔刷不做成真键等效（系统接管会毁掉按住加速），提示内嵌标签
        QAction *bZoomIn = fa->addAction(QStringLiteral("字号放大 ⌘="));
        QAction *bZoomOut = fa->addAction(QStringLiteral("字号缩小 ⌘-"));
        QAction *bZoom0 = fa->addAction(QStringLiteral("字号复位 ⌘0"));
        QAction *bBrushIn = fa->addAction(QStringLiteral("笔刷加粗 ⇧⌘="));
        QAction *bBrushOut = fa->addAction(QStringLiteral("笔刷变细 ⇧⌘-"));
        QAction *bBrush0 = fa->addAction(QStringLiteral("笔刷复位 ⇧⌘0"));
        fa->addSeparator();
        QAction *bUndo = fa->addAction(QStringLiteral("撤销"));
        bUndo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
        QAction *bRedo = fa->addAction(QStringLiteral("重做"));
        bRedo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Y")));
        QObject::connect(bMo, &QAction::triggered, &editor, [&editor] { editor.mo(); });
        QObject::connect(bKong, &QAction::triggered, &editor, [&editor] { editor.kong(); });
        QObject::connect(bYin, &QAction::triggered, &editor, [&editor] { editor.setDark(true); });
        QObject::connect(bYang, &QAction::triggered, &editor, [&editor] { editor.setDark(false); });
        QObject::connect(bTu, &QAction::triggered, &editor, [&editor] { editor.toggleMode(Editor::Mode::Draw); });
        QObject::connect(bCa, &QAction::triggered, &editor, [&editor] { editor.toggleMode(Editor::Mode::Erase); });
        QObject::connect(bXiao, &QAction::triggered, &editor, [&editor] { editor.clearInk(); });
        QObject::connect(bBian, &QAction::triggered, &editor, [&editor] { editor.toggleCodeMode(); });
        QObject::connect(bZoomIn, &QAction::triggered, &editor, [&editor] { editor.zoom(1); });
        QObject::connect(bZoomOut, &QAction::triggered, &editor, [&editor] { editor.zoom(-1); });
        QObject::connect(bZoom0, &QAction::triggered, &editor, [&editor] { editor.zoomReset(); });
        QObject::connect(bBrushIn, &QAction::triggered, &editor, [&editor] { editor.brushUp(); });
        QObject::connect(bBrushOut, &QAction::triggered, &editor, [&editor] { editor.brushDown(); });
        QObject::connect(bBrush0, &QAction::triggered, &editor, [&editor] { editor.brushDefault(); });
        QObject::connect(bUndo, &QAction::triggered, &editor, [&editor] { editor.undoAll(); });
        QObject::connect(bRedo, &QAction::triggered, &editor, [&editor] { editor.redoAll(); });
        QObject::connect(fa, &QMenu::aboutToShow, &editor, [&editor, bYin, bYang, bTu, bCa, bBian] {
            bYin->setChecked(editor.isDark());
            bYang->setChecked(!editor.isDark());
            bTu->setChecked(editor.mode() == Editor::Mode::Draw);
            bCa->setChecked(editor.mode() == Editor::Mode::Erase);
            bBian->setChecked(editor.codeMode());
        });
    }
#endif

    return app.exec();
}

// LineNumberArea 定义：独立子控件，用 blockBoundingGeometry 与 contentOffset
// 走查，与文字共享同一几何，对齐由构造保证
LineNumberArea::LineNumberArea(Editor *editor)
    : QWidget(editor)
    , m_editor(editor)
{
    // 视觉透明（无背景填充），但接收鼠标事件：过滤器会拦截并换算坐标，
    // 使足迹与笔迹可以在行号区上作画
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setMouseTracking(true); // 无按键移动也派发（Shift 拖动路径依赖）
}

QSize LineNumberArea::sizeHint() const
{
    return QSize(m_editor->gutterWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setPen(m_editor->isDark() ? QColor(0x6a, 0x6a, 0x6a) : QColor(0xb0, 0xb0, 0xb0));
    painter.setFont(m_editor->codeFont());
    // 光学对齐：数字无升部，相对小写文字略下沉 (cap-x)/2
    const QFontMetricsF fm(painter.font());
    const int optical = int((fm.capHeight() - fm.xHeight()) / 2.0);

    QTextBlock block = m_editor->firstVisibleBlockPub();
    int blockNumber = block.blockNumber();
    qreal top = m_editor->blockBoundingGeometryPub(block)
                    .translated(m_editor->contentOffsetPub())
                    .top();
    qreal bottom = top + m_editor->blockBoundingRectPub(block).height();
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.drawText(0, int(top) + optical, width() - 6,
                             m_editor->fontMetrics().height(), Qt::AlignRight,
                             QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + m_editor->blockBoundingRectPub(block).height();
        ++blockNumber;
    }
}

#include "main.moc"
