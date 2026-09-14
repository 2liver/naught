// 「無」(naught) —— 空白。打开即写，关闭即无。
// 右键 / 双指点按：摹 · 空 ── 阴 · 阳
// Ctrl/Cmd+S：摹（全选并复制）   Ctrl/Cmd+N：空（清空，可撤销）
// Ctrl/Cmd+= / -：字号缩放（按住加速，步长随字号等比增长）  Ctrl/Cmd+0：复位
// Ctrl/Cmd+滚轮、触控板捏合：缩放；触控板横向平移 / Shift+滚轮：横向滚动

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QNativeGestureEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStyleHints>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QWheelEvent>

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

        viewport()->installEventFilter(this);

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

    void zoom(int delta)
    {
        m_size = std::clamp(m_size + delta, 6, 1024);
        applyZoom();
    }

    void zoomTo(int size)
    {
        m_size = std::clamp(size, 6, 1024);
        applyZoom();
    }

    void zoomReset()
    {
        m_size = m_baseSize;
        applyZoom();
    }

    // 自检（CI/本地验证）：确认 O(1) 缩放（文档默认字号）对既有文本生效。
    static bool selftest()
    {
        Editor e;
        e.setPlainText(QStringLiteral("無"));
        const QTextBlock block = e.document()->firstBlock();
        const qreal h1 = e.document()->documentLayout()->blockBoundingRect(block).height();
        e.zoomTo(200);
        const qreal h2 = e.document()->documentLayout()->blockBoundingRect(block).height();
        if (!(h2 > h1 * 2.0)) {
            qWarning("selftest FAIL: h1=%f h2=%f", h1, h2);
            return false;
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

        menu.exec(event->globalPos());
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
            switch (event->key()) {
            case Qt::Key_S:
                mo();
                return;
            case Qt::Key_N:
                kong();
                return;
            case Qt::Key_Equal:
            case Qt::Key_Plus:
                if (event->isAutoRepeat())
                    return; // 按住时的重复交给加速定时器
                zoom(+1);
                startHold(+1);
                return;
            case Qt::Key_Minus:
                if (event->isAutoRepeat())
                    return;
                zoom(-1);
                startHold(-1);
                return;
            case Qt::Key_0:
                zoomReset();
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
        // 触控板横向平移 / Shift+滚轮：横向滚动（单字溢出窗口时可用）
        QScrollBar *hbar = horizontalScrollBar();
        int dx = event->angleDelta().x();
        if (dx == 0 && (event->modifiers() & Qt::ShiftModifier))
            dx = event->angleDelta().y();
        if (dx != 0 && hbar->isVisible()) {
            hbar->setValue(hbar->value() - dx);
            return;
        }
        QPlainTextEdit::wheelEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // 触控板捏合缩放（macOS 原生手势；Windows 精确触摸板同路径）
        if (watched == viewport() && event->type() == QEvent::NativeGesture) {
            const auto *ng = static_cast<QNativeGestureEvent *>(event);
            switch (ng->gestureType()) {
            case Qt::BeginNativeGesture:
                m_pinchBase = m_size;
                m_pinchLast = 0.0;
                m_pinchCalibrated = false;
                break;
            case Qt::ZoomNativeGesture: {
                const qreal v = ng->value();
                if (!m_pinchCalibrated) {
                    // 首个事件只记基准：不同平台 value 起点不同（0 或 1），
                    // 一律按“相对上一步的倍率”计算，避免瞬间跳变
                    m_pinchLast = v;
                    m_pinchCalibrated = true;
                    break;
                }
                if (v == m_pinchLast)
                    break;
                const qreal factor = (1.0 + v) / (1.0 + m_pinchLast);
                m_pinchLast = v;
                zoomTo(int(std::lround(m_size * factor)));
                break;
            }
            default:
                break;
            }
            return true;
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

        const QString handle = m_dark ? QStringLiteral("#4a4a4a") : QStringLiteral("#b8b8b8");
        const QString sheet = QStringLiteral(
            "QScrollBar:vertical{background:transparent;width:10px;margin:2px;}"
            "QScrollBar:vertical:hover{width:14px;}"
            "QScrollBar::handle:vertical{background:%1;border-radius:4px;min-height:32px;}"
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
            "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"
            "QScrollBar:horizontal{background:transparent;height:10px;margin:2px;}"
            "QScrollBar:horizontal:hover{height:14px;}"
            "QScrollBar::handle:horizontal{background:%1;border-radius:4px;min-width:32px;}"
            "QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}"
            "QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{background:transparent;}")
            .arg(handle);
        verticalScrollBar()->setStyleSheet(sheet);
        horizontalScrollBar()->setStyleSheet(sheet);
    }

    void applyZoom()
    {
        // O(1)：只改文档默认字号并标脏，重排由 Qt 惰性完成（仅可见区域）。
        QFont f = m_baseFont;
        f.setPointSize(m_size);
        document()->setDefaultFont(f);
        document()->markContentsDirty(0, document()->characterCount());
        setFont(f);
    }

    void startHold(int dir)
    {
        m_holdDir = dir;
        m_holdInterval = 70;
        m_holdTimer.start(m_holdInterval);
    }

    QFont m_baseFont;
    int m_baseSize = 12;
    int m_size = 12;
    bool m_dark = false;
    QTimer m_holdTimer;
    int m_holdDir = 1;
    int m_holdInterval = 70;
    int m_wheelAccum = 0;
    int m_pinchBase = 12;
    qreal m_pinchLast = 0.0;
    bool m_pinchCalibrated = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("無"));
    app.setApplicationDisplayName(QStringLiteral("無"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/naught.png")));

    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--selftest"))
        return Editor::selftest() ? 0 : 1;

    Editor editor;
    editor.setWindowTitle(QString());
    editor.resize(900, 600);
    editor.show();
    return app.exec();
}
