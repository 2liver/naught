// crt_source.h —— CrtView 与 Editor 之间的窄接口（M7 架构解耦·Q5 收尾）。
// 裂为两层：CrtConfig（每帧的配置值读取，值结构零虚调用开销）+
// CrtSnapshotSource（快照合成/脏区/几何挂载的纯虚接口）。
// CrtView 只依赖本头，不 include editor.h 单体。
#pragma once

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QWidget>

#include "crt.h"

// CRT 渲染每帧所需的配置读取（值快照，帧内一致）
struct CrtConfig {
    const Crt::Palette *palette = nullptr; // 调色板（静态预设，指针稳定）
    int machine = 0;                      // 0 琥珀 / 1 绿磷 / 2 C64 / 3 白磷
    bool scrolling = false;               // 滚动中：快照降载信号
    bool typing = false;                  // 输入突发期（打字/删除连发）：链节流
    bool drawing = false;                 // 画刷会话中（涂/擦按住）：半分辨率回读
    bool fading = false;                  // 滚动条淡出中：余晖清零（把手残影）
    bool viewMoving = false;              // 鼠标移动中：余晖幽灵加速衰减
    bool viewLocked = true;               // 追随视角锁定（M1）
    bool screenEntity = false;            // 屏幕实体（曲率/边框；= 解锁态）
    QPointF lastMouse;                    // 人眼代理（反光视差）
};

class CrtSnapshotSource
{
public:
    virtual ~CrtSnapshotSource() = default;
    // 快照合成（全量 / 增量）
    virtual void paintTextSnapshot(QImage &img) const = 0;
    virtual void paintTextSnapshotRegion(QImage &img, const QRect &dirty) const = 0;
    // 快照脏区（P3 增量）——一次性消费，消除"先读全量再清脏区"的时序约定
    struct SnapDirty {
        bool full = false;
        QRect rect;
    };
    virtual SnapDirty consumeSnapshotDirty() = 0;
    // 光标叠加（CrtView 顶层绘制——不进余晖历史，无残影）：
    virtual QRect cursorCellRect() const = 0;   // 光标单元格（编辑器坐标）
    virtual bool cursorUnderline() const = 0;   // 机型 3 = 下划线光标
    virtual bool cursorOnGlyph() const = 0;     // 光标压在字符上（非空位）
    virtual bool cursorCodeMode() const = 0;    // 编模式（块光标用中性暖白）
    virtual bool cursorVisible() const = 0;     // 焦点 + 眨眼亮拍
    // 几何与挂载：CrtView 必须以编辑器为父窗口（alien 覆盖层）
    virtual QRect sourceRect() const = 0;
    virtual QSize sourceViewportSize() const = 0;
    virtual QWidget *sourceWidget() const = 0;
    // 配置读取（每帧一次）
    virtual CrtConfig config() const = 0;
};
