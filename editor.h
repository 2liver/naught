// editor.h —— 编辑器：文字 + 工具（涂/擦）+ 视图（编）+ 光标/滚动条行为 + 自检。
#pragma once

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPointF>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

#ifdef NAUGHT_WITH_HIGHLIGHT
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif

#include "ascii_art.h"
#include "canvas.h"
#include "crt.h"
#include "crt_view.h"
#include "line_number_area.h"
#include "zen_scroll_bar.h"

class Editor : public QPlainTextEdit, public CrtSource {
public:
    // CrtSource 几何（窄接口）：
    QRect sourceRect() const override { return rect(); }
    QSize sourceViewportSize() const override { return viewport() ? viewport()->size() : QSize(); }
    QWidget *sourceWidget() const override { return const_cast<Editor *>(this); }
    Editor()
        : QPlainTextEdit()
        , m_holdTimer(this)
    {
        setFrameShape(QFrame::NoFrame);
        setTabChangesFocus(true);
        setAcceptDrops(true); // M3：拖图片 → 字符画（隐藏功能）
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        m_baseFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        m_codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        m_baseSize = m_baseFont.pointSize();
        if (m_baseSize <= 0)
            m_baseSize = 12;
        m_size = m_baseSize;

        // 出厂磷光字体：随 qrc 捆绑（均 OFL），一次加载终身可用——用户
        // 删光字体文件夹也不影响机器开机（出厂字体在应用包里，删不掉）
        if (s_crtFamily.isEmpty()) {
            const int id = QFontDatabase::addApplicationFont(
                QStringLiteral(":/fonts/fusion-pixel-12px-monospaced-zh_hans.ttf"));
            if (id >= 0 && !QFontDatabase::applicationFontFamilies(id).isEmpty())
                s_crtFamily = QFontDatabase::applicationFontFamilies(id).first();
        }
        if (s_greenFamily.isEmpty()) {
            const int id = QFontDatabase::addApplicationFont(
                QStringLiteral(":/fonts/VT323-Regular.ttf"));
            if (id >= 0 && !QFontDatabase::applicationFontFamilies(id).isEmpty())
                s_greenFamily = QFontDatabase::applicationFontFamilies(id).first();
        }
        if (s_whiteFamily.isEmpty()) {
            const int id = QFontDatabase::addApplicationFont(
                QStringLiteral(":/fonts/classic/FSEX302.ttf"));
            if (id >= 0 && !QFontDatabase::applicationFontFamilies(id).isEmpty())
                s_whiteFamily = QFontDatabase::applicationFontFamilies(id).first();
        }
        if (s_c64Family.isEmpty()) {
            // C64 出厂字体：Press Start 2P（OFL）——8 位像素观感，
            // 独立于绿磷机的 VT323（旧版回退到绿磷字体，两台同脸）
            const int id = QFontDatabase::addApplicationFont(
                QStringLiteral(":/fonts/classic/PressStart2P-Regular.ttf"));
            if (id >= 0 && !QFontDatabase::applicationFontFamilies(id).isEmpty())
                s_c64Family = QFontDatabase::applicationFontFamilies(id).first();
        }
        seedClassicFonts(); // 出厂经典库存（首次启动播种，可删可改名）
        refreshFonts(); // 用户字体文件夹（可含嵌套子文件夹）
        // 暂时默认：跨启动记忆上次选用的字体（按机器；字体被删则回出厂）
        {
            QSettings st;
            const QString famA = st.value(QStringLiteral("fontAmber")).toString();
            if (!famA.isEmpty() && m_userFonts.contains(famA))
                m_amberUser = famA;
            const QString famG = st.value(QStringLiteral("fontGreen")).toString();
            if (!famG.isEmpty() && m_userFonts.contains(famG))
                m_greenUser = famG;
            const QString famA2 = st.value(QStringLiteral("fontC64")).toString();
            if (!famA2.isEmpty() && m_userFonts.contains(famA2))
                m_c64User = famA2;
        }
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

        // M3：画布缩放尾拍兜底（捏合事件高频，停止后补渲染最后一拍）
        m_asciiSettleTimer.setSingleShot(true);
        connect(&m_asciiSettleTimer, &QTimer::timeout, this, [this] {
            if (m_asciiActive)
                replaceAsciiArt();
        });

        // M3：字符画逐行打印（打字机效果——每行插入触发磷粉激发）
        m_asciiPrintTimer.setInterval(35);
        connect(&m_asciiPrintTimer, &QTimer::timeout, this, [this] {
            if (!m_asciiPrinting)
                return;
            if (m_asciiPrintIdx >= m_asciiPrintLines.size()) {
                m_asciiPrintTimer.stop();
                m_asciiPrinting = false;
                endAsciiEditBlock(); // 整轮移除+重印 = 一步撤销（防逐行残步）
                m_settingAscii = false;
                // 打印结束：CRT 一次性追拍（打印期间已解耦，见下）
                if (m_crtView) {
                    m_crtView->markDirty();
                    m_crtSettleTimer.start(400);
                }
                return;
            }
            QTextCursor c = textCursor();
            c.setPosition(m_asciiPrintPos);
            m_settingAscii = true;
            // 行块打印：每拍插入多行 = 每拍一次重排——总拍数 ~40、
            // 间隔 40ms → 总时长 ~1.6s。旧版逐行插入：文档重排成本
            // ×行数，全屏 200+ 行时 20-30 秒
            const int end = qMin(m_asciiPrintIdx + m_asciiLinesPerTick,
                                 m_asciiPrintLines.size());
            if (m_asciiColors.isEmpty()) {
                QStringList chunk;
                for (int i = m_asciiPrintIdx; i < end; ++i)
                    chunk.append(m_asciiPrintLines.at(i));
                QString txt = chunk.join(QLatin1Char('\n'));
                if (end < m_asciiPrintLines.size())
                    txt += QLatin1Char('\n');
                c.insertText(txt);
            } else {
                for (int i = m_asciiPrintIdx; i < end; ++i) {
                    insertColoredLine(c, i); // C64 真彩：逐字符前景色
                    if (i + 1 < m_asciiPrintLines.size())
                        c.insertText(QStringLiteral("\n"));
                }
            }
            m_settingAscii = false;
            m_asciiPrintPos = c.position();
            m_asciiEnd = m_asciiPrintPos;
            m_lastArtStart = m_asciiStart; // 打印推进时同步"上次范围"
            m_lastArtEnd = m_asciiEnd;
            m_asciiPrintIdx = end;
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
        m_blinkTimer.setSingleShot(true);
        connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
            if (++m_blinkHalf >= SLEEP_BLINKS * 2) {
                m_blinkTimer.stop(); // 第 N 次闪烁的“灭”拍即休眠
                syncNativeCaretWidth();
                return;
            }
            syncNativeCaretWidth();
            if (m_crtView)
                m_crtView->markDirty(); // 脏驱动下块光标闪烁需显式标脏
        });
        // P3：光标移动也纳入脏区（旧位置的块光标必须被擦掉——
        // 否则增量快照留下幽灵光标；箭头键移动不触发 contentsChange）
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
            if (!m_crt)
                return;
            const int halo = qCeil(fontMetrics().horizontalAdvance(QLatin1Char('M'))) + 12;
            m_snapDirty |= m_lastCursorRect;
            m_lastCursorRect = cursorRect().translated(viewport()->pos())
                                   .adjusted(-halo, -halo, halo, halo);
            m_snapDirty |= m_lastCursorRect;
        });
        connect(document(), &QTextDocument::contentsChange, this,
                [this](int from, int removed, int added) {
            // 只有真实文字变化（插入/删除）才算"手动编辑"；
            // 格式变化（高亮器 rehighlight 的同步/异步 chunk）不杀画布——
            // 否则打印期间连按 Cmd+B：高亮器在切编之后异步上色，
            // contentsChanged(0,0,0) 误触发反激活 → 打印被杀、画布被吞。
            const bool textChange = removed > 0 || added > 0;
            if (!textChange) {
                if (m_crtView) {
                    m_crtView->markDirty();
                    m_crtSettleTimer.start(400);
                }
                return;
            }
            wakeCaret();
            m_lastWasInk = false;
            m_undoWasInk = false; // 双栈撤销：文字变化后重做走文档栈（不再误重做笔迹）
            // M3：手动编辑 = 字符画回归普通文本（程序打印/替换不受影响）
            if (!m_settingAscii && m_asciiActive) {
                m_asciiActive = false;
                m_asciiPrintTimer.stop(); // 打断逐行打印
                m_asciiPrinting = false;
                endAsciiEditBlock(); // 打印块收口：不得并入用户的编辑
                setLineWrapMode(QPlainTextEdit::WidgetWidth);
                // 剥掉 C64 逐字符前景色：反激活 = 回归普通文本。否则换机时
                // （画布不在场不重印）真彩跟着文字走到琥珀/绿磷机上
                QTextCursor fc(document());
                fc.select(QTextCursor::Document);
                QTextCharFormat plain;
                plain.setForeground(QBrush()); // 无效画刷：合并即清除前景
                fc.setCharFormat(plain);
            }
            // 磷粉激发（二期三件套·回接）：插入的新字符记下位置与时刻，
            // 快照在 ~900ms 内给它画三圈软边增亮（指数回落）
            const int cc = document()->characterCount();
            // 只有真实打字激发辉光；程序性重印（换机/缩放/Cmd+0）批量
            // 插入不激发——否则整屏字符画逐拍叠加 = 过曝闪光（用户：
            // "拍立得闪光灯怼脸"）
            if (!m_settingAscii && cc > m_lastCharCount && textCursor().position() > 0) {
                m_excitePos = textCursor().position() - 1;
                m_exciteClock.start();
            }
            m_lastCharCount = cc;
            // P3：增量脏区 = 变化块 + 其下方全部。任何编辑（尤其删除/换行）
            // 都会让下方内容整体位移——只标变化块会让旧内容与新内容叠加
            // 成亮斑（重印过曝、按住删字行下方亮得一塌糊涂的真凶）
            {
                QTextBlock blk = document()->findBlock(qMin(from, qMax(0, cc - 1)));
                const QTextBlock endBlk = document()->findBlock(
                    qMin(from + qMax(added, removed), qMax(0, cc - 1)));
                QRect firstR;
                for (;;) {
                    QRect r = blockBoundingGeometryPub(blk)
                                  .translated(contentOffsetPub()).toAlignedRect()
                                  .translated(viewport()->pos());
                    r = r.intersected(viewport()->rect().translated(viewport()->pos()));
                    m_snapDirty |= r;
                    if (firstR.isNull() && !r.isNull())
                        firstR = r;
                    if (blk == endBlk)
                        break;
                    blk = blk.next();
                }
                // 下方至视口底全宽纳入：位移区域
                if (!firstR.isNull()) {
                    QRect below = viewport()->rect().translated(viewport()->pos());
                    below.setTop(firstR.top());
                    m_snapDirty |= below;
                }
                m_snapDirty |= m_lastCursorRect;
                // 激发辉光（cell ±6px 三圈）超出光标矩形——脏区扩展覆盖
                const int halo = qCeil(fontMetrics().horizontalAdvance(QLatin1Char('M'))) + 12;
                m_lastCursorRect = cursorRect().translated(viewport()->pos())
                                       .adjusted(-halo, -halo, halo, halo);
                m_snapDirty |= m_lastCursorRect;
            }
            // 行号区：文档一变立即重绘，否则清空/换行不会刷新（假行号）
            m_canvas->update();
            updateGutterWidth();
            // 打印期间不解耦的话：每拍标脏 → CRT 全屏快照（余晖+辉光，
            // CPU 大户）霸占主线程 → 打印拍被饿死（全屏 20-30s 的元凶）。
            // 打印中跳过，收尾拍（打印定时器末拍）一次性追拍
            if (m_crtView && !m_asciiPrinting) {
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
            if (m_crtView)
                m_crtView->markDirty(); // 滚动条淡出需逐拍重拍
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
        // 滚动态：滚动期间 CRT 快照跳过余晖+辉光重活（卡顿大户），
        // 停稳后半拍补全量——滚动手感优先，痕迹自愈
        m_scrollSettle.setSingleShot(true);
        connect(&m_scrollSettle, &QTimer::timeout, this, [this] {
            m_scrolling = false;
            if (m_crtView)
                m_crtView->markDirty(true);
        });
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
            m_scrolling = true;
            markSnapshotFullDirty(); // 视口内容整体位移：增量不适用
            m_scrollSettle.start(180);
        });
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
            m_scrolling = true;
            markSnapshotFullDirty();
            m_scrollSettle.start(180);
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
        endAsciiEditBlock(); // 全清自成一步撤销（不并入画布打印块）
        QTextCursor cursor(document());
        cursor.select(QTextCursor::Document);
        cursor.removeSelectedText();
    }

    void setDark(bool dark)
    {
        if (m_dark == dark)
            return;
        m_dark = dark;
        invalidateFullSnapshot(); // 配色全变：全量 + 唤醒渲染环
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

    // ---- 格式化库（自 AsciiTools 移植：框/压行/路径树）+ 居中 ----
    // 原子尾部：一步撤销替换 [start,end) → out，整段保持选中（链式）。
    // 六个格式化操作共用——Q1 审查提取
    void replaceAndReselect(int start, int end, const QString &out)
    {
        QTextCursor c = textCursor();
        c.beginEditBlock();
        c.setPosition(start);
        c.setPosition(end, QTextCursor::KeepAnchor);
        c.insertText(out);
        c.endEditBlock();
        c.setPosition(start);
        c.setPosition(start + out.size(), QTextCursor::KeepAnchor);
        setTextCursor(c);
        wakeCaret();
    }
    // 范围解析（两变体）：选区或全文 / 选区或当前行
    struct TextRange {
        int start;
        int end;
    };
    TextRange selectionOrDoc() const
    {
        QTextCursor c = textCursor();
        if (c.hasSelection())
            return { c.selectionStart(), c.selectionEnd() };
        const QTextBlock lastBlk = document()->lastBlock();
        return { 0, lastBlk.position() + qMax(0, lastBlk.length() - 1) };
    }
    void selectionOrLine(QTextBlock &first, QTextBlock &last) const
    {
        QTextCursor c = textCursor();
        first = document()->findBlock(c.selectionStart());
        last = document()->findBlock(c.selectionEnd());
        if (last.position() == c.selectionEnd() && last != first)
            last = last.previous(); // 选区恰在行首结束：上一行才是最后受影响行
    }
    // CJK 等宽字符按 2 格计算（与 AsciiTools 同约定，保证对齐）
    static int displayWidth(const QString &s)
    {
        int w = 0;
        for (QChar ch : s) {
            const ushort cp = ch.unicode();
            w += (cp > 0x2E80 && cp != 0x303F) ? 2 : 1;
        }
        return w;
    }
    // 框：选区（或当前行）加框线图——单线/双线/圆角/粗线。整框保持选中
    // （链式操作）；一步撤销
    void formatBox(int style)
    {
        QTextDocument *doc = document();
        if (doc->isEmpty())
            return;
        QTextBlock first, last;
        selectionOrLine(first, last);
        QStringList lines;
        for (QTextBlock b = first;; b = b.next()) {
            lines.append(b.text());
            if (b == last)
                break;
        }
        // 按字体真实推进像素级对齐：框宽 = 最长行宽 + 左右各一格空白。
        // 旧版用 CJK=2 的字符格计数——与字体实际推进不符时右边
        // 多出一大截（用户报）。这里每字符量推进、空格与框线也量推进，
        // 任何字体/混排都严丝合缝。
        const QFontMetricsF fm(activeFont());
        const qreal spw = qMax(0.1, fm.horizontalAdvance(QLatin1Char(' ')));
        const auto lineW = [&](const QString &t) {
            qreal w = 0.0;
            for (QChar ch : t)
                w += fm.horizontalAdvance(ch);
            return w;
        };
        qreal maxW = 0.0;
        for (const QString &l : lines)
            maxW = qMax(maxW, lineW(l));
        const struct {
            QChar tl, tr, bl, br, h, v;
        } st[4] = {
            {QChar(0x250C), QChar(0x2510), QChar(0x2514), QChar(0x2518),
             QChar(0x2500), QChar(0x2502)}, // 单线
            {QChar(0x2554), QChar(0x2557), QChar(0x255A), QChar(0x255D),
             QChar(0x2550), QChar(0x2551)}, // 双线
            {QChar(0x256D), QChar(0x256E), QChar(0x2570), QChar(0x256F),
             QChar(0x2500), QChar(0x2502)}, // 圆角
            {QChar(0x250F), QChar(0x2513), QChar(0x2517), QChar(0x251B),
             QChar(0x2501), QChar(0x2503)}, // 粗线
        };
        const auto &s = st[qBound(0, style, 3)]; // 防御：菜单只传 0-3
        // 笔画中心（ink 范围中点）：fallback 框线字形宽窄不一，
        // 按字面推进对齐必然错位——必须按笔画中心对齐
        const auto stemC = [&fm](QChar ch) {
            return fm.leftBearing(ch)
                 + (fm.horizontalAdvance(ch) - fm.leftBearing(ch)
                    - fm.rightBearing(ch)) / 2.0;
        };
        // 选竖线字形：候选里挑笔画中心最接近两角平均的——左右竖线
        // 与角的 stem 一致，四边才连得起来（不同机器 fallback 各异）。
        // 本样式的原生竖线（双线框的 ║ 等）权重最高——只有 stem 偏差
        // 超过半格才换字形（双线框的"双线"不可被单线替换）
        const QChar vCand[4] = { s.v, QChar(0x2502), QChar(0x2503), QChar(0x250B) };
        QChar vBest = s.v;
        const qreal cornerStem = (stemC(s.tl) + stemC(s.tr)) / 2.0;
        const qreal nativeErr = qAbs(stemC(s.v) - cornerStem);
        qreal bestErr = nativeErr;
        if (nativeErr > 0.75) {
            for (const QChar c : vCand) {
                const qreal err = qAbs(stemC(c) - cornerStem);
                if (err < bestErr) {
                    bestErr = err;
                    vBest = c;
                }
            }
        }
        const qreal vw = fm.horizontalAdvance(vBest);
        const qreal tlW = fm.horizontalAdvance(s.tl);
        const qreal trW = fm.horizontalAdvance(s.tr);
        const qreal hw = qMax(0.1, fm.horizontalAdvance(s.h));
        // 渲染级测量：与编辑器同引擎（QTextLayout），fallback 全对——
        // 字体指标估算只作初值，最终以渲染宽度为准
        const auto renderedW = [&](const QString &t) {
            QTextLayout lay(t, activeFont());
            QTextOption opt;
            opt.setWrapMode(QTextOption::NoWrap);
            lay.setTextOption(opt);
            lay.beginLayout();
            lay.createLine();
            lay.endLayout();
            return lay.lineAt(0).naturalTextWidth();
        };
        const qreal need = maxW + spw * 2.0 + vw * 2.0;
        int hN = qMax(1, int(qCeil((need - tlW - trW) / hw)));
        QString topBar = s.tl + QString(hN, s.h) + s.tr;
        const qreal barW = renderedW(topBar);
        // 横线不足/超出：按渲染宽度 ±1 修正
        if (barW < need - hw * 0.5) {
            topBar = s.tl + QString(hN + 1, s.h) + s.tr;
        } else if (barW > need + hw * 0.5 && hN > 1) {
            topBar = s.tl + QString(hN - 1, s.h) + s.tr;
        }
        const qreal boxW = renderedW(topBar);
        // 右竖线的笔画中心 = 右角的笔画中心
        const qreal target = (boxW - trW) + stemC(s.tr);
        QString out = topBar + QLatin1Char('\n');
        for (const QString &l : lines) {
            int nSp = qMax(0, int(qRound(
                (target - vw - spw - lineW(l) - stemC(vBest)) / spw)));
            auto body = [&](int n) {
                return vBest + QLatin1Char(' ') + l
                     + QString(n, QLatin1Char(' ')) + vBest;
            };
            const qreal w0 = renderedW(body(nSp));
            if (w0 < boxW - spw * 0.5)
                ++nSp; // 渲染宽度为准：不足补一格
            else if (w0 > boxW + spw * 0.5 && nSp > 0)
                --nSp;
            QString lineTxt = body(nSp);
            // 细空格（U+2009）最后半步：空格网格的残余 > 半格时，
            // 以细空格补到最近（残余从 ±半空格缩到 ±细空格）
            const qreal wf = renderedW(lineTxt);
            if (boxW - wf > spw * 0.5 && nSp > 0) {
                lineTxt = vBest + QLatin1Char(' ') + l
                        + QString(nSp - 1, QLatin1Char(' '))
                        + QChar(0x2009) + vBest;
                if (renderedW(lineTxt) > boxW)
                    lineTxt = body(nSp); // 细空格不存在（豆腐块）→ 回退
            }
            out += lineTxt + QLatin1Char('\n');
        }
        out += s.bl + QString(topBar.size() - 2, s.h) + s.br;
        replaceAndReselect(first.position(),
                           last.position() + last.length() - 1, out);
    }
    // 压行：选区（或全文）每行去首尾空白、空行跳过、以空格连成一行
    void joinLinesTo()
    {
        QTextDocument *doc = document();
        if (doc->isEmpty())
            return;
        const TextRange rng = selectionOrDoc();
        const QString sel = doc->toPlainText().mid(rng.start, rng.end - rng.start);
        QStringList kept;
        for (const QString &l : sel.split(QLatin1Char('\n'))) {
            const QString t = l.trimmed();
            if (!t.isEmpty())
                kept.append(t);
        }
        if (kept.isEmpty())
            return;
        const QString joined = kept.join(QLatin1Char(' '));
        m_joinMemory = sel; // 记住原文：还原 = 原样恢复（可逆）
        m_joinJoined = joined;
        m_joinMemoryValid = true;
        replaceAndReselect(rng.start, rng.end, joined);
    }
    // 还原：按 ; { } 语义切分回多行 + 2 空格缩进（括号内分号不切）；
    // 内容不含代码分隔符则静默无效果（刚压完想反悔直接 Cmd+Z）
    void restoreLines()
    {
        QTextDocument *doc = document();
        if (doc->isEmpty())
            return;
        const TextRange rng = selectionOrDoc();
        const QString text = doc->toPlainText().mid(rng.start, rng.end - rng.start);
        // 1) 刚压完且内容未再编辑：原样恢复（压行/还原可逆）
        if (m_joinMemoryValid && text == m_joinJoined) {
            m_joinMemoryValid = false;
            replaceAndReselect(rng.start, rng.end, m_joinMemory);
            return;
        }
        QString out;
        const bool hasCode = text.contains(QLatin1Char(';'))
                          || text.contains(QLatin1Char('{'))
                          || text.contains(QLatin1Char('}'));
        if (!hasCode) {
            // 2) 散文：按句读（。！？…!?）切回一行一句（无句读则静默）
            bool any = false;
            for (QChar ch : text) {
                out += ch;
                if (ch == QChar(0x3002) || ch == QChar(0xFF01) || ch == QChar(0xFF1F)
                    || ch == QChar(0x2026) || ch == QLatin1Char('!')
                    || ch == QLatin1Char('?')) {
                    out += QLatin1Char('\n');
                    any = true;
                }
            }
            if (!any)
                return;
            out = out.trimmed(); // 尾部句读后的换行并成一段
            if (out.isEmpty())
                return;
        } else {
        int indent = 0;
        int paren = 0;
        const auto ind = [&indent] { return QString(2 * indent, QLatin1Char(' ')); };
        for (int i = 0; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            if (ch == QLatin1Char('('))
                ++paren;
            else if (ch == QLatin1Char(')'))
                paren = qMax(0, paren - 1);
            if (ch == QLatin1Char('{')) {
                out += QLatin1Char('{');
                out += QLatin1Char('\n');
                ++indent;
                while (i + 1 < text.size() && text.at(i + 1) == QLatin1Char(' '))
                    ++i;
                out += ind();
            } else if (ch == QLatin1Char('}')) {
                while (out.endsWith(QLatin1Char(' ')) || out.endsWith(QLatin1Char('\n')))
                    out.chop(1);
                indent = qMax(0, indent - 1);
                out += QLatin1Char('\n');
                out += ind();
                out += QLatin1Char('}');
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char(';')) {
                    out += QLatin1Char(';');
                    ++i;
                }
                if (i + 1 < text.size()) {
                    while (i + 1 < text.size() && text.at(i + 1) == QLatin1Char(' '))
                        ++i;
                    out += QLatin1Char('\n');
                    out += ind();
                }
            } else if (ch == QLatin1Char(';') && paren == 0
                       && (i + 1 == text.size() || text.at(i + 1) == QLatin1Char(' '))) {
                out += QLatin1Char(';');
                out += QLatin1Char('\n');
                out += ind();
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char(' '))
                    ++i;
            } else {
                out += ch;
            }
        }
        }
        out.replace(QRegularExpression(QStringLiteral("\n{3,}")), QStringLiteral("\n\n"));
        out = out.trimmed();
        replaceAndReselect(rng.start, rng.end, out);
    }
    // 路径→树：选区（或全文）每行一个路径（'/' 分层；行尾 '/' = 目录）
    // → ├──/└── ASCII 树（自 AsciiTools 移植）
    void pathsToTree()
    {
        struct Node {
            QString name;
            bool isDir = false;
            QVector<Node> children;
        };
        QTextDocument *doc = document();
        if (doc->isEmpty())
            return;
        const TextRange rng = selectionOrDoc();
        const QStringList rawLines =
            doc->toPlainText().mid(rng.start, rng.end - rng.start).split(QLatin1Char('\n'));
        Node root;
        root.isDir = true;
        for (const QString &raw : rawLines) {
            const QString line = raw.trimmed();
            if (line.isEmpty())
                continue;
            const bool isDir = line.endsWith(QLatin1Char('/'))
                            || line.endsWith(QLatin1Char('\\'));
            // '/' 与 '\' 都按层级分隔（Windows 路径无需预处理）
            QStringList parts =
                line.split(QRegularExpression(QStringLiteral("[\\\\/]")), Qt::SkipEmptyParts);
            Node *node = &root;
            for (int i = 0; i < parts.size(); ++i) {
                const QString name = parts.at(i);
                Node *child = nullptr;
                for (Node &n : node->children) {
                    if (n.name == name) {
                        child = &n;
                        break;
                    }
                }
                if (!child) {
                    node->children.append(Node{name, (i < parts.size() - 1) || isDir, {}});
                    child = &node->children.last();
                }
                if (isDir && i == parts.size() - 1)
                    child->isDir = true;
                node = child;
            }
        }
        QStringList out;
        std::function<void(const Node &, const QString &, bool)> render =
            [&](const Node &node, const QString &prefix, bool isLast) {
                out.append(prefix + (isLast ? QStringLiteral("└── ") : QStringLiteral("├── "))
                           + node.name + (node.isDir ? QStringLiteral("/") : QString()));
                const QString childPrefix = prefix + (isLast ? QStringLiteral("    ")
                                                             : QStringLiteral("│   "));
                for (int i = 0; i < node.children.size(); ++i)
                    render(node.children.at(i), childPrefix, i == node.children.size() - 1);
            };
        for (int i = 0; i < root.children.size(); ++i)
            render(root.children.at(i), QString(), i == root.children.size() - 1);
        if (out.isEmpty())
            return;
        const QString tree = out.join(QLatin1Char('\n'));
        replaceAndReselect(rng.start, rng.end, tree);
    }
    // 树→路径：├──/└── 树解析回 '/' 路径列表（目录行尾带 '/'）——
    // 路径树的还原（自 AsciiTools 移植）
    void treeToPaths()
    {
        struct Node {
            QString name;
            bool isDir = false;
            QVector<Node> children;
        };
        QTextDocument *doc = document();
        if (doc->isEmpty())
            return;
        const TextRange rng = selectionOrDoc();
        const QStringList rawLines =
            doc->toPlainText().mid(rng.start, rng.end - rng.start).split(QLatin1Char('\n'));
        Node root;
        root.isDir = true;
        QVector<QPair<int, Node *>> stack; // (depth, node)
        bool any = false;
        for (const QString &raw : rawLines) {
            QString line = raw;
            while (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t')))
                line.chop(1);
            if (line.trimmed().isEmpty())
                continue;
            int p = 0;
            while (p < line.size()
                   && (line.at(p) == QChar(0x2502) || line.at(p) == QLatin1Char(' ')))
                ++p;
            const QString prefix = line.left(p); // 前缀只含 │ 与空格
            QString rest = line.mid(p);
            const bool branch = rest.startsWith(QStringLiteral("├── "))
                             || rest.startsWith(QStringLiteral("└── "));
            QString nameRaw = branch ? rest.mid(4).trimmed() : rest.trimmed();
            if (nameRaw.isEmpty())
                continue;
            const bool isDir = nameRaw.endsWith(QLatin1Char('/'));
            QString name = nameRaw;
            if (isDir)
                name.chop(1);
            // 每 4 字符前缀块 = 一层（├──/└── 缩进约定）
            const int depth = prefix.size() / 4;
            if (!branch) { // 根名行：容忍但本工具不产出
                root.name = name;
                root.isDir = isDir;
                stack.clear();
                stack.append({0, &root});
                continue;
            }
            while (!stack.isEmpty() && stack.last().first >= depth)
                stack.removeLast();
            Node *parent = stack.isEmpty() ? &root : stack.last().second;
            parent->children.append(Node{name, isDir, {}});
            stack.append({depth, &parent->children.last()});
            any = true;
        }
        if (!any)
            return;
        QStringList out;
        std::function<void(const Node &, const QString &)> walk =
            [&](const Node &node, const QString &prefix) {
                const QString full =
                    prefix.isEmpty() ? node.name : prefix + QLatin1Char('/') + node.name;
                if (node.isDir) {
                    if (node.children.isEmpty())
                        out.append(full + QLatin1Char('/'));
                    for (const Node &ch : node.children)
                        walk(ch, full);
                } else {
                    out.append(full);
                }
            };
        if (root.children.isEmpty() && !root.name.isEmpty()) {
            walk(root, QString());
        } else {
            for (const Node &ch : root.children)
                walk(ch, root.name);
        }
        const QString paths = out.join(QLatin1Char('\n'));
        replaceAndReselect(rng.start, rng.end, paths);
    }
    // 居中：选区（或当前行）每一行按窗口宽度居中——左补半差空格
    // （CJK 双格）；超宽行不动（不毁字）
    void centerToWidth()
    {
        QTextDocument *doc = document();
        if (doc->isEmpty())
            return;
        const QFontMetricsF fm(activeFont());
        const qreal spw = qMax(0.1, fm.horizontalAdvance(QLatin1Char(' ')));
        const qreal winW = qMax(1.0, qreal(viewport()->width()));
        const auto lineW = [&](const QString &t) {
            qreal w = 0.0;
            for (QChar ch : t)
                w += fm.horizontalAdvance(ch);
            return w;
        };
        QTextBlock first, last;
        selectionOrLine(first, last);
        QStringList out;
        bool changed = false;
        for (QTextBlock b = first;; b = b.next()) {
            QString t = b.text();
            // 按字体真实推进居中（旧版 CJK=2 计数与字体不符 → 偏右）
            const int nSp = int((winW - lineW(t)) / (2.0 * spw));
            if (nSp > 0) {
                t.prepend(QString(nSp, QLatin1Char(' ')));
                changed = true;
            }
            out.append(t);
            if (b == last)
                break;
        }
        if (!changed)
            return;
        replaceAndReselect(first.position(),
                           last.position() + last.length() - 1,
                           out.join(QLatin1Char('\n')));
    }

    // 自杀（M6）：立即退出，无保存提示——"关闭即无"的极致
    void commitSuicide()
    {
        QApplication::quit();
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
        // 块状反相光标：只在显模式、有焦点、眨眼"亮"拍时画（休眠 = 隐去，
        // 与原生光标同一节拍；见 syncNativeCaretWidth）。
        const bool cursorBlock = m_crt && hasFocus()
            && m_blinkTimer.isActive() && m_blinkHalf % 2 == 0;
        {
            QPainter p(&img);
            if (!p.isActive())
                return;
            p.fillRect(img.rect(), crtPalette().bg); // 磷光底：行号列条/滚动条槽也同色
            // DPR：调用方把图像按物理像素建好并 setDevicePixelRatio(dpr)。
            // Qt 6.8+ 的 QImage 画笔会在引擎层自动应用图像 DPR（经验证：
            // 有效缩放 = 画笔变换 × 图像 DPR），所以这里绝不能手动再
            // p.scale(dpr)——二次相乘会把内容放大推出画面（右侧滚动条把手
            // 完全消失、文字只剩左上象限的根源），逻辑坐标交给引擎映射。
            if (viewport())
                viewport()->render(&p, viewport()->pos());
            paintExcitation(p); // 磷粉激发：新字符在文字之上加色增亮（900ms 内）
            if (m_canvas && m_canvas->isVisible())
                m_canvas->render(&p, m_canvas->pos());
            // 行号区：编模式下真实组件已隐藏（防双层），合成仍渲染它——
            // 行号只存在于光栅内；非编模式不渲染（残留的隐藏组件会在
            // 旧位置叠在字上）
            if (m_codeMode && m_lineNumberArea)
                m_lineNumberArea->render(&p, m_lineNumberArea->pos());
            if (m_fadeOpacity > 0.02) {
                p.save();
                p.setOpacity(m_fadeOpacity);
                // 滚动条直接画把手几何：不走 QWidget::render。Qt 6.9 起滚动条
                // 住在 QAbstractScrollArea 的私有容器 QWidget 里，pos() 只是
                // 容器内坐标（恒 (0,0)）——按 pos() 平移 = 把手画到窗口左缘
                // （"左侧镜像滚动条"）。必须 mapTo 换算到编辑器坐标；
                // 隐藏的滚动条不画（防隐藏条的把手残留在窗口左上角）。
                if (auto *v = qobject_cast<ZenScrollBar *>(verticalScrollBar()))
                    if (v->isVisible())
                        v->paintOnto(p, v->mapTo(this, QPoint(0, 0)));
                if (auto *h = qobject_cast<ZenScrollBar *>(horizontalScrollBar()))
                    if (h->isVisible())
                        h->paintOnto(p, h->mapTo(this, QPoint(0, 0)));
                p.restore();
            }
        } // 画家析构后直接回写像素，避免与光栅引擎缓存交错

        if (cursorBlock)
            paintCrtCursor(img);
    }
    // P3：增量重拍——只重画脏区（复用上一帧图像为底）。与全量同构图，
    // 仅以 clip 限制绘制范围；脏区先清底再画（旧光标/旧字符被抹掉）
    void paintTextSnapshotRegion(QImage &img, const QRect &dirty) const
    {
        if (dirty.isEmpty())
            return;
        const bool cursorBlock = m_crt && hasFocus()
            && m_blinkTimer.isActive() && m_blinkHalf % 2 == 0;
        {
            QPainter p(&img);
            if (!p.isActive())
                return;
            p.setClipRect(dirty);
            p.fillRect(dirty, crtPalette().bg); // 磷光底：先清脏区
            // 渲染整个视口、由画家 clip 限范围：source-region 语义在
            // DPR 引擎下与 clip 的坐标映射存在偏差（实测边缘像素漏画）
            if (viewport())
                viewport()->render(&p, viewport()->pos());
            paintExcitation(p); // clip 限范围：只重画脏区内的激发
            if (m_canvas && m_canvas->isVisible())
                m_canvas->render(&p, m_canvas->pos());
            if (m_codeMode && m_lineNumberArea)
                m_lineNumberArea->render(&p, m_lineNumberArea->pos());
            if (m_fadeOpacity > 0.02) {
                p.save();
                p.setOpacity(m_fadeOpacity);
                if (auto *v = qobject_cast<ZenScrollBar *>(verticalScrollBar()))
                    if (v->isVisible())
                        v->paintOnto(p, v->mapTo(this, QPoint(0, 0)));
                if (auto *h = qobject_cast<ZenScrollBar *>(horizontalScrollBar()))
                    if (h->isVisible())
                        h->paintOnto(p, h->mapTo(this, QPoint(0, 0)));
                p.restore();
            }
        }
        if (cursorBlock)
            paintCrtCursor(img);
    }

    // 块状反相光标（真机时代的整格闪烁块）：落点所在字格满格点亮成
    // 炽磷色，字符像素按亮度线性映射回底色（保 AA）——反相视频的
    // 双色精确版。原生 I 形在显模式恒不画（syncNativeCaretWidth）。
    void paintCrtCursor(QImage &img) const
    {
        const QTextCursor c = textCursor();
        QRect cell = cursorRect(c); // 插入位（宽度为 0 的落点矩形）
        cell.translate(viewport()->pos()); // 视口坐标 → 编辑器坐标（编模式有行号槽偏移）
        if (cell.isNull())          // 零宽合法（isValid 要求宽高>0，会误拒）
            return;
        const QChar ch = document()->characterAt(c.position());
        const qreal adv = (ch.isNull() || ch == QChar::ParagraphSeparator)
                              ? fontMetrics().horizontalAdvance(QLatin1Char('M'))
                              : fontMetrics().horizontalAdvance(ch);
        cell.setWidth(qMax(1, qCeil(adv)));
        // 逻辑 → 物理像素（引擎层 DPR 映射：物理 = 逻辑 × dpr）
        const qreal dpr = img.devicePixelRatio();
        const int x0 = qFloor(cell.x() * dpr), y0 = qFloor(cell.y() * dpr);
        const int x1 = qCeil((cell.x() + cell.width()) * dpr);
        const int y1 = qCeil((cell.y() + cell.height()) * dpr);
        // 双色反相：t = 像素亮度在 底→墨 间的归一位置；out = lerp(块, 底, t)
        const Crt::Palette &pp = crtPalette();
        // 编模式（多彩语法高亮）下块光标用中性暖白：绿磷块在代码里太突兀
        const QColor block = m_codeMode ? QColor(0xE8, 0xE8, 0xE0) : pp.cursorBlock;
        const int bgSum = pp.bg.red() + pp.bg.green() + pp.bg.blue();
        const int inkSum = pp.ink.red() + pp.ink.green() + pp.ink.blue();
        const int span = qMax(1, inkSum - bgSum); // 防御除零（底=墨时）
        const int w = img.width(), h = img.height();
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
    // 磷粉激发：新字符三圈软边增亮，~500ms 指数回落（二期三件套·回接）。
    // 在文字之上做加色混合（CompositionMode_Plus）——真机上是磷粉
    // 刚被打中时的过量发光，随后按指数回落到常态。
    void paintExcitation(QPainter &p) const
    {
        if (!m_exciteClock.isValid())
            return;
        const qint64 t = m_exciteClock.elapsed();
        if (t < 0 || t >= 900)
            return;
        const qreal a = 0.6 * qExp(-qreal(t) / 500.0); // 激发峰值 0.6，500ms 指数回落
        if (a < 0.02)
            return;
        const int pos = qBound(0, m_excitePos, document()->characterCount() - 1);
        QTextCursor cc = textCursor();
        cc.setPosition(pos);
        QRect cell;
        if (cc.atEnd()) {
            cell = cursorRect(cc);
            cell.setWidth(qCeil(fontMetrics().horizontalAdvance(QLatin1Char('M'))));
        } else {
            QTextCursor sel(cc);
            sel.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            cell = cursorRect(sel);
        }
        cell.translate(viewport()->pos()); // 视口坐标 → 编辑器坐标（编模式有行号槽偏移）
        if (cell.isNull())
            return;
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.setPen(Qt::NoPen);
        const Crt::Palette &pp = crtPalette();
        const qreal amps[3] = { a, a * 0.45, a * 0.2 };
        const int grow[3] = { 1, 3, 6 };
        for (int i = 0; i < 3; ++i) {
            p.setBrush(QColor(pp.cursorBlock.red(), pp.cursorBlock.green(),
                              pp.cursorBlock.blue(), qRound(255.0 * amps[i])));
            const int g = grow[i];
            p.drawRoundedRect(cell.adjusted(-g, -g, g, g), 4 + g, 4 + g);
        }
        p.restore();
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
    QImage crtShownImage() const
    {
        return m_crtView ? m_crtView->shownImage() : QImage();
    }
    // 人眼代理：鼠标在视口内的位置（反光视差追踪用）
    QPointF lastMouseViewport() const { return m_lastMouse; }
    // 追随视角锁定（M1）：true = 复现鼠标离开窗口后的"完美视角"
    // （内容完整不被裁剪）；进「显」时重置为 true
    bool crtViewLocked() const { return m_viewLock; }
    void toggleViewLock() { m_viewLock = !m_viewLock; }

    // 切换计算机（M2）：琥珀（Osborne Executive）↔ 绿磷（IBM 5100），
    // 同时切换两台的出厂默认字体（Fusion Pixel ↔ VT323）
    const Crt::Palette &crtPalette() const
    {
        return m_machine == 1 ? Crt::kGreen
             : m_machine == 2 ? Crt::kC64
             : m_machine == 3 ? Crt::kWhite
             : Crt::kAmber;
    }
    int machine() const { return m_machine; }
    bool machineGreen() const { return m_machine == 1; }
    void toggleMachine()
    {
        markSnapshotFullDirty(); // 调色板/字体变化：全量
        m_machine = (m_machine + 1) % 4; // 琥珀 → 绿磷 → C64 → IBM PC → 琥珀
        const bool prev = m_settingAscii;
        m_settingAscii = true; // 高亮器 rehighlight 会发 contentsChanged——程序操作
        applyScheme();
        applyZoom(); // 字体随机器切换（Fusion Pixel ↔ VT323）
        if (m_asciiActive)
            replaceAsciiArt(); // 画布在场 → 按新机器重印（真彩 ↔ 单色即时切换）
        viewport()->update();
        if (m_canvas)
            m_canvas->update();
        if (m_lineNumberArea)
            m_lineNumberArea->update();
        if (m_crtView)
            m_crtView->markDirty(true);
        m_settingAscii = prev;
    }

    // 屏幕实体（原实验功能，M4.5 并入）：追随视角解锁（非锁定）时生效
    // ——锁定时是干净"完美视角"，解锁后是沉浸的弯曲玻璃屏（不裁字）
    bool screenEntityOn() const { return m_crt && !m_viewLock; }
    // P3 快照增量：打字只重画脏区。consume 一次性取走脏区并复位
    CrtSource::SnapDirty consumeSnapshotDirty() override
    {
        SnapDirty d;
        d.full = m_snapFullDirty;
        d.rect = m_snapDirty;
        m_snapDirty = QRect();
        m_snapFullDirty = false;
        return d;
    }
    void markSnapshotFullDirty() { m_snapFullDirty = true; }
    // 全量失效收口：标全量 + 唤醒渲染环（避免只标全量不唤醒 → 等环境拍）
    void invalidateFullSnapshot()
    {
        markSnapshotFullDirty();
        if (m_crtView)
            m_crtView->markDirty(true);
    }
    bool isScrolling() const { return m_scrolling; } // CRT 快照降载信号
    bool asciiArtActive() const { return m_asciiActive; } // 画布编辑态（立为图勾选）
    bool colorMachine() const { return m_machine == 2; }   // C64：字符画逐字符真彩
    bool asciiPrintingDbg() const { return m_asciiPrinting; }

    // ---- 字体管理（「项」·字体区）：用户字体文件夹 + 上/下一个 ----
    static QString fontDir()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
               + QStringLiteral("/naught/fonts");
    }
    // 出厂经典库存：字体文件夹不存在时（首次启动/整个文件夹被删）播种。
    // 经典字体可改名（改循环顺序）、可删；整个文件夹删除后下次启动重播。
    // 出厂默认（Fusion Pixel / VT323）在 qrc 里，删不掉。
    static void seedClassicFonts()
    {
        const QString dir = fontDir();
        // 文件夹存在但为空（例如「打开字体文件夹」先建了空目录）也要播种；
        // 删光后重启 = 重新播种（软恢复出厂）
        if (QDir(dir).exists() && !QDir(dir).entryList(QDir::Files, QDir::Name).isEmpty())
            return;
        QDir().mkpath(dir);
        const QStringList classics = {
            QStringLiteral(":/fonts/classic/PressStart2P-Regular.ttf"),
            QStringLiteral(":/fonts/classic/3270-Regular.ttf"),
            QStringLiteral(":/fonts/classic/CozetteVector.ttf"),
            QStringLiteral(":/fonts/classic/ProggyClean.ttf"),
            QStringLiteral(":/fonts/classic/Silkscreen-Regular.ttf"),
            QStringLiteral(":/fonts/classic/FSEX302.ttf"),
        };
        for (const QString &res : classics) {
            QFile f(res);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            QFile out(dir + QLatin1Char('/') + QFileInfo(res).fileName());
            if (out.open(QIODevice::WriteOnly))
                out.write(f.readAll());
        }
    }
    // 递归扫描字体文件夹（含嵌套子文件夹），按文件顺序加载并返回可用族名；
    // 损坏/被删的文件自动跳过——出厂字体始终在包里，机器永不断字。
    static QStringList scanFontFamilies(const QString &dir)
    {
        QStringList families;
        QDirIterator it(dir, { QStringLiteral("*.ttf"), QStringLiteral("*.otf") },
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = QFileInfo(it.next()).canonicalFilePath();
            // 去重：cycleCrtFont 每次重扫重复注册同一文件，字体库缓存增长
            if (s_registeredFonts.contains(path))
                continue;
            const int id = QFontDatabase::addApplicationFont(path);
            if (id >= 0) {
                const QStringList fams = QFontDatabase::applicationFontFamilies(id);
                if (!fams.isEmpty()) {
                    s_registeredFonts.insert(path);
                    families.append(fams.first());
                }
            }
        }
        return families;
    }
    void refreshFonts()
    {
        m_userFonts = scanFontFamilies(fontDir());
    }
    void openFontFolder()
    {
        QDir().mkpath(fontDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(fontDir()));
    }
    // 上一个/下一个：按字体文件夹文件顺序循环当前机器的字体，实时生效。
    // 循环列表 = [出厂默认] + 文件夹字体（可循环回出厂）；文件夹为空或
    // 字体全被删 → 保持出厂字体（静默）。
    void cycleCrtFont(int delta)
    {
        refreshFonts(); // 每次重扫：刚拖入的字体即时可用
        if (m_userFonts.isEmpty())
            return;
        const QString factory = factoryFontFor(m_machine);
        const QStringList all = QStringList() << factory << m_userFonts;
        int idx = all.indexOf(crtFontFamily());
        if (idx < 0)
            idx = 0;
        idx += delta;
        if (idx < 0)
            idx += all.size();
        idx %= all.size();
        QString &u = machineUserFont();
        u = (idx == 0) ? QString() : all.at(idx); // 空 = 出厂
        saveFontChoice(); // 暂时默认：跨启动记忆
        applyZoom();
        viewport()->update();
        if (m_lineNumberArea)
            m_lineNumberArea->update();
        if (m_crtView)
            m_crtView->markDirty(true);
    }
    // 「恢复默认」：当前机器回到出厂默认字体（清掉暂时默认）
    void restoreDefaultFont()
    {
        QString &u = machineUserFont();
        u.clear();
        saveFontChoice();
        applyZoom();
        viewport()->update();
        if (m_lineNumberArea)
            m_lineNumberArea->update();
        if (m_crtView)
            m_crtView->markDirty(true);
    }
    void saveFontChoice()
    {
        QSettings st;
        const QString key = m_machine == 1 ? QStringLiteral("fontGreen")
                             : m_machine == 2 ? QStringLiteral("fontC64")
                                              : QStringLiteral("fontAmber");
        const QString &u = machineUserFont();
        if (u.isEmpty())
            st.remove(key);
        else
            st.setValue(key, u);
    }
    // 机器 → 用户手选字体槽 / 出厂字体（三元散落多处，收口）
    QString &machineUserFont()
    {
        return m_machine == 1 ? m_greenUser
             : m_machine == 2 ? m_c64User : m_amberUser;
    }
    const QString &machineUserFont() const
    {
        return m_machine == 1 ? m_greenUser
             : m_machine == 2 ? m_c64User : m_amberUser;
    }
    static const QString &factoryFontFor(int machine)
    {
        return machine == 0 ? s_crtFamily
             : machine == 2 ? s_c64Family
             : machine == 3 ? s_whiteFamily : s_greenFamily;
    }
    QString crtFontFamily() const
    {
        const QString &u = machineUserFont();
        if (!u.isEmpty() && m_userFonts.contains(u))
            return u;
        return factoryFontFor(m_machine); // 出厂回退
    }

    // ---- M3：拖图片 → 字符画（隐藏功能，README 不提及）----
    // 逐行打印（打字机效果，真机感）：每行插入都触发磷粉激发。
    // 画布缩放（Shift+捏合 / Shift+Ctrl+滚轮）"再打印"：移除旧块 →
    // 逐行打印新网格。字符画永不重排（NoWrap，防叠行）；画布放大有
    // 原生分辨率上限（1 源像素 1 格——再大没有更多细节，也防爆炸）。
    void loadAsciiImage(const QImage &img)
    {
        markSnapshotFullDirty(); // 换行模式/画布插入：全量起步
        if (img.isNull())
            return;
        m_asciiImage = img;
        m_asciiScale = 1.0;
        const QFontMetricsF fm(activeFont());
        const qreal cw = qMax(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        m_asciiBaseCols = qMax(2, int(viewport()->width() / cw));
        setLineWrapMode(QPlainTextEdit::NoWrap); // 永不重排（防叠行）
        QTextCursor c = textCursor();
        m_asciiStart = c.position();
        m_asciiEnd = m_asciiStart;
        m_lastArtStart = m_asciiStart;
        m_lastArtEnd = m_asciiStart;
        m_asciiActive = true;
        beginAsciiEditBlock(); // 首次打印 = 一步撤销（防逐行残步污染撤销栈）
        printAscii(artText(), m_asciiStart);
        if (m_crtView)
            m_crtView->markDirty();
    }
    QString artText()
    {
        const QFontMetricsF fm(activeFont());
        const qreal cw = qMax(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        const qreal ch = qMax(1.0, fm.height());
        // 画布列数 = 倍率 × 基准；上限 = 源图原生分辨率（防爆炸）
        const int maxCols = qMax(m_asciiBaseCols, m_asciiImage.width());
        int cols = qBound(2, int(m_asciiBaseCols * m_asciiScale), maxCols);
        int rows = qMax(2, int(cols * (qreal(m_asciiImage.height()) / m_asciiImage.width())
                                * (cw / ch)));
        // 总量护栏：任何路径（拖图/缩放/立为图）画布 ≤ 20 万字符——
        // 等比缩回（真机一屏 80×25 = 2000 字符，20 万已是百屏）
        if (qint64(cols) * rows > 200000) {
            const qreal k = qSqrt(200000.0 / (qint64(cols) * rows));
            cols = qMax(2, int(cols * k));
            rows = qMax(2, int(rows * k));
        }
        if (colorMachine()) {
            // C64 真彩：每字符前景色 = 源图像素 16 色量化（chafa 式）
            return Ascii::imageToTextColors(m_asciiImage, cols, rows, m_asciiColors,
                                            Crt::kC64Colors);
        }
        m_asciiColors.clear();
        return Ascii::imageToText(m_asciiImage, cols, rows);
    }
    // 彩色行插入：同色连续段合并为单次 insertText（带前景色格式）
    void insertColoredLine(QTextCursor &c, int lineIdx)
    {
        int offset = 0;
        for (int l = 0; l < lineIdx; ++l)
            offset += m_asciiPrintLines.at(l).size();
        const QString line = m_asciiPrintLines.at(lineIdx);
        const int total = m_asciiColors.size();
        const auto colAt = [&](int idx) {
            return idx < total ? m_asciiColors.at(idx) : QRgb(0x00FFFFFF);
        };
        int i = 0;
        while (i < line.size()) {
            // 越界钳位：颜色数与字符数不符时不再 at() 崩溃（防御）
            const QRgb col = colAt(offset + i);
            int j = i + 1;
            while (j < line.size() && colAt(offset + j) == col)
                ++j;
            QTextCharFormat fmt;
            fmt.setForeground(QColor(col));
            c.insertText(line.mid(i, j - i), fmt);
            i = j;
        }
    }
    // 逐块打印：m_asciiPrintTimer 每拍插入一个行块（光标跟进，行行激发）。
    // 每拍一次文档重排（成本大户），总拍数 ~40、间隔 40ms → 总时长
    // ~1.6s 不随分辨率变慢（真机一帧满屏，打印是剧场不是机器速度）
    void printAscii(const QString &art, int pos)
    {
        m_asciiPrintLines = art.split(QLatin1Char('\n'));
        m_asciiPrintIdx = 0;
        m_asciiPrintPos = pos;
        m_asciiPrinting = true;
        m_asciiLinesPerTick = qMax(1, m_asciiPrintLines.size() / 40);
        m_asciiPrintTimer.setInterval(40);
        m_asciiPrintTimer.start();
    }
    // 画布合并撤销策略：程序写入（移除+整轮重印）期间暂停撤销记录。
    // Qt 的编辑块(block_part/block_end)撤销回走不可靠（实测：块内命令
    // 交错合并后一次 undo 只退一行，undo/redo 来回还会丢字符——旧事故
    // "字符都删了"的根源）。暂停记录后画布操作完全不进撤销历史：
    // 画布成为新的撤销基线，Cmd+Z 永不蚕食画布、永不损坏文档；
    // 之后的打字照常可撤销（空/撤销/重勾立为图一切如常）。
    void beginAsciiEditBlock()
    {
        if (!document()->isUndoRedoEnabled())
            return;
        m_asciiUndoPaused = true;
        document()->setUndoRedoEnabled(false);
    }
    void endAsciiEditBlock()
    {
        if (!m_asciiUndoPaused)
            return;
        m_asciiUndoPaused = false;
        document()->setUndoRedoEnabled(true);
    }
    // 「立为图」：把选区栅格化成字符画的源图——之后 Shift+缩放即可调
    // 画布（网格重渲染）。无选区时自动复选上次立为图的字符；没有上次
    // 则静默无效果。编辑过的字符画选中再立为图即可（编辑成果保留）。
    void declareArtFromSelection()
    {
        markSnapshotFullDirty(); // 画布态切换（NoWrap）：全量
        QTextCursor c = textCursor();
        int start = -1, end = -1;
        if (c.hasSelection()) {
            start = c.selectionStart();
            end = c.selectionEnd();
        } else if (m_lastArtStart >= 0 && m_lastArtEnd > m_lastArtStart) {
            // 自动复选上次立为图的字符；范围因撤销/清空失效时退化为全文
            // （撤销恢复后的画布字符仍然在文档里——复选它们即"记忆"）
            const int last = qMax(0, document()->characterCount() - 1);
            start = qBound(0, m_lastArtStart, last);
            end = qMin(m_lastArtEnd, last);
            if (end <= start) { // 上次范围已不复存在 → 复选全文
                start = 0;
                end = last;
            }
        } else {
            return; // 无选区且无上次 → 无任何影响
        }
        if (end <= start)
            return; // 空文档：无事（不产生假勾选）
        // 注意：selectedText() 的段落分隔是 \u2029——split('\n') 劈不开，
        // 整段会当成一行（baseCols 爆炸 → 换机重印 200 万字符 → 卡死）。
        // 改用 toPlainText（\n 分段）。
        const QString sel = document()->toPlainText().mid(start, end - start);
        const QFont f = activeFont();
        const QFontMetricsF fm(f);
        const qreal cw = fm.horizontalAdvance(QLatin1Char('M'));
        const qreal lh = fm.height();
        const QStringList lines = sel.split(QLatin1Char('\n'));
        int maxLen = 1;
        for (const QString &l : lines)
            maxLen = qMax(maxLen, int(l.size()));
        // 防御：单行过长（>400 列）是散文不是画布——画布列数封顶，
        // 防巨幅重印拖死换机（真机一屏 80 列，400 已远超）
        maxLen = qMin(maxLen, 400);
        QImage raster(qMax(8, int(maxLen * cw) + 8),
                      qMax(8, int(lines.size() * lh) + 8),
                      QImage::Format_ARGB32);
        raster.fill(Qt::black);
        {
            QPainter p(&raster);
            p.setFont(f);
            p.setPen(Qt::white);
            qreal y = lh;
            for (const QString &l : lines) {
                p.drawText(QPointF(4.0, y), l);
                y += lh;
            }
        }
        m_asciiImage = raster;
        m_asciiScale = 1.0;
        m_asciiBaseCols = maxLen; // 当前观感：列数 = 选区最长行
        m_asciiStart = start;
        m_asciiEnd = end;
        m_lastArtStart = start;
        m_lastArtEnd = end;
        // 清掉上一轮画布的残留打印状态（防旧打印头污染新画布范围）
        m_asciiPrintTimer.stop();
        m_asciiPrinting = false;
        m_asciiPrintPos = start;
        endAsciiEditBlock(); // 收口被重勾打断的打印块
        setLineWrapMode(QPlainTextEdit::NoWrap); // 立为图即入画布态：永不重排（防叠行）
        m_asciiActive = true;
    }

    // 用新画布尺寸原位替换已插入的字符画（移除旧块 → 再打印）
    void replaceAsciiArt()
    {
        if (!m_asciiActive || m_asciiImage.isNull())
            return;
        endAsciiEditBlock(); // 上一轮打印块收口（中断时不留开块）
        m_asciiPrintTimer.stop();
        m_asciiPrinting = false;
        // 边界钳位：任何历史残留都不能让移除范围越过画布自身区域
        // （旧事故：m_asciiEnd 越过文档末尾 → 移除吞掉画布外文字）
        const int last = qMax(0, document()->characterCount() - 1);
        m_asciiStart = qBound(0, m_asciiStart, last);
        m_asciiEnd = qBound(m_asciiStart, m_asciiEnd, last);
        m_asciiPrintPos = qBound(m_asciiStart, m_asciiPrintPos, last);
        QTextCursor c = textCursor();
        c.setPosition(m_asciiStart);
        c.setPosition(qMax(m_asciiEnd, m_asciiPrintPos), QTextCursor::KeepAnchor);
        beginAsciiEditBlock(); // 移除+重印 = 一步撤销
        m_settingAscii = true; // 程序替换不算手动编辑——否则画布态被自己杀掉
        c.removeSelectedText();
        m_settingAscii = false;
        m_asciiEnd = m_asciiStart;
        m_asciiPrintPos = m_asciiStart;
        printAscii(artText(), m_asciiStart);
        if (m_crtView)
            m_crtView->markDirty(true);
    }
    // 画布缩放：网格随倍率重渲染；80ms 节流 + 尾拍兜底（捏合事件高频）
    void zoomAsciiCanvas(qreal factor)
    {
        if (!m_asciiActive)
            return;
        m_asciiScale = qBound(0.05, m_asciiScale * factor, 8.0); // 下限 5%：dpi 拉满后可整体缩得更小
        if (!m_asciiRenderClock.isValid() || m_asciiRenderClock.elapsed() > 80) {
            replaceAsciiArt();
            m_asciiRenderClock.restart();
        }
        m_asciiSettleTimer.start(120); // 手势停止后补渲染最后一拍
    }
    // 画布最佳化（Cmd+0，字符画在场时）：网格贴合窗口——无论字号大小
    // 都不叠行、不溢出（缩放放大的归位键）
    void optimizeAsciiCanvas()
    {
        if (!m_asciiActive || m_asciiImage.isNull())
            return;
        const QFontMetricsF fm(activeFont());
        const qreal cw = qMax(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        m_asciiScale = qBound(0.2, qreal(viewport()->width()) / cw / m_asciiBaseCols, 8.0);
        replaceAsciiArt();
    }



    // 显：单一琥珀磷光模式——零 UI，一键回到过去（与编、阴/阳正交可叠加）
    void toggleCrt()
    {
        m_crt = !m_crt;
        if (m_crt) {
            m_viewLock = true; // 进显即锁定居中视角（退出重进 = 再次锁定）
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
        } else {
            // 常驻对象，只隐藏。无原生窗口：隐藏即彻底让位，
            // 不需要销毁、不需要摘除任何属性——事件分发天然恢复。
            if (m_crtView)
                m_crtView->hide();
            viewport()->releaseMouse(); // 防御：抓取会话不跨显模式残留
            // 仅编模式恢复行号区：非编模式下它是隐藏的残留组件，
            // 无条件 show 会把旧几何的行号叠在首列文字上
            if (m_codeMode && m_lineNumberArea)
                m_lineNumberArea->show();
            viewport()->update();
            setFocus();
        }
        syncNativeCaretWidth(); // 显：原生 I 形退场，块光标接管；退出时恢复
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
        applyAnchoredZoom(m_size + delta); // 缩放字符（字号）——与无图时一致
    }

    void zoomTo(qreal size)
    {
        applyAnchoredZoom(size);
    }

    void zoomReset()
    {
        if (m_asciiActive) {
            optimizeAsciiCanvas(); // 画布最佳化：网格贴合窗口
            return;
        }
        if (m_crt) {
            // 显·Cmd+0 = 机器原生网格（原实验·字符网格并入）：琥珀 80 列 /
            // 绿磷 64 列——真机的"原生分辨率"
            const int cols = (m_machine == 0) ? 80 : (m_machine == 1) ? 64 : 40;
            qreal sz = qreal(viewport()->width()) / cols
                       / (m_machine == 0 ? 1.0 : 1.25);
            if (m_machine == 2)
                sz = qMin(sz, 16.0); // C64：真机字符 ≈ 物理 4mm——大窗口不无限放大
            m_size = qMax(6.0, sz);
            applyAnchoredZoom(m_size);
            return;
        }
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
        for (int s = 0; s < 10; ++s) { // 布局沉降：负载高时 resize 竞态（1/6 偶发，加厚）
            QApplication::processEvents();
            QEventLoop lp;
            QTimer::singleShot(10, &lp, &QEventLoop::quit);
            lp.exec();
        }
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
        for (int s = 0; s < 3; ++s) { // 滚动条轨道点击前再沉降（范围/几何竞态）
            QApplication::processEvents();
            QEventLoop lp;
            QTimer::singleShot(10, &lp, &QEventLoop::quit);
            lp.exec();
        }
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
            while (e.machine() != 0)
                e.toggleMachine(); // 测试前提：琥珀机（配色断言以 kInk 为准）
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
                qWarning("selftest FAIL: CRT text color not amber (machine=%d)",
                         e.machine());
                return false;
            }
            if (e.palette().color(QPalette::Base) != Crt::kBg) {
                qWarning("selftest FAIL: CRT base not the phosphor background");
                return false;
            }
            // M1：视角锁定——进显即锁定，切换翻转，退出重进再次锁定
            if (!e.crtViewLocked()) {
                qWarning("selftest FAIL: view lock not default-on");
                return false;
            }
            e.toggleViewLock();
            if (e.crtViewLocked()) {
                qWarning("selftest FAIL: view lock toggle failed");
                return false;
            }
            e.toggleViewLock();
            if (!e.crtViewLocked()) {
                qWarning("selftest FAIL: view lock re-toggle failed");
                return false;
            }
            // M4.5 并入：显·Cmd+0 = 机器原生网格（80 列）；屏幕实体随
            // 追随视角解锁生效（screenEntityOn == !locked）
            e.zoomReset();
            {
                const QFont gf = e.document()->defaultFont();
                const int wantPx = qMax(6, e.viewport()->width() / 80);
                if (qAbs(gf.pixelSize() - wantPx) > 1) {
                    qWarning("selftest FAIL: crt Cmd+0 grid size %d want %d",
                             gf.pixelSize(), wantPx);
                    return false;
                }
            }
            e.toggleViewLock(); // 解锁 → 屏幕实体生效
            if (!e.screenEntityOn()) {
                qWarning("selftest FAIL: screen entity not tied to unlocked view");
                return false;
            }
            e.toggleViewLock(); // 重新锁定 → 实体关（干净完美视角）
            // M2：切换计算机——默认琥珀，切绿磷（文字/底色/字体随调色板与
            // 出厂字库），切回
            if (e.crtPalette().ink != Crt::kInk) {
                qWarning("selftest FAIL: default machine not amber");
                return false;
            }
            const QString famAmber = e.document()->defaultFont().family();
            e.toggleMachine();
            if (e.crtPalette().ink != Crt::kGreen.ink
                || e.palette().color(QPalette::Text) != Crt::kGreen.ink
                || e.palette().color(QPalette::Base) != Crt::kGreen.bg
                || e.document()->defaultFont().family() == famAmber) {
                qWarning("selftest FAIL: green machine palette/font not applied");
                return false;
            }
            e.toggleMachine(); // C64 真彩（蓝屏 + 16 色逐字符前景色）
            if (e.crtPalette().ink != Crt::kC64.ink
                || e.palette().color(QPalette::Text) != Crt::kC64.ink
                || e.palette().color(QPalette::Base) != Crt::kC64.bg
                || e.machine() != 2 || !e.colorMachine()) {
                qWarning("selftest FAIL: C64 machine palette not applied");
                return false;
            }
            e.toggleMachine(); // IBM PC 白磷（CGA 白字，FSEX302 字库）
            if (e.crtPalette().ink != Crt::kWhite.ink
                || e.palette().color(QPalette::Text) != Crt::kWhite.ink
                || e.palette().color(QPalette::Base) != Crt::kWhite.bg
                || e.machine() != 3) {
                qWarning("selftest FAIL: white machine palette not applied");
                return false;
            }
            e.toggleMachine();
            if (e.crtPalette().ink != Crt::kInk
                || e.palette().color(QPalette::Text) != Crt::kInk
                || e.palette().color(QPalette::Base) != Crt::kBg) {
                qWarning("selftest FAIL: amber machine not restored");
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
                // 块状反相光标有焦点时会把首字格反相、吃掉顶部琥珀——
                // 光标挪到文末，让顶部断言照旧测文字本身
                QTextCursor endC = e.textCursor();
                endC.movePosition(QTextCursor::End);
                e.setTextCursor(endC);
                QApplication::processEvents();
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
            // 颜色分类取证：琥珀透色（r 主导、g 中量、b 近零——颜色穿过
            // 竖纹亮度纹理）、暗底、无蓝泛滥（坏管线 = 蓝通道点燃）
            {
                int amber = 0, blue = 0, dark = 0, other = 0;
                for (int y = 0; y < e.height(); ++y)
                    for (int x = g; x < e.width() - 30; ++x) {
                        const QRgb px = img.pixel(x, y);
                        const int r = qRed(px), gr = qGreen(px), b = qBlue(px);
                        if (r > 150 && gr > 100 && b < 90) ++amber;
                        else if (b > r + 40 && b > gr + 40 && b > 100) ++blue;
                        else if (r < 60 && gr < 60 && b < 60) ++dark;
                        else ++other;
                    }
                qInfo("CRT-COLORS amber=%d blue=%d dark=%d other=%d", amber, blue, dark, other);
                if (amber < 500) {
                    qWarning("selftest FAIL: amber phosphor not lighting (%d)", amber);
                    return false;
                }
                if (blue > 100) {
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
            // M1：退出重进显 → 视角锁定重置
            e.toggleCrt();
            QApplication::processEvents();
            const bool relocked = e.crtViewLocked();
            e.toggleCrt();
            QApplication::processEvents();
            if (!relocked) {
                qWarning("selftest FAIL: view lock not reset on re-entering CRT");
                return false;
            }
        }
        // 格式化库（自 AsciiTools 移植）：框/压行/还原/路径树往返/居中
        {
            e.setPlainText(QStringLiteral("無无\nA\n"));
            e.selectAll();
            e.formatBox(0); // 单线框（字体真实推进对齐）
            const QString boxed = e.toPlainText();
            if (!boxed.startsWith(QStringLiteral("┌"))
                || !boxed.endsWith(QStringLiteral("┘\n"))
                || !boxed.contains(QStringLiteral("無无"))) {
                qWarning("selftest FAIL: box render wrong: %s",
                         boxed.toUtf8().constData());
                return false;
            }
            e.setPlainText(QStringLiteral("一 二\n三\n\n四\n"));
            e.selectAll();
            e.joinLinesTo(); // 压行
            if (e.toPlainText() != QStringLiteral("一 二 三 四")) {
                qWarning("selftest FAIL: join lines wrong");
                return false;
            }
            e.selectAll();
            e.restoreLines(); // 还原 = 原样恢复（可逆）
            if (e.toPlainText() != QStringLiteral("一 二\n三\n\n四\n")) {
                qWarning("selftest FAIL: restore lines wrong: %s",
                         e.toPlainText().toUtf8().constData());
                return false;
            }
            e.setPlainText(QStringLiteral("甲。乙！丙？\n"));
            e.selectAll();
            e.restoreLines(); // 无记忆、无代码分隔符 → 按句读切
            if (e.toPlainText() != QStringLiteral("甲。\n乙！\n丙？")) {
                qWarning("selftest FAIL: prose restore wrong: %s",
                         e.toPlainText().toUtf8().constData());
                return false;
            }
            e.setPlainText(QStringLiteral("if (a;b) { x; y } z;"));
            e.selectAll();
            e.restoreLines(); // 还原（代码语义）
            if (!e.toPlainText().contains(QStringLiteral("{\n"))) {
                qWarning("selftest FAIL: restore lines wrong");
                return false;
            }
            e.setPlainText(QStringLiteral("a\nb/c\nb/d/\n"));
            e.selectAll();
            e.pathsToTree(); // 路径 → 树
            const QString tree = e.toPlainText();
            if (tree != QStringLiteral("├── a\n└── b/\n    ├── c\n    └── d/")) {
                qWarning("selftest FAIL: pathsToTree wrong: %s",
                         tree.toUtf8().constData());
                return false;
            }
            e.selectAll();
            e.treeToPaths(); // 树 → 路径（往返必须还原）
            if (e.toPlainText() != QStringLiteral("a\nb/c\nb/d/")) {
                qWarning("selftest FAIL: treeToPaths roundtrip wrong: %s",
                         e.toPlainText().toUtf8().constData());
                return false;
            }
            e.setPlainText(QStringLiteral("a\nb\\c\nb\\d\\\n"));
            e.selectAll();
            e.pathsToTree(); // 反斜杠路径（Windows）同样嵌套
            if (e.toPlainText() != QStringLiteral("├── a\n└── b/\n    ├── c\n    └── d/")) {
                qWarning("selftest FAIL: backslash tree wrong: %s",
                         e.toPlainText().toUtf8().constData());
                return false;
            }
            e.setPlainText(QStringLiteral("無\n"));
            e.selectAll();
            e.centerToWidth(); // 居中：左补空格
            if (!e.toPlainText().startsWith(QLatin1Char(' '))) {
                qWarning("selftest FAIL: centerToWidth no padding");
                return false;
            }
            e.setPlainText(QStringLiteral("無\n"));
        }
        // P3 哨兵：增量重拍与全量重拍像素一致（打一个字 → 脏区重画
        // → 与全量重画逐字节比对；不一致 = 增量漏画，必须查）
        {
            e.setPlainText(QStringLiteral("甲乙丙\n丁戊己\n"));
            e.markSnapshotFullDirty();
            const qreal dpr = e.devicePixelRatioF(); // 与真实管线同构：窗口 DPR
            QImage full1(e.viewport()->size() * dpr, QImage::Format_ARGB32);
            full1.setDevicePixelRatio(dpr);
            full1.fill(Qt::black);
            e.paintTextSnapshot(full1); // 全量基线
            QImage base = full1.copy(); // 增量底 = 上一帧
            e.moveCursor(QTextCursor::End);
            e.insertPlainText(QStringLiteral("無")); // 触发 contentsChange → 脏区
            const auto snap = e.consumeSnapshotDirty();
            const QRect dirty = snap.rect;
            { // 等激发衰减归零（900ms 上限）：时间敏感部分排除出比对
                QEventLoop lp;
                QTimer::singleShot(950, &lp, &QEventLoop::quit);
                lp.exec();
            }
            QImage full2(e.viewport()->size() * dpr, QImage::Format_ARGB32);
            full2.setDevicePixelRatio(dpr);
            full2.fill(Qt::black);
            e.paintTextSnapshot(full2); // 全量对照
            e.paintTextSnapshotRegion(base, dirty); // 增量
            if (base != full2) {
                // 定位首个差异像素
                QString diff;
                const int w = qMin(base.width(), full2.width());
                const int h = qMin(base.height(), full2.height());
                for (int y = 0; y < h && diff.isEmpty(); ++y) {
                    const uchar *a = base.constScanLine(y);
                    const uchar *b = full2.constScanLine(y);
                    for (int x = 0; x < w; ++x) {
                        if (qAbs(int(a[x*4]) - b[x*4]) > 0
                            || qAbs(int(a[x*4+1]) - b[x*4+1]) > 0
                            || qAbs(int(a[x*4+2]) - b[x*4+2]) > 0) {
                            diff = QStringLiteral("(%1,%2) inc=%3,%4,%5 full=%6,%7,%8 dirty=%9,%10,%11,%12")
                                .arg(x).arg(y).arg(a[x*4]).arg(a[x*4+1]).arg(a[x*4+2])
                                .arg(b[x*4]).arg(b[x*4+1]).arg(b[x*4+2])
                                .arg(dirty.x()).arg(dirty.y()).arg(dirty.width()).arg(dirty.height());
                            break;
                        }
                    }
                }
                qWarning("selftest FAIL: incremental snapshot != full snapshot %s",
                         qPrintable(diff));
                return false;
            }
            e.setPlainText(QStringLiteral("無\n"));
        }
        // 撤销基线回归：格式化操作 = 一步撤销（Qt 编辑块合并语义是
        // 本应用的依赖项——升级 Qt 前必过的哨兵）
        {
            e.setPlainText(QStringLiteral("甲\n乙\n丙\n"));
            e.selectAll();
            e.formatBox(0); // 框 = 一步撤销
            const QString boxed2 = e.toPlainText();
            QKeyEvent ku(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
            QApplication::sendEvent(&e, &ku);
            QApplication::processEvents();
            if (e.toPlainText() != QStringLiteral("甲\n乙\n丙\n")) {
                qWarning("selftest FAIL: box not one-undo-step");
                return false;
            }
            e.setPlainText(QStringLiteral("甲乙\n"));
            e.selectAll();
            e.centerToWidth();
            QApplication::sendEvent(&e, &ku);
            QApplication::processEvents();
            if (!e.toPlainText().startsWith(QStringLiteral("甲乙"))) {
                qWarning("selftest FAIL: center not one-undo-step");
                return false;
            }
            e.setPlainText(QStringLiteral("無\n"));
        }
        // CRT 管线冒烟（C64 三色栅 + 行扫描激励）：渲两机各一帧落盘，
        // 供人工/取证核对（shader 编译失败 = 黑帧 + 空图）
        {
            auto waitFrames = [&](int ms) {
                QElapsedTimer clk;
                clk.start();
                while (clk.elapsed() < ms) {
                    QEventLoop lp;
                    QTimer::singleShot(30, &lp, &QEventLoop::quit);
                    lp.exec();
                }
            };
            e.setPlainText(QStringLiteral("無無無 WuWu\nAABBCC 123456\n"));
            for (int m = 0; m < 4; ++m) { // 四机各渲一帧：亮度横比
                while (e.machine() != m)
                    e.toggleMachine();
                e.toggleCrt();
                waitFrames(1600);
                const QImage f = e.crtSnapImage();
                const QString tag = QStringLiteral("crt_m%1").arg(m);
                f.save(QStringLiteral("/tmp/") + tag + QStringLiteral(".png"));
                e.crtShownImage().save(QStringLiteral("/tmp/") + tag + QStringLiteral("_gpu.png"));
                e.toggleCrt();
                if (f.isNull() || f.size().isEmpty()) {
                    qWarning("selftest FAIL: machine %d crt frame empty", m);
                    return false;
                }
            }
            while (e.machine() != 0)
                e.toggleMachine(); // 还原琥珀
        }
        // M3：图片 → 字符画（纯函数验证：合成左白右黑图 → 粗梯度映射）
        {
            QImage simg(64, 32, QImage::Format_ARGB32);
            simg.fill(Qt::black);
            {
                QPainter pp(&simg);
                pp.fillRect(QRect(0, 0, 32, 32), Qt::white);
            }
            const QString art = Ascii::imageToText(simg, 8, 4);
            const QStringList lines = art.split(QLatin1Char('\n'));
            if (lines.size() != 4 || lines[0].size() != 8 || lines[3].size() != 8) {
                qWarning("selftest FAIL: ascii art grid wrong (%d lines, sizes %d/%d)",
                         int(lines.size()), lines.isEmpty() ? -1 : lines[0].size(),
                         lines.size() < 4 ? -1 : lines[3].size());
                return false;
            }
            if (lines[0].at(0) == QLatin1Char(' ') || lines[0].at(7) != QLatin1Char(' ')) {
                qWarning("selftest FAIL: ascii art luminance mapping wrong (left='%c' right='%c')",
                         lines[0].at(0).toLatin1(), lines[0].at(7).toLatin1());
                return false;
            }
            // 编辑器路径冒烟：空文档插入 → 逐行打印出整页字符画；
            // 非空文档 → 光标处插入（不覆盖）
            auto waitPrint = [&e] {
                for (int guard = 0; e.asciiPrintingDbg() && guard < 100; ++guard) {
                    QEventLoop lp;
                    QTimer::singleShot(50, &lp, &QEventLoop::quit);
                    lp.exec(); // 打字机打印 ~35ms/行，等到完
                }
            };
            e.setPlainText(QString());
            e.loadAsciiImage(simg);
            waitPrint();
            {
                const QString doc = e.toPlainText();
                const QStringList dl = doc.split(QLatin1Char('\n'));
                if (dl.size() < 3 || dl[0].isEmpty()) {
                    qWarning("selftest FAIL: ascii art editor path produced empty doc");
                    return false;
                }
                const QString rampChars = QStringLiteral(" .:*#@.,-~:;=!*#$@");
                bool onlyRamp = true;
                QChar badChar;
                for (const QChar ch : doc) {
                    if (ch != QLatin1Char('\n') && !rampChars.contains(ch)) {
                        badChar = ch;
                        onlyRamp = false;
                        break;
                    }
                }
                if (!onlyRamp) {
                    qWarning("selftest FAIL: ascii art editor path has non-ramp chars (first=%04x len=%d)",
                             badChar.unicode(), int(doc.size()));
                    return false;
                }
            }
            e.setPlainText(QStringLiteral("無\n"));
            {
                e.moveCursor(QTextCursor::End);
                e.loadAsciiImage(simg);
                waitPrint();
                const QString doc = e.toPlainText();
                if (!doc.startsWith(QStringLiteral("無\n")) || doc.size() < 10) {
                    qWarning("selftest FAIL: ascii art insert overwrote existing text");
                    return false;
                }
                // 插入后缩放压力段（回归：插入图片后 Cmd+=/- / 捏合闪退）
                for (int z = 0; z < 4; ++z) {
                    e.zoom(1);
                    QApplication::processEvents();
                }
                for (int z = 0; z < 4; ++z) {
                    e.zoom(-1);
                    QApplication::processEvents();
                }
                if (e.toPlainText().size() < 10) {
                    qWarning("selftest FAIL: ascii art lost after zoom");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n")); // 还原，防污染后续
            }
            // 回归：画布外文字共存 → 切编（Cmd+B）重印不得吞掉画布外文字
            {
                e.setPlainText(QStringLiteral("开头文字\n"));
                e.moveCursor(QTextCursor::End);
                e.loadAsciiImage(simg);
                waitPrint();
                e.toggleCodeMode(); // 切编（用户误报 Cmd+B 吞字符）
                waitPrint();
                if (!e.toPlainText().startsWith(QStringLiteral("开头文字\n"))) {
                    qWarning("selftest FAIL: code toggle ate surrounding text");
                    return false;
                }
                e.toggleCodeMode();
                waitPrint();
                if (!e.toPlainText().startsWith(QStringLiteral("开头文字\n"))) {
                    qWarning("selftest FAIL: code exit ate surrounding text");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归（用户事故复现）：空(Cmd+N)清光 → 撤销复原 → 重勾立为图
            // 必须真正复活画布态（NoWrap 不叠行 + 缩放可用）——不得假勾
            {
                e.setPlainText(QStringLiteral("开头文字\n"));
                e.moveCursor(QTextCursor::End);
                e.loadAsciiImage(simg);
                waitPrint();
                const QString before = e.toPlainText();
                QKeyEvent kn(QEvent::KeyPress, Qt::Key_N, Qt::ControlModifier);
                QApplication::sendEvent(&e, &kn); // 空：清空全部文字
                QApplication::processEvents();
                if (!e.toPlainText().isEmpty() || e.m_asciiActive) {
                    qWarning("selftest FAIL: kong did not clear/end art");
                    return false;
                }
                QKeyEvent kz(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
                QApplication::sendEvent(&e, &kz); // 撤销 → 复原
                QApplication::processEvents();
                if (e.toPlainText() != before) {
                    qWarning("selftest FAIL: kong undo did not restore doc (%d vs %d)",
                             int(e.toPlainText().size()), int(before.size()));
                    return false;
                }
                QTextCursor nc = e.textCursor();
                nc.clearSelection();
                e.setTextCursor(nc);
                e.declareArtFromSelection(); // 无选区 → 复选上次范围
                if (!e.m_asciiActive
                    || e.lineWrapMode() != QPlainTextEdit::NoWrap) {
                    qWarning("selftest FAIL: re-declare after kong-undo not engaged");
                    return false;
                }
                e.zoomAsciiCanvas(1.2); // 画布缩放仍应生效
                waitPrint();
                if (!e.m_asciiActive || e.toPlainText().isEmpty()) {
                    qWarning("selftest FAIL: canvas zoom after re-declare failed");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归：画布操作不进撤销历史（撤销基线）——Cmd+Z 不得蚕食画布，
            // 之后打字的撤销照常
            {
                e.setPlainText(QString());
                e.loadAsciiImage(simg);
                waitPrint();
                const QString artOnce = e.toPlainText();
                e.zoomAsciiCanvas(0.8); // 缩小：必产生重印（放大可能触原生上限）
                waitPrint();
                if (e.toPlainText() == artOnce) {
                    qWarning("selftest FAIL: zoom did not reprint");
                    return false;
                }
                QKeyEvent kz2(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
                QApplication::sendEvent(&e, &kz2); // Cmd+Z：不得触碰画布
                QApplication::processEvents();
                if (e.toPlainText().isEmpty() || !e.m_asciiActive) {
                    qWarning("selftest FAIL: undo ate art after zoom");
                    return false;
                }
                const QString afterZoom = e.toPlainText();
                e.moveCursor(QTextCursor::End);
                QKeyEvent kt(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
                QApplication::sendEvent(&e, &kt); // 画布后打字
                QApplication::processEvents();
                QKeyEvent kz3(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
                QApplication::sendEvent(&e, &kz3); // 撤销打字：照常可用
                QApplication::processEvents();
                if (e.toPlainText() != afterZoom) {
                    qWarning("selftest FAIL: post-art typing not undoable");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归（用户报：打印期间 Cmd+B 会删字符）：打印不等待，
            // 连续多次切编交错打印拍 → 画布必须一字不少
            {
                e.setPlainText(QString());
                e.loadAsciiImage(simg);
                waitPrint();
                const int fullLen = e.toPlainText().size(); // 参考全长
                e.setPlainText(QStringLiteral("开头文字\n"));
                e.moveCursor(QTextCursor::End);
                e.loadAsciiImage(simg); // 打印开始——不等待
                for (int i = 0; i < 10; ++i) {
                    QKeyEvent kb(QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier);
                    QApplication::sendEvent(&e, &kb); // 打印期间切编
                    QEventLoop lp; // 让打印拍与切编交错
                    QTimer::singleShot(5, &lp, &QEventLoop::quit);
                    lp.exec();
                }
                waitPrint();
                if (!e.m_asciiActive || !e.toPlainText().startsWith(QStringLiteral("开头文字\n"))
                    || e.toPlainText().size() != fullLen + 5) {
                    qWarning("selftest FAIL: rapid code toggles during print ate text (cc=%d want %d)",
                             int(e.toPlainText().size()), fullLen + 5);
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归：打印期间切编（高亮器 rehighlight 曾误触发反激活）→
            // 画布态存活、退出编存活、无选区立为图可复选上次范围
            {
                e.setPlainText(QString());
                e.loadAsciiImage(simg); // 打印开始
                e.toggleCodeMode();     // 打印期间切编
                waitPrint();
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: code-mode toggle deactivated art");
                    return false;
                }
                e.toggleCodeMode();     // 退出编
                waitPrint();
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: code-mode exit deactivated art");
                    return false;
                }
                QTextCursor nc = e.textCursor();
                nc.clearSelection();
                e.setTextCursor(nc);
                e.declareArtFromSelection(); // 无选区 → 复选上次范围
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: no-selection declare failed");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n")); // 还原
            }
            // C64 真彩路径：颜色与字符一一对应、含黑白两端色
            {
                QVector<QRgb> cols;
                const QString cart = Ascii::imageToTextColors(simg, 8, 4, cols,
                                                              Crt::kC64Colors);
                const int chars = cart.count(QStringLiteral("\n")) * -1 + cart.size();
                if (cols.size() != chars || cols.size() < 20) {
                    qWarning("selftest FAIL: color art size mismatch (%d vs %d)",
                             int(cols.size()), chars);
                    return false;
                }
                bool hasWhite = false, hasBlack = false;
                for (QRgb c : cols) {
                    if (qRed(c) > 200 && qGreen(c) > 200 && qBlue(c) > 200)
                        hasWhite = true;
                    if (qRed(c) < 30 && qGreen(c) < 30 && qBlue(c) < 30)
                        hasBlack = true;
                }
                if (!hasWhite || !hasBlack) {
                    qWarning("selftest FAIL: color quantization lost black/white");
                    return false;
                }
            }
            // 「立为图」：任意选区 → 立为图 → 画布缩放（再打印）可用；
            // 无选区时复选上次范围
            {
                e.setPlainText(QStringLiteral("一二三\n四五六\n"));
                e.selectAll();
                e.declareArtFromSelection();
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: declare-art did not activate");
                    return false;
                }
                e.zoomAsciiCanvas(1.2);
                waitPrint();
                if (e.toPlainText().isEmpty()) {
                    qWarning("selftest FAIL: declare-art canvas zoom emptied doc");
                    return false;
                }
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: canvas zoom deactivated art");
                    return false;
                }
                e.zoomAsciiCanvas(1.2); // 第二次画布缩放仍应生效（再打印）
                waitPrint();
                if (!e.m_asciiActive || e.toPlainText().isEmpty()) {
                    qWarning("selftest FAIL: second canvas zoom failed");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归（用户报：拖图→摹→压行→还原→立为图→连按换机 = 卡死）
            // 注：压行/还原走菜单快捷键（自测无菜单栏，直接调方法等价）
            {
                e.setPlainText(QString());
                while (e.machine() != 2)
                    e.toggleMachine(); // C64
                e.loadAsciiImage(simg);
                waitPrint();
                const QString doc0 = e.toPlainText();
                e.mo(); // 摹：全选+复制
                e.joinLinesTo(); // 压行
                if (e.toPlainText().count(QLatin1Char('\n')) > 2) {
                    qWarning("selftest FAIL: join did not join");
                    return false;
                }
                e.restoreLines(); // 还原
                if (e.toPlainText() != doc0) {
                    qWarning("selftest FAIL: restore did not restore");
                    return false;
                }
                e.selectAll(); // 还原后选区 = 恢复的全文
                e.declareArtFromSelection(); // 立为图
                if (e.m_asciiBaseCols > 400) {
                    qWarning("selftest FAIL: declare baseCols exploded (%d)",
                             e.m_asciiBaseCols);
                    return false;
                }
                for (int i = 0; i < 8; ++i) {
                    e.toggleMachine(); // 换机连按（卡死场景）
                    QApplication::processEvents();
                }
                waitPrint();
                if (!e.toPlainText().isEmpty() && e.m_asciiActive) {
                    // 画布在场且非空：换机重印应已完成
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
            // 回归（用户报：格式化→重勾立为图后缩放/换机失效、真彩泄漏）
            {
                e.setPlainText(QString());
                while (e.machine() != 2)
                    e.toggleMachine(); // C64
                e.loadAsciiImage(simg);
                waitPrint();
                if (e.m_asciiColors.isEmpty()) {
                    qWarning("selftest FAIL: c64 art produced no colors");
                    return false;
                }
                e.yan(); // 格式化（言）→ 反激活 + 叠行（预期）
                if (e.m_asciiActive) {
                    qWarning("selftest FAIL: yan did not deactivate art");
                    return false;
                }
                e.declareArtFromSelection(); // 无选区 → 复选上次范围（恢复路径）
                if (!e.m_asciiActive) {
                    qWarning("selftest FAIL: re-declare did not reactivate");
                    return false;
                }
                e.zoomAsciiCanvas(1.2); // 缩放必须可用
                waitPrint();
                if (!e.m_asciiActive || e.toPlainText().isEmpty()) {
                    qWarning("selftest FAIL: zoom after re-declare failed");
                    return false;
                }
                const QString afterZoom = e.toPlainText();
                e.zoomAsciiCanvas(0.8);
                waitPrint();
                if (e.toPlainText() == afterZoom) {
                    qWarning("selftest FAIL: second zoom after re-declare no-op");
                    return false;
                }
                // 换机不得泄漏真彩：非 C64 机上文档不得再带彩色前景
                e.yan(); // 再反激活（画布不在场）
                while (e.machine() != 0)
                    e.toggleMachine(); // 琥珀
                QTextCursor fc(e.document());
                fc.movePosition(QTextCursor::NextCharacter);
                if (fc.charFormat().foreground().style() != Qt::NoBrush) {
                    qWarning("selftest FAIL: c64 colors leaked to amber machine");
                    return false;
                }
                e.setPlainText(QStringLiteral("無\n"));
            }
        }
        // 字体管理：空/不存在目录 → 扫描为空；循环后族名永不为空（出厂回退）
        {
            const QStringList none = Editor::scanFontFamilies(
                QStringLiteral("/nonexistent-naught-fonts-dir"));
            if (!none.isEmpty()) {
                qWarning("selftest FAIL: font scan of missing dir not empty");
                return false;
            }
            const QString f0 = e.crtFontFamily();
            e.cycleCrtFont(+1); // 用户文件夹无论有无字体，族名都必须可用
            if (e.crtFontFamily().isEmpty()) {
                qWarning("selftest FAIL: font family empty after cycle");
                return false;
            }
            e.cycleCrtFont(-1);
            if (e.crtFontFamily() != f0) {
                qWarning("selftest FAIL: font cycle round-trip changed family");
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

    // M3：拖入图片文件 → 字符画；其余拖放按原生文字行为
    static bool isImageUrl(const QMimeData *mime)
    {
        if (!mime || !mime->hasUrls())
            return false;
        const QList<QUrl> urls = mime->urls();
        for (const QUrl &u : urls) {
            const QString path = u.toLocalFile().toLower();
            if (path.endsWith(QLatin1String(".png")) || path.endsWith(QLatin1String(".jpg"))
                || path.endsWith(QLatin1String(".jpeg")) || path.endsWith(QLatin1String(".gif"))
                || path.endsWith(QLatin1String(".webp")) || path.endsWith(QLatin1String(".bmp")))
                return true;
        }
        return false;
    }
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (isImageUrl(event->mimeData()))
            event->acceptProposedAction();
        else
            QPlainTextEdit::dragEnterEvent(event);
    }
    void dropEvent(QDropEvent *event) override
    {
        if (isImageUrl(event->mimeData())) {
            for (const QUrl &u : event->mimeData()->urls()) {
                const QImage img(u.toLocalFile());
                if (!img.isNull()) {
                    loadAsciiImage(img);
                    event->acceptProposedAction();
                    return;
                }
            }
        }
        QPlainTextEdit::dropEvent(event);
    }

    // 全屏/激活后窗口易失焦（macOS 全屏过渡会换 NSWindow）：
    // 快捷键、光标、捏合全依赖编辑器焦点——拿回焦点即自愈
    void changeEvent(QEvent *event) override
    {
        QPlainTextEdit::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange) {
            QTimer::singleShot(0, this, [this] {
                activateWindow();
                setFocus();
                if (m_crt && m_crtView) {
                    // 全屏过渡换 NSWindow 后 Metal 回读可能失联（画面冻结）：
                    // 强制重置管线并同步几何
                    m_crtView->resetPipeline();
                    m_crtView->syncGeometry();
                }
            });
        }
        if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
            setFocus();
        }
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
                if ((event->modifiers() & Qt::ControlModifier)
                    && (event->modifiers() & Qt::MetaModifier)
                    && !(event->modifiers() & Qt::ShiftModifier))
                    commitSuicide(); // 自杀（M6）：⌃⌘N；⌃⇧⌘N 归代理（重生）
                else if (event->modifiers() & Qt::ShiftModifier)
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
                if (event->modifiers() & Qt::ShiftModifier)
                    toggleViewLock(); // 显·追随视角锁定：T 家族 Shift 变体
                else
                    toggleCrt(); // 显：T 是 Tube / Time——显像管，回到过去
                return;
            case Qt::Key_M:
                if (event->modifiers() & Qt::ShiftModifier)
                    toggleMachine(); // 显·切换计算机：M = Machine（琥珀 ↔ 绿磷）
                return;
            case Qt::Key_A:
                if (event->modifiers() & Qt::ShiftModifier)
                    declareArtFromSelection(); // 立为图：选区 → 字符画源图（可调画布）
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
        // Ctrl/Cmd + 滚轮：缩放字符；字符画在场：Shift = 画布缩放（网格）
        if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
            m_wheelAccum += event->angleDelta().y();
            const bool canvas = event->modifiers() & Qt::ShiftModifier;
            while (m_wheelAccum >= 120) {
                if (canvas)
                    zoomAsciiCanvas(1.1);
                else
                    zoom(+1);
                m_wheelAccum -= 120;
            }
            while (m_wheelAccum <= -120) {
                if (canvas)
                    zoomAsciiCanvas(1.0 / 1.1);
                else
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
                    // 换模式即换缩放项目：涂/擦模式捏合控制笔刷，其余控制字号；
                    // 字符画在场时：Shift+捏合 = 画布缩放（网格重渲染），
                    // 普通捏合永远缩放字符
                    if (m_mode == Mode::Draw || m_mode == Mode::Erase)
                        brushScale(factor);
                    else if (m_asciiActive
                             && (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier))
                        zoomAsciiCanvas(factor);
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
                m_crtView->markDirty(); // 视差/墨迹/足迹需即时（update 被 P1 压到环境拍）
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
        const Crt::Palette &pp = crtPalette(); // 调色板（M2：琥珀/绿磷）
        if (m_crt) {
            // 磷光模式配色（着色器层盖住视口，此为兜底）
            pal.setColor(QPalette::Window, pp.bg);
            pal.setColor(QPalette::Base, pp.bg);
            pal.setColor(QPalette::Text, pp.ink);
            pal.setColor(QPalette::Highlight, QColor(0x5C, 0x3E, 0x00, 0xB0));
            pal.setColor(QPalette::HighlightedText, pp.ink);
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
            m_canvas->setInk(m_crt ? pp.ink : (m_dark ? QColor(255, 255, 255) : QColor(0, 0, 0)));
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
        markSnapshotFullDirty(); // 字号变化：全文重排，增量不适用
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
        markSnapshotFullDirty(); // 行号槽/换行变化：全量
        if (m_codeMode == on)
            return;
        m_codeMode = on;
        const bool prev = m_settingAscii;
        m_settingAscii = true; // 高亮器 rehighlight 会发 contentsChanged——程序操作
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
        if (m_asciiActive)
            replaceAsciiArt(); // 画布在场 → 按当前机器/字体重印（防"失去颜色"）
        viewport()->update();
        m_settingAscii = prev;
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
            // 机器字符 ROM：出厂/手选字体；整数像素号（12px 为设计原大），
            // 绿磷 VT323 设计号偏大，放大 1.25×；无抗锯齿
            QFont f = QFont(crtFontFamily());
            f.setPixelSize(qMax(6, qRound(m_size * (m_machine == 0 ? 1.0 : 1.25))));
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
        m_size = std::clamp<qreal>(newSize, 3, 1024); // 下限 3pt：C64 等粗体可进一步缩小
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
        m_blinkHalf = 0;
        m_blinkTimer.start(BLINK_HALF_MS);
        syncNativeCaretWidth();
    }

    // 原生 I 形光标的宽度：显模式下恒 0（块状反相光标由光栅快照自绘，
    // 相位与休眠仍由同一眨眼节拍驱动）；其余模式亮 2px、灭 0、休眠 0。
    void syncNativeCaretWidth()
    {
        const bool awake = m_blinkTimer.isActive() && m_blinkHalf % 2 == 0;
        setCursorWidth((m_crt || !awake) ? 0 : 2);
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
    QElapsedTimer m_exciteClock; // 磷粉激发：最后插入时刻
    int m_excitePos = 0;
    int m_lastCharCount = 1;
    QGraphicsOpacityEffect *m_vFade = nullptr;
    QGraphicsOpacityEffect *m_hFade = nullptr;
    qreal m_fadeOpacity = 1.0;
    qreal m_pinchSmooth = 0.0;
    Canvas *m_canvas = nullptr;
    Mode m_mode = Mode::Normal;
    qreal m_brushSize = 20.0;
    QPointF m_lastMouse = QPointF(-1, -1);
    bool m_viewLock = true; // 显·追随视角锁定（M1）：进显重置，Cmd+Shift+T 切换
    int m_machine = 0;        // 显·切换计算机：0 琥珀 / 1 绿磷 / 2 C64 真彩 / 3 IBM PC 白磷
    QStringList m_userFonts;  // 字体文件夹族名（文件顺序）
    QString m_amberUser;      // 琥珀机器的手选字体（会话内，空 = 出厂）
    QString m_greenUser;      // 绿磷机器的手选字体（会话内，空 = 出厂）
    QString m_c64User;      // C64 的手选字体（会话内，空 = 出厂）
    QImage m_asciiImage;         // M3：字符画源图
    bool m_asciiActive = false;  // 字符画在场且未被手动编辑
    bool m_settingAscii = false; // 程序替换期间置位（抑制反激活）
    qreal m_asciiScale = 1.0;    // 画布倍率（网格行列随倍率）
    int m_asciiBaseCols = 0;     // 插入时刻的基准列数（视口宽/字宽）
    int m_asciiStart = 0;        // 字符画在文档中的起止位置（原位替换用）
    int m_asciiEnd = 0;
    QTimer m_asciiSettleTimer;   // 画布缩放尾拍兜底
    QElapsedTimer m_asciiRenderClock; // 画布缩放 80ms 节流
    QTimer m_asciiPrintTimer;    // 字符画逐行打印
    QStringList m_asciiPrintLines;
    QVector<QRgb> m_asciiColors; // C64 真彩：与字符一一对应的前景色
    int m_asciiPrintIdx = 0;
    int m_asciiLinesPerTick = 1; // 每拍插入的行数（~40 拍打完）
    int m_asciiPrintPos = 0;
    bool m_asciiUndoPaused = false; // 画布程序写入期间撤销记录已暂停
    QString m_joinMemory;      // 压行记住的原文（还原 = 原样恢复）
    QString m_joinJoined;      // 压行后的文本（校验期间未被再编辑）
    bool m_joinMemoryValid = false;
    bool m_asciiPrinting = false;
    int m_lastArtStart = -1;     // 上次立为图/拖图的范围（无选区时复选）
    int m_lastArtEnd = -1;
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
    static inline QString s_crtFamily;   // 出厂琥珀：Fusion Pixel（qrc）
    static inline QString s_greenFamily; // 出厂绿磷：VT323（qrc）
    static inline QString s_whiteFamily; // 出厂白磷：Fixedsys Excelsior（CC0，qrc）
    static inline QString s_c64Family;  // 出厂 C64：Press Start 2P（OFL，qrc）
    static inline QSet<QString> s_registeredFonts; // 已注册字体文件（去重）
    QTimer m_crtSettleTimer;
    QTimer m_scrollSettle;  // 滚动停稳计时：结束后补全量快照
    bool m_scrolling = false;
    QRect m_snapDirty;          // P3：增量快照脏区（编辑器坐标）
    bool m_snapFullDirty = true; // 全量标志（滚动/缩放/换机/首次）
    QRect m_lastCursorRect;     // 上一光标矩形（光标移动也纳入脏区）

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
