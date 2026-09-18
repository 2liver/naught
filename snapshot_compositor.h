// snapshot_compositor.h —— 快照合成器（架构深模块化·Q5）。
// 把编辑器控件子树序列化成 CRT 快照图：全量 paint / 增量 paintRegion /
// 脏区计算 computeDirty。与 Editor 互为 friend——访问其私有控件，
// 但把 ~170 行合成逻辑整体迁出 Editor 单体；增量/全量逻辑可独立演进。
#pragma once

#include <QImage>
#include <QRect>

class Editor;
class QPainter;

class SnapshotCompositor
{
public:
    explicit SnapshotCompositor(Editor &editor);
    // 全量合成（磷光底 + 视口 + 激发 + 画布 + 行号 + 滚动条 + 块光标）
    void paint(QImage &img) const;
    // 增量重拍：只重画脏区（复用上一帧为底，clip 限范围）
    void paintRegion(QImage &img, const QRect &dirty) const;
    // P3 脏区：变化块 + 下方位移区（任何编辑都会让下方整体位移）
    QRect computeDirty(int from, int removed, int added) const;

private:
    void paintExcitation(QPainter &p) const;
public:
    // 公开给自检：直接画光标（离屏无焦点时合成器门控会跳过——形状
    // 验证需要确定性入口）
    void paintCursor(QImage &img) const;
    Editor &m_e;
};
