// crt_source.h —— CrtView 与 Editor 之间的窄接口（M7 架构解耦）。
// CrtView 只依赖本接口，不再 include editor.h 单体：编辑器任何改动
// 不再连坐 CRT 管线的重编译；契约集中一处，双方各自演进。
#pragma once

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QWidget>

#include "crt.h"

class CrtSource
{
public:
    virtual ~CrtSource() = default;
    // 快照合成（全量 / 增量）
    virtual void paintTextSnapshot(QImage &img) const = 0;
    virtual void paintTextSnapshotRegion(QImage &img, const QRect &dirty) const = 0;
    // 调色板与机器
    virtual const Crt::Palette &crtPalette() const = 0;
    virtual int machine() const = 0;
    // 画面状态
    virtual bool isScrolling() const = 0;      // 滚动中：快照降载信号
    virtual bool crtViewLocked() const = 0;    // 追随视角锁定（M1）
    virtual bool screenEntityOn() const = 0;   // 屏幕实体（曲率/边框）
    virtual QPointF lastMouseViewport() const = 0; // 人眼代理（反光视差）
    // 快照脏区（P3 增量）
    virtual QRect consumeSnapshotDirty() = 0;
    virtual bool snapshotFullDirty() const = 0;
    // 几何与挂载：CrtView 必须以编辑器为父窗口（alien 覆盖层）
    virtual QRect sourceRect() const = 0;
    virtual QSize sourceViewportSize() const = 0;
    virtual QWidget *sourceWidget() const = 0;
};
