// zen_scroll_bar.h —— 自绘滚动条：命中区恒为 18px（从任何一侧都容易接近），
// 闲置时把手 10px、悬停时长满 18px。QSS 无法控制把手宽度（实测），故自绘。
#pragma once

#include <QEnterEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

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
