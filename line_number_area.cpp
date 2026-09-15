// line_number_area.cpp —— 行号区实现：用 blockBoundingGeometry 与 contentOffset
// 走查，与文字共享同一几何。
#include "line_number_area.h"

#include "editor.h"

#include <QFontMetrics>
#include <QPainter>
#include <QTextBlock>

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
    painter.setPen(m_editor->crtOn()
                       ? m_editor->crtPalette().inkDim // 随调色板（M2：琥珀/绿磷）
                       : (m_editor->isDark() ? QColor(0x6a, 0x6a, 0x6a)
                                             : QColor(0xb0, 0xb0, 0xb0)));
    painter.setFont(m_editor->displayFont());
    const QFontMetricsF fm(painter.font());
    const qreal rightEdge = width() - 6.0;

    // 几何对齐 = 结构对齐：行号落在文字自己的基线上。
    // 基线 = 块顶 + 首行 y + QTextLine::ascent —— 与 QPlainTextEdit 内部
    // QTextLine::draw 完全同一坐标，无任何启发式偏移（废除 optical 下沉）。
    QTextBlock block = m_editor->firstVisibleBlockPub();
    int blockNumber = block.blockNumber();
    qreal top = m_editor->blockBoundingGeometryPub(block)
                    .translated(m_editor->contentOffsetPub())
                    .top();
    qreal bottom = top + m_editor->blockBoundingRectPub(block).height();
    while (block.isValid() && top <= event->rect().bottom()) {
        // 文末空块是"纸的余白"：文字侧零视觉高度（不可入），行号也不画——
        // 否则它会以 blockBoundingRect 的完整行高（19px）撑出一个幽灵行号
        const bool phantomTail = block == m_editor->document()->lastBlock()
            && block.length() == 1 && m_editor->document()->characterCount() >= 2;
        if (block.isVisible() && bottom >= event->rect().top() && !phantomTail) {
            qreal baseline = top;
            const QTextLayout *tl = block.layout();
            if (tl && tl->lineCount() > 0) {
                const QTextLine line = tl->lineAt(0);
                baseline += line.y() + line.ascent();
            } else {
                baseline += fm.ascent();
            }
            const QString number = QString::number(blockNumber + 1);
            // drawText(QPointF)：y 即基线；右缘由精确字符宽度推进，等宽/变宽皆准
            painter.drawText(QPointF(rightEdge - fm.horizontalAdvance(number), baseline), number);
        }
        block = block.next();
        top = bottom;
        bottom = top + m_editor->blockBoundingRectPub(block).height();
        ++blockNumber;
    }
}
