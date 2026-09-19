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
#include <QHash>
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
#include "snapshot_compositor.h"
#include "zen_scroll_bar.h"

class Editor : public QPlainTextEdit, public CrtSnapshotSource {
    friend class SnapshotCompositor;
public:
    // CrtSource 几何（窄接口）：
    QRect sourceRect() const override { return rect(); }
    QRect cursorCellRect() const override
    {
        QRect cell = cursorRect(textCursor());
        // 块宽 = max(标准格宽, 光标位字符的推进宽)：窄字符（i/l/标点）
        // 用满格块（防细窄竖条），宽字符（CJK 双格）整字覆盖（用户报
        // "压在字上压不全、只有半边字"——旧版两版各错一边，取 max 两边都对）
        const QChar ch = document()->characterAt(textCursor().position());
        const qreal charAdv = (ch.isNull() || ch == QChar::ParagraphSeparator)
                                  ? 0.0
                                  : fontMetrics().horizontalAdvance(ch);
        const qreal cellAdv = fontMetrics().horizontalAdvance(QLatin1Char('M'));
        cell.setWidth(qMax(1, qCeil(qMax(cellAdv, charAdv))));
        return cell.translated(viewport()->pos());
    }
    bool cursorOnGlyph() const override
    {
        const QChar ch = document()->characterAt(textCursor().position());
        return !ch.isNull() && ch != QChar::ParagraphSeparator;
    }
    bool sourceHasText() const override { return document()->characterCount() > 2; }
    bool cursorVisible() const override
    {
        // 选区激活时光标不叠加（用户报：光标那格"透明的、没有选中该有
        // 的样子"——块光标对已反相的选区二次反相 = 还原成普通字 = 亮块
        // 里的洞；选区本身就是那一格该有的样子，光标退场，选区结束
        // 后恢复眨眼块光标）
        return m_crt && hasFocus() && !textCursor().hasSelection()
               && m_blinkTimer.isActive() && m_blinkHalf % 2 == 0;
    }
    QSize sourceViewportSize() const override { return viewport() ? viewport()->size() : QSize(); }
    QWidget *sourceWidget() const override { return const_cast<Editor *>(this); }
    CrtConfig config() const override
    {
        CrtConfig c;
        c.palette = &crtPalette();
        c.machine = m_machine;
        c.scrolling = m_scrolling;
        c.viewLocked = m_viewLock;
        c.screenEntity = m_crt && !m_viewLock;
        c.lastMouse = m_lastMouse;
        c.viewMoving = m_crt && m_mouseMoveClock.isValid() && m_mouseMoveClock.elapsed() < 150;
        c.typing = m_crt && m_inputClock.isValid() && m_inputClock.elapsed() < 300;
        c.drawing = m_crt && m_inkSession; // 涂/擦按住期间
        c.fading = m_crt && m_fadeTimer.isActive(); // 滚动条淡出中
        return c;
    }
    Editor()
        : QPlainTextEdit()
        , m_holdTimer(this)
        , m_compositor(*this)
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
        m_machineSettle.setSingleShot(true);
        connect(&m_machineSettle, &QTimer::timeout, this, [this] {
            const bool prev = m_settingAscii;
            m_settingAscii = true;
            applyZoom(); // 字体随机器（重排只此一次）
            if (m_asciiActive)
                replaceAsciiArt(); // 画布在场 → 按新机器重印
            m_settingAscii = prev;
        });
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
        // 周期触发（子代理审计：旧版 setSingleShot(true) 但 timeout 处理器
        // 从不重新 start——唤醒后只亮 750ms 就永久熄灭 = 用户报的
        // "方向键期间光标进入睡眠、以隐形方式移动"的根因）
        m_blinkTimer.setSingleShot(false);
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
            // 子代理审计：方向键导航只标脏区、从不 markDirty——显模式
            // 块光标/选区在导航期间冻结（"Shift+方向键无法选中"）
            if (m_crtView)
                m_crtView->markDirty();
            // 选区变化 → 选区整体区域入脏区 + 余晖冲刷。
            // ①全量重拍的教训（用户报：逐字慢选渲染慢约 1 秒——每键
            //   重画整窗，自动重复 15-30 发/秒把帧队列积压到秒级）；
            // ②旧版只标光标点脏区的教训（Shift 多行选中一块一块从
            //   中间出来）。正解 = 选区整体包围（含中间整行全宽）一次
            //   画全，每拍只画选区几行。
            // ③余晖冲刷（用户报：选区渲染像"先内衣后衣服"——余晖 max
            //   混合把选中前的旧文字压在新区上；选区 = 状态跳变清零）
            if (textCursor().hasSelection()) {
                const QTextCursor c = textCursor();
                QTextCursor sa = c, sb = c;
                sa.setPosition(c.selectionStart());
                sb.setPosition(c.selectionEnd());
                QRect sel = cursorRect(sa).united(cursorRect(sb));
                sel.setLeft(0); // 中间整行 = 全宽选中
                sel.setRight(viewport()->width());
                m_snapDirty |= sel.translated(viewport()->pos());
                if (m_crtView)
                    m_crtView->flushHistory();
            }
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
            if (m_crt)
                m_inputClock.start(); // 输入突发期信号：链节流 + 半分辨率回读
            // 统一撤销日志（时间序）：真实文字变化 = 一步文字撤销。
            // 只在文档撤销栈确实记录了这一步时入日志（availableUndoSteps
            // > 0）——程序性 setPlainText/打印暂停期 = 0，不入日志，
            // 日志与文档栈永不失步
            // 撤销/重做驱动的 contentsChange 不入日志（否则撤销会
            // 被记成新操作并清空重做日志 → 重做失效）
            if (!m_inUndoRedo && document()->availableUndoSteps() > 0) {
                // 程序性重写（setPlainText/清空）会清空文档撤销栈——
                // 日志同步清空（审计风险 2c：陈条目导致空撤）
                m_undoOps.append(true);
                m_redoOps.clear();
                m_inkRedo.clear(); // 审查 P2：文字编辑后墨迹重做栈同步清
            } else if (!m_inUndoRedo && removed > 0
                       && document()->availableUndoSteps() == 0) {
                m_undoOps.clear();
                m_redoOps.clear();
                m_inkRedo.clear();
            }
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
            // 删除时辉光随字退场：激发位落在被删区间内即熄灭——否则
            // 字删了辉光仍在原地亮 ~1.7s，形状像一个字符幽灵（用户报
            // "空删后本该被删的字符常驻、渲染不消失"）
            if (removed > 0 && m_exciteClock.isValid()
                && m_excitePos >= from && m_excitePos < from + removed) {
                m_exciteClock.invalidate();
            }
            m_lastCharCount = cc;
            m_snapDirty |= m_compositor.computeDirty(from, removed, added);
            m_snapDirty |= m_lastCursorRect;
            // 激发辉光（cell ±6px 三圈）超出光标矩形——脏区扩展覆盖
            const int halo = qCeil(fontMetrics().horizontalAdvance(QLatin1Char('M'))) + 12;
            m_lastCursorRect = cursorRect().translated(viewport()->pos())
                                   .adjusted(-halo, -halo, halo, halo);
            m_snapDirty |= m_lastCursorRect;
            // 行号区：文档一变立即重绘，否则清空/换行不会刷新（假行号）
            m_canvas->update();
            if (m_lineNumberArea)
                m_lineNumberArea->update(); // 用户报：换行后的行号不显示，
                // 得再换一行前一行的才出现——行号区缺显式重绘
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
        connect(vsb, &ZenScrollBar::hovered, this, [this](bool on) {
            if (on) {
                scrollActivity();
                // 悬停加宽 = 把手几何瞬变——冲刷余晖（用户报：鼠标
                // 从滚动条侧移出再回来，灰伪影紧贴滚动条旁）
                if (m_crtView)
                    m_crtView->flushHistory();
            }
        });
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
            if (m_crtView) {
                // 停稳冲刷余晖历史：滚动期积累的旧把手位置不得在
                // 停稳后第一帧复活（审计建议 2）
                m_crtView->flushHistory();
                m_crtView->markDirty(true);
            }
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
        markSnapshotFullDirty(); // 墨水进了光栅快照：清墨必须强制重拍，
        // 否则 80ms 节流窗口内旧墨迹残留在 CRT 画面上（用户擦除后
        // 画面滞后 = 本行缺失的根修；暴力闸清墨依赖此标脏）
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
        if (lines.isEmpty())
            return; // 全是空行（用户报：非首行空行上按 ⌘F 闪退——
            // 旧代码 lines.last()/first() 空向量越界 = 段错误；
            // 负向验证：去掉本守卫 → SIGSEGV 复现）
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
        // 首行之前：非首行插在上一行换行符处（不劈块）；首行没有
        // 上一行，直接在文档头插换行符（位置 0）——应用按降序，
        // 0 最后插，不影响此前各插入点（位置法恒有效，无句柄漂移）
        if (lines.first().position() > 0) {
            if (lines.first().previous().length() > 1)
                inserts.append(lines.first().position() - 1);
        } else {
            inserts.append(0);
        }
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
        // 统一撤销（时间序）：文字与墨迹按发生顺序一个栈内互穿。
        // 旧双栈靠 m_lastWasInk 猜"上一操作是不是笔迹"——交叉序列
        //（笔迹→打字→撤销）会跳过笔迹、撤错目标（用户报：撤回的
        // 并非想撤回的）。日志法逐条精确回放。
        if (m_undoOps.isEmpty())
            return;
        const bool text = m_undoOps.takeLast();
        m_redoOps.append(text);
        if (text) {
            m_inUndoRedo = true;
            document()->undo();
            m_inUndoRedo = false;
        } else if (!m_inkUndo.isEmpty()) {
            const InkOp op = m_inkUndo.takeLast();
            m_inkRedo.append(op);
            m_canvas->restore(op.before);
        }
    }

    void redoAll()
    {
        if (m_redoOps.isEmpty())
            return;
        const bool text = m_redoOps.takeLast();
        m_undoOps.append(text);
        if (text) {
            m_inUndoRedo = true;
            document()->redo();
            m_inUndoRedo = false;
        } else if (!m_inkRedo.isEmpty()) {
            const InkOp op = m_inkRedo.takeLast();
            m_inkUndo.append(op);
            m_canvas->restore(op.after);
        }
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
    void paintTextSnapshot(QImage &img) const override
    {
        m_compositor.paint(img);
    }
    // P3：增量重拍——只重画脏区（复用上一帧图像为底）。与全量同构图，
    // 仅以 clip 限制绘制范围；脏区先清底再画（旧光标/旧字符被抹掉）
    void paintTextSnapshotRegion(QImage &img, const QRect &dirty) const override
    {
        m_compositor.paintRegion(img, dirty);
    }

    // 块状反相光标（真机时代的整格闪烁块）：落点所在字格满格点亮成
    // 炽磷色，字符像素按亮度线性映射回底色（保 AA）——反相视频的
    // 双色精确版。原生 I 形在显模式恒不画（syncNativeCaretWidth）。

    // 磷粉激发：新字符三圈软边增亮，~500ms 指数回落（二期三件套·回接）。
    // 在文字之上做加色混合（CompositionMode_Plus）——真机上是磷粉
    // 刚被打中时的过量发光，随后按指数回落到常态。


    qreal brushSize() const { return m_brushSize; }

    void brushUp() { brushStep(+1); }
    void brushDown() { brushStep(-1); }
    void brushDefault() { brushReset(); }

    void toggleMode(Mode m)
    {
        // 切换前终结进行中的会话（子代理状态机审计的根修）：
        // 旧版只切模式不清会话——纯 Shift 画的笔画滞留 m_activePts
        //（橡皮看不见 = 擦不掉）；陈旧的 m_eraseActive/m_eraseOriginal
        // 残留（新会话不重捕快照 → 整块回滚 = 用户报的"换画笔清空
        // 内容且无法复原"的数据丢失）
        if (m_inkSession) {
            m_canvas->endStroke(); // 有活跃笔画先提交
            m_canvas->eraseEnd();  // 清擦除态（union/original 复位）
            const bool hadGrab = m_shiftInkActive;
            if (m_shiftInkActive)
                m_shiftInkActive = false;
            // 真抓取过才释放（审查 R1：不释放会泄漏抓取；无抓取强释放
            // 会破坏视口渲染——两者都错，只有"有抓取才释放"对）
            if (hadGrab)
                viewport()->releaseMouse();
            endInkSession();
        }
        m_mode = (m_mode == m) ? Mode::Normal : m;
        if (m_crtView)
            m_crtView->markDirty(true); // 模式切换强制重拍（快照及时反映新状态）
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
        applyScheme(); // 调色板即时（便宜）
        if (m_crtView)
            m_crtView->flushHistory(); // 旧机磷光幽灵清零：切机瞬间画面即新机
        viewport()->update();
        if (m_canvas)
            m_canvas->update();
        if (m_lineNumberArea)
            m_lineNumberArea->update();
        if (m_crtView)
            m_crtView->markDirty(true);
        m_settingAscii = prev;
        // 换机自动对齐机型原生网格（用户要求：等同自动 ⌘0）——字体
        // 尺寸随机型即刻正确（80/64/40 列网格公式），字体的实际应用
        // 走下方合并定时器（重排只做一次）
        if (m_crt) {
            m_size = crtGridSize();
            m_crtGridActive = true;
        }
        // 重活合并（用户报：⌘⇧M 连点切机会卡死一会——旧版每拍一次
        // 全文档重排 + 画布重印，按住自动重复把主线程排队堵死）。
        // 字体应用/画布重印推迟到停顿 200ms 后，连点期间只做一次
        m_machineSettle.start(200);
    }

    // 屏幕实体（原实验功能，M4.5 并入）：追随视角解锁（非锁定）时生效
    // ——锁定时是干净"完美视角"，解锁后是沉浸的弯曲玻璃屏（不裁字）
    bool screenEntityOn() const { return m_crt && !m_viewLock; }
    // P3 快照增量：打字只重画脏区。consume 一次性取走脏区并复位
    CrtSnapshotSource::SnapDirty consumeSnapshotDirty() override
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
            // 去重：cycleCrtFont 每次重扫——已注册的直接从缓存取族名
            // （不再重复注册，但**族名必须照常收集**——旧版跳过整个
            // 文件导致重扫为空、换字体无反应）
            const auto cached = s_registeredFonts.constFind(path);
            if (cached != s_registeredFonts.constEnd()) {
                families.append(cached.value());
                continue;
            }
            const int id = QFontDatabase::addApplicationFont(path);
            if (id >= 0) {
                const QStringList fams = QFontDatabase::applicationFontFamilies(id);
                if (!fams.isEmpty()) {
                    s_registeredFonts.insert(path, fams.first());
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
            // 不在此处 releaseMouse（第五轮二分定位 + 八轮实机铁证
            // vpLum=15：无抓取时强释放 → 视口渲染被破坏 → 快照失字 →
            // 画面冻结/字隐形/删掉的字常驻）。收口与 toggleMode 同款：
            // 会话终结，真机抓取由 keyReleaseEvent 的 Shift 分支统一释放
            if (m_shiftInkActive) {
                m_shiftInkActive = false;
                if (m_mode == Mode::Draw)
                    m_canvas->endStroke();
                else if (m_mode == Mode::Erase)
                    m_canvas->eraseEnd();
                endInkSession();
                viewport()->releaseMouse(); // 真抓取过才释放（审查 R1：
                // 置 false 后 Shift 释放分支不再放 → 抓取泄漏；此处补放，
                // 与"无抓取强释放"的对偶都安全）
            }
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
        m_crtGridActive = false; // 手动缩放退出网格态
        applyAnchoredZoom(m_size + delta); // 缩放字符（字号）——与无图时一致
    }

    void zoomTo(qreal size)
    {
        applyAnchoredZoom(size);
    }

    // CRT 原生网格的单一公式来源：列数（琥珀 80 / 绿磷 64 / C64·IBM 40）
    // 与窗口宽度 → m_size（activeFont 再乘 1.25 → 像素）。自检断言
    // 同源调用，不再各自推演（旧版三处重复公式曾各自漂移）。
    qreal crtGridSize() const
    {
        const int cols = (m_machine == 0) ? 80 : (m_machine == 1) ? 64 : 40;
        if (m_machine != 2)
            return qMax(6.0, qreal(viewport()->width()) / cols
                             / (m_machine == 0 ? 1.0 : 1.25));
        // C64（用户三轮拍板）：全屏 = 封顶 16（像素 20px）——"正常"尺子；
        // 窗口化 = 尺子 ×（窗口宽/屏宽）——严格按全屏比例缩小对齐，
        // 永远小于全屏字号。旧版"移除封顶"把全屏好字号一起拖大 = 负优化
        qreal screenW = 1.0;
        if (auto *scr = QGuiApplication::primaryScreen())
            screenW = qMax(1.0, qreal(scr->availableGeometry().width()));
        const qreal full = qMin(screenW / 50.0, 16.0);
        const qreal ratio = qBound(0.25, qreal(viewport()->width()) / screenW, 1.0);
        return qMax(6.0, full * ratio);
    }
    // 网格像素字号（activeFont 之后）——自检断言口径
    int crtGridPixelSize() const
    {
        return qMax(6, qRound(crtGridSize() * (m_machine == 0 ? 1.0 : 1.25)));
    }

    void zoomReset()
    {
        if (m_asciiActive) {
            optimizeAsciiCanvas(); // 画布最佳化：网格贴合窗口
            return;
        }
        if (m_crt) {
            // 显·Cmd+0 = 机器原生网格（原实验·字符网格并入）：琥珀 80 列 /
            // 绿磷 64 列 / C64·IBM 40 列——真机的"原生分辨率"。网格态
            // 跟随窗口宽度（resize 重拟合）。无封顶（旧版 C64 封顶 16 →
            // 像素 20px）：封顶让窗口 ≥800px 时与全屏字号相等，违反
            // "窗口化永远小于全屏"（用户明令：窗口模式按全屏比例缩小
            // 对齐，最起码永远更小）。纯列数网格，字号严格 ∝ 宽度。
            // 公式单一来源：crtGridPixelSize()（自检同源，不再重复推演）
            m_size = crtGridSize();
            applyAnchoredZoom(m_size);
            m_crtGridActive = true;
            return;
        }
        applyAnchoredZoom(m_baseSize);
    }

    // 自检（CI/本地验证）：确认 O(1) 缩放、光标最右缘落点、轨道点击转落点均正常。
    static bool selftest();

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
        // 顶行再按上 = 跳文首；底行再按下 = 跳文末（用户要求的方向键
        // 边界跳跃：光标到最上行后继续按"上"→ 首行的首字符之前；
        // 到最下行后继续按"下"→ 尾行的尾字符之后）
        if (event->key() == Qt::Key_Up
            && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier
                                       | Qt::AltModifier | Qt::ShiftModifier))) {
            QTextCursor c = textCursor();
            if (!c.hasSelection() && c.blockNumber() == 0 && c.position() > 0) {
                c.setPosition(0);
                setTextCursor(c);
                wakeCaret();
                return;
            }
        }
        if (event->key() == Qt::Key_Down
            && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier
                                       | Qt::AltModifier | Qt::ShiftModifier))) {
            QTextCursor c = textCursor();
            const int endPos = document()->characterCount() - 1;
            if (!c.hasSelection() && c.blockNumber() == document()->blockCount() - 2
                && c.position() < endPos) {
                c.setPosition(endPos);
                setTextCursor(c);
                wakeCaret();
                return;
            }
        }
        // Shift+↑ 选区扩展语义（用户拍板）：光标 = 移动端（上端），锚点
        // 锁在原选区**远端（下端）**——选区 = 上一行对应列起的半截 + 原
        // 来的整行。旧基类把光标留在原选区下端、锚点在上端 → 原选区被
        // 吞掉只剩半截（用户报"选中上方半行"）。Shift+↓ 保持基类（光标
        // 自然为移动端：向下扩展/向上回缩都正确）
        if (event->key() == Qt::Key_Up
            && event->modifiers() == Qt::ShiftModifier) {
            QTextCursor c = textCursor();
            // 真机取证（用户报：选区语义与离屏探针不符，反复确认全新
            // 进程仍复现——把每次 Shift+↑ 的前后状态落盘，供定位）
            {
                const QTextCursor pre = c;
                const QTextBlock bPre = document()->findBlock(pre.selectionStart());
                const QTextBlock bPreE = document()->findBlock(pre.selectionEnd());
                QFile f(QStringLiteral("/tmp/naught-selup-diag.log"));
                if (f.open(QIODevice::Append | QIODevice::Text)) {
                    f.write(QStringLiteral(
                        "SELUP wrap=%1 blocks=%2 cc=%3 before=[%4,%5 a=%6 p=%7] "
                        "beforeBlocks=[%8..%9] line1=%10 line2=%11\n")
                        .arg(int(lineWrapMode())).arg(document()->blockCount())
                        .arg(document()->characterCount())
                        .arg(pre.selectionStart()).arg(pre.selectionEnd())
                        .arg(pre.anchor()).arg(pre.position())
                        .arg(bPre.blockNumber()).arg(bPreE.blockNumber())
                        .arg(bPre.text().left(12)).arg(bPreE.text().left(12))
                        .toUtf8());
                    f.close();
                }
            }
            if (c.hasSelection()) {
                const int bottom = qMax(c.anchor(), c.position());
                const int top = qMin(c.anchor(), c.position());
                c.setPosition(bottom);   // 先落锚点（下端）
                c.setPosition(top, QTextCursor::KeepAnchor); // 光标到上端
            }
            c.movePosition(QTextCursor::Up, QTextCursor::KeepAnchor);
            setTextCursor(c);
            {
                const QTextCursor post = c;
                QFile f(QStringLiteral("/tmp/naught-selup-diag.log"));
                if (f.open(QIODevice::Append | QIODevice::Text)) {
                    f.write(QStringLiteral("SELUP after=[%1,%2 a=%3 p=%4]\n")
                        .arg(post.selectionStart()).arg(post.selectionEnd())
                        .arg(post.anchor()).arg(post.position()).toUtf8());
                    f.close();
                }
            }
            wakeCaret();
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
                if (event->modifiers() & Qt::ShiftModifier) {
                    toggleMachine(); // 显·切换计算机：M = Machine（琥珀 ↔ 绿磷）
                    return;
                }
                break; // 裸 ⌘M = 系统最小化，落回基类（同 ⌘A 病根，审查补漏）
            case Qt::Key_A:
                if (event->modifiers() & Qt::ShiftModifier) {
                    declareArtFromSelection(); // 立为图：选区 → 字符画源图（可调画布）
                    return;
                }
                break; // 裸 ⌘A = 系统全选，落回基类（同病根：无条件 return 吞键）
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
            case Qt::Key_C:
                if (event->modifiers() & Qt::ShiftModifier) {
                    centerToWidth(); // 居中（⌘⇧C：原仅菜单 QAction，死键）
                    return;
                }
                break; // 裸 ⌘C = 系统复制，落回基类
            case Qt::Key_G:
                if (event->modifiers() & Qt::ShiftModifier) {
                    formatBox(0); // 单线框
                    return;
                }
                break;
            case Qt::Key_H:
                if (event->modifiers() & Qt::ShiftModifier) {
                    formatBox(1); // 双线框
                    return;
                }
                break;
            case Qt::Key_U:
                if (event->modifiers() & Qt::ShiftModifier) {
                    formatBox(2); // 圆角框
                    return;
                }
                break;
            case Qt::Key_V:
                if (event->modifiers() & Qt::ShiftModifier) {
                    formatBox(3); // 粗线框（⌘⇧V 接管系统"粘贴并匹配样式"）
                    return;
                }
                break; // 裸 ⌘V = 系统粘贴，落回基类
            case Qt::Key_J:
                if (event->modifiers() & Qt::ShiftModifier) {
                    joinLinesTo(); // 压成一行
                    return;
                }
                break;
            case Qt::Key_K:
                if (event->modifiers() & Qt::ShiftModifier) {
                    restoreLines(); // 还原为多行
                    return;
                }
                break;
            case Qt::Key_P:
                if (event->modifiers() & Qt::ShiftModifier) {
                    pathsToTree(); // 路径列表 → 树
                    return;
                }
                break;
            case Qt::Key_R:
                if (event->modifiers() & Qt::ShiftModifier) {
                    treeToPaths(); // 树 → 路径列表
                    return;
                }
                break;
            case Qt::Key_Comma:
                if (event->modifiers() & Qt::ShiftModifier) {
                    cycleCrtFont(-1); // 上一字体
                    return;
                }
                break;
            case Qt::Key_Period:
                if (event->modifiers() & Qt::ShiftModifier) {
                    cycleCrtFont(+1); // 下一字体
                    return;
                }
                break;
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
                const QPointF prev = m_lastMouse;
                m_lastMouse = posOf(me);
                // 视图移动追踪：位移 > 3px 视为移动（幽灵加速衰减的触发）
                if ((m_lastMouse - prev).manhattanLength() > 3.0)
                    m_mouseMoveClock.start();
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
            // 选区 = 反相视频（真机时代的标记/反白约定，与光标同哲学）：
            // 块 = 本机墨色、字 = 本机底色——按机型拟真（旧版固定暗琥珀
            // 0x5C3E00 全机通用，且退出显后泄漏到常规模式 = 用户报）
            pal.setColor(QPalette::Highlight,
                         QColor(pp.ink.red(), pp.ink.green(), pp.ink.blue(), 255));
            // 实心（用户报：逐字慢选出现"透明选中框"——半透明先盖、
            // 辉光加工后才显实；真机反白标记 = 实心，这里直接做实）
            pal.setColor(QPalette::HighlightedText, pp.bg);
        } else if (m_dark) {
            pal.setColor(QPalette::Window, QColor(0, 0, 0));
            pal.setColor(QPalette::Base, QColor(0, 0, 0));
            pal.setColor(QPalette::Text, QColor(255, 255, 255));
            // 常规模式 = 系统标准蓝选区（不再继承显模式的琥珀残留）
            pal.setColor(QPalette::Highlight, QColor(0x4A, 0x8C, 0xFF));
            pal.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        } else {
            pal.setColor(QPalette::Window, QColor(255, 255, 255));
            pal.setColor(QPalette::Base, QColor(255, 255, 255));
            pal.setColor(QPalette::Text, QColor(0, 0, 0));
            pal.setColor(QPalette::Highlight, QColor(0x4A, 0x8C, 0xFF));
            pal.setColor(QPalette::HighlightedText, QColor(0, 0, 0));
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
        m_undoOps.append(false);
        if (m_inkUndo.size() > 100) {
            m_inkUndo.removeFirst();
            // 删日志里最早的墨迹条目（不是队首——中间夹着文字步）
            const int idx = m_undoOps.indexOf(false);
            if (idx >= 0)
                m_undoOps.removeAt(idx);
        }
        m_inkRedo.clear();
        m_redoOps.clear();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QPlainTextEdit::resizeEvent(event);
        updateLineNumberArea();
        if (m_crtView)
            m_crtView->syncGeometry(); // 整面覆盖随窗口缩放
        // 显·机器原生网格态：字号跟随窗口宽度（窗口化/全屏同一公式，
        // 不再取决于按键瞬间的旧宽度）
        if (m_crt && m_crtGridActive) {
            m_crtGridActive = false; // 防递归（zoomReset 触发的 resize）
            zoomReset();
        }
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
    QVector<bool> m_undoOps; // 统一撤销日志：true=文字步 false=墨迹步（时间序）
    QVector<bool> m_redoOps;
    bool m_inUndoRedo = false; // 撤销/重做重入保护：contentsChange 不入日志
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
    static inline QHash<QString, QString> s_registeredFonts; // 已注册字体文件 → 族名（去重缓存）
    QTimer m_crtSettleTimer;
    QTimer m_machineSettle;
    QTimer m_scrollSettle;  // 滚动停稳计时：结束后补全量快照
    bool m_scrolling = false;
    bool m_crtGridActive = false; // 显·机器原生网格态：resize 重拟合
    QElapsedTimer m_mouseMoveClock; // 鼠标移动时钟：视图移动期幽灵加速衰减
    QElapsedTimer m_inputClock;     // 输入突发时钟：打字/删除期链节流 + 半分辨率
    SnapshotCompositor m_compositor; // 快照合成器（全量/增量/脏区）
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
