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
        m_activeOutline = QPainterPath();
        invalidateCache();
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
        invalidateCache(); // 缓存按旧颜色烘的——换色必须重烘
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
        // 滚动只平移 blit，不重烘（审计风险 1：旧版每次滚动整窗重烘
        // = 方向键/滚动卡顿的主因）
        update();
    }

    void beginStroke(const QPointF &docPos)
    {
        m_activePts.clear();
        m_activePts.append(docPos);
        m_activeOutline = QPainterPath();
        m_activeOutline.addEllipse(docPos, m_brush / 2.0, m_brush / 2.0);
        update();
    }

    void extendStroke(const QPointF &docPos)
    {
        if (m_activePts.isEmpty())
            return;
        if (QLineF(m_activePts.last(), docPos).length() >= 2.0) {
            m_activePts.append(docPos);
            // 增量轮廓：只描新管段并入缓存——平铺式来回画线不再
            // 每帧重算整条轮廓（旧版 O(n²)：用户报"一根线来回画就会卡"）
            QPainterPath seg;
            seg.moveTo(m_activePts.at(m_activePts.size() - 2));
            seg.lineTo(docPos);
            m_activeOutline |= strokeOutline(seg, m_brush);
            update();
        }
    }

    void endStroke()
    {
        if (m_activePts.isEmpty())
            return;
        m_strokes.append(InkStroke{m_brush, m_activeOutline});
        m_activePts.clear();
        m_activeOutline = QPainterPath();
        invalidateCache();
    }

    void clearAll()
    {
        if (m_strokes.isEmpty() && m_activePts.isEmpty())
            return;
        m_strokes.clear();
        m_activePts.clear();
        invalidateCache();
        update();
    }

    // 圆头圆角描边轮廓：口径 + 路径 → 填充用轮廓（画/擦/显示三处共用同一几何）
    static QPainterPath strokeOutline(const QPainterPath &line, qreal width)
    {
        QPainterPathStroker stroker;
        stroker.setWidth(width);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        return stroker.createStroke(line);
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
        s.path = strokeOutline(line, width);
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
            tube = strokeOutline(seg, m_brush);
        } else {
            tube.addEllipse(c, m_brush / 2.0, m_brush / 2.0);
        }
        m_eraseLast = c;
        if (!m_eraseActive)
            m_eraseOriginal = m_strokes; // 会话起点快照
        m_eraseActive = true;
        // 会话累积并集 + 从起点快照重放全集：任何一步的结果 = 一次性
        // 全集减法的结果（拆分后再减 ≠ 减后再拆——顺序拖动若在中间
        // 碎片上继续减，后一段会把前一段打出的洞切碎填回，实心内部
        // 穿孔失效的根因）
        m_eraseUnion |= tube;
        m_strokes = m_eraseOriginal;
        applyErase(m_eraseUnion);
        invalidateCache();
        update();
    }

    void eraseEnd()
    {
        m_eraseActive = false;
        m_eraseUnion = QPainterPath();
    }

    void applyErase(const QPainterPath &tube)
    {
        // Qt 自带几何引擎：轮廓布尔相减，所见即所得，小口径也精确。
        // 包围盒先筛：不与管段相交的笔迹跳过昂贵的布尔运算（大笔量文档的关键）
        const QRectF tubeRect = tube.boundingRect();
        bool changed = false;
        for (int i = m_strokes.size() - 1; i >= 0; --i) {
            const qreal width = m_strokes.at(i).width;
            const QPainterPath before = m_strokes.at(i).path;
            if (!before.boundingRect().intersects(tubeRect))
                continue;
            const QPainterPath after = before.subtracted(tube);
            if (after == before)
                continue;
            changed = true;
            const QVector<QPainterPath> subs = splitSubpaths(after, before);
            m_strokes.removeAt(i);
            for (int k = subs.size() - 1; k >= 0; --k)
                m_strokes.insert(i, InkStroke{width, subs.at(k)});
        }
        if (changed) {
            invalidateCache();
            update();
        }
    }

    // 保留曲线元素地把路径拆成子路径，洞环条件并回（子代理几何审计
    // 的修法）：只把"本次擦除新打出的洞"并回其容器——判定 = 洞环
    // 探针点在擦除前(before)是否实心。既有空心（画出来的洞）在
    // before 里是空心 → 不并回 → 保持独立成片填充 = 填实功能；
    // 新打的洞在 before 里是实心 → 并回 → 奇偶填充保洞 = 实心内部
    // 穿孔生效（用户报：实心内部无法直接擦除，只能外部入侵）。
    static QVector<QPainterPath> splitSubpaths(const QPainterPath &p,
                                               const QPainterPath &before)
    {
        QVector<QPainterPath> loops;
        QPainterPath cur;
        for (int i = 0; i < p.elementCount(); ++i) {
            const QPainterPath::Element &e = p.elementAt(i);
            if (e.isMoveTo()) {
                if (cur.elementCount() > 0)
                    loops.append(cur);
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
            loops.append(cur);
        QVector<bool> merged(loops.size(), false);
        QVector<QPainterPath> out;
        for (int i = 0; i < loops.size(); ++i) {
            if (merged[i])
                continue;
            QPainterPath host = loops.at(i);
            for (int j = 0; j < loops.size(); ++j) {
                if (i == j || merged[j])
                    continue;
                const QPointF probe = loops.at(j).boundingRect().center();
                // 只并"闭环的新洞"（擦除前此处实心 + 环闭合）。判别：
                // 穿孔 = 管段完全在实心内 → 减法产出完整闭合的洞界环；
                // 填实的碎片 = 管段切碎既有洞界 → 开弧（首尾点分离）。
                // 只并闭合环：穿孔保洞、填实保留（独立探针验证）
                const QPainterPath::Element firstE = loops.at(j).elementAt(0);
                const QPainterPath::Element lastE = loops.at(j).elementAt(
                    loops.at(j).elementCount() - 1);
                const bool closed = qAbs(firstE.x - lastE.x) < 1.0
                                    && qAbs(firstE.y - lastE.y) < 1.0;
                // 纯橡皮擦（用户五轮拍板：移除填实功能——橡皮擦就是
                // 橡皮擦）：闭合洞界一律并回容器（并集语义 = 干净的切，
                // 洞不打、空心不填）
                const bool merge = closed && host.contains(probe);
                if (merge) {
                    host.addPath(loops.at(j));
                    merged[j] = true;
                }
            }
            out.append(host);
        }
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
        // 笔迹烘焙缓存：paintEvent 只 blit + 画活跃笔画——每帧成本 O(1)，
        // 不再随笔画数线性增长（用户报：越画越卡）
        if (m_cache.isNull())
            rebuildCache();
        p.drawPixmap(m_cacheOrigin, m_cache);
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
    void invalidateCache() { m_cache = QPixmap(); }

    void rebuildCache()
    {
        QRectF bounds;
        for (const InkStroke &s : m_strokes)
            bounds |= s.path.boundingRect();
        if (bounds.isEmpty())
            bounds = QRectF(QPointF(0, 0), QSizeF(size()));
        const QSize cacheSz(qMax(1, int(bounds.width()) + 2),
                            qMax(1, int(bounds.height()) + 2));
        m_cache = QPixmap(cacheSz);
        m_cache.fill(Qt::transparent);
        m_cacheOrigin = bounds.topLeft() - QPointF(1, 1);
        QPainter p(&m_cache);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(-m_cacheOrigin); // 笔迹在文档坐标——烘焙进文档空间
        for (const InkStroke &s : m_strokes) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_ink);
            p.drawPath(s.path);
        }
    }

    void drawStroke(QPainter &p, const QVector<QPointF> &pts, qreal width) const
    {
        if (pts.isEmpty())
            return;
        // 增量轮廓缓存：作画过程所见 = 松手后所提交，几何唯一且 O(1)
        p.setPen(Qt::NoPen);
        p.setBrush(m_ink);
        p.drawPath(m_activeOutline);
    }

    QColor m_ink = QColor(0, 0, 0);
    qreal m_brush = 20.0;
    QPointF m_offset;
    QPoint m_vpOffset;
    QVector<InkStroke> m_strokes;
    QVector<QPointF> m_activePts;
    QPainterPath m_activeOutline; // 活跃笔画增量轮廓缓存（O(1) 作画）
    QPixmap m_cache; // 笔迹烘焙缓存（只随内容变化重烘；滚动平移 blit）
    QPointF m_cacheOrigin; // 缓存在文档坐标的原点
    QPointF m_eraseLast;
    QPainterPath m_eraseUnion; // 橡皮会话管段并集（从起点快照重放全集）
    QVector<InkStroke> m_eraseOriginal; // 会话起点笔迹快照
    bool m_eraseActive = false;
    bool m_fpVisible = false;
    bool m_fpErase = false;
    QPointF m_fpPos;
};
