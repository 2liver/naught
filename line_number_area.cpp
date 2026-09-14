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
