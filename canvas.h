// canvas.h —— 画布层：独立于文本，浮于文字之上。笔迹存文档坐标——随滚动平移、
// 不随缩放变化（每笔在落笔瞬间锁定自己的笔宽）；颜色随阴/阳；事件全部穿透。
#pragma once

#include <algorithm>

#include <QAbstractScrollArea>
#include <QColor>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QVector>
#include <QWidget>

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
