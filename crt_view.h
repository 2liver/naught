// crt_view.h —— 「显」真光学显示层（自有 QRhi·Metal 离屏渲染 + 回读）。
// 架构：普通 alien QWidget 覆盖层。GPU 用我们自己的 QRhi 实例离屏渲染，
// 回读成 QImage 后由 QPainter 绘制——不经过 QRhiWidget、不创建原生窗口、
// 不参与窗口 backing store 的 RHI 合成。指针事件经 WA_TransparentForMouseEvents
// 天然穿透到真实组件（画布层同款路径）——输入即原生，无转发、无 teardown。
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QTimer>
#include <QWidget>

class QRhi;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QRhiRenderPassDescriptor;

#include "crt_source.h"

class CrtView : public QWidget {
public:
    explicit CrtView(CrtSnapshotSource *source);
    ~CrtView() override;
    void markDirty(bool force = false);
    void syncGeometry();
    // 自检闸门：管线可用（后端 + 着色器全部就绪）才跑 GPU 相关断言
    bool pipelineReady() const { return m_ps != nullptr; }
    // 同上，但 Null 后端不算可用（Null 对一切说"成功"却不产出真实帧）
    bool pipelineUsable() const;
    // 全屏/窗口过渡后 Metal 回读可能失联：强制重置管线（下一帧全量重建）
    void resetPipeline()
    {
        m_readbackInFlight = false;
        releaseGpu();
        m_forceNow = true;
        ensureRhi();
    }
    QImage frameImage() const { return m_pending; }
    QImage shownImage() const { return m_shown; } // 着色后回读帧（GPU 真输出）

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void ensureRhi();
    void releaseGpu();
    void renderFrame();

    CrtSnapshotSource *m_source = nullptr;
    QRhi *m_r = nullptr;
    QRhiTexture *m_colorTex = nullptr;
    QRhiTextureRenderTarget *m_rt = nullptr;
    QRhiRenderPassDescriptor *m_rp = nullptr;
    QRhiGraphicsPipeline *m_ps = nullptr;
    QRhiShaderResourceBindings *m_srb = nullptr;
    QRhiBuffer *m_pxbuf = nullptr;
    QRhiBuffer *m_ubuf = nullptr;
    QImage m_pending; // CPU 合成快照（上传源）
    QImage m_prev;    // 上一帧快照（余晖快分量，浅拷贝）
    QImage m_prev2;   // 上上帧快照（余晖慢分量，浅拷贝）
    QImage m_shown;   // 最近一帧 GPU 输出（paintEvent 绘制）
    bool m_forceNow = false;
    bool m_readbackInFlight = false;
    bool m_rhiUnavailable = false; // 后端可用但管线创建失败：本会话渲染层停用
    int m_readbackGen = 0; // 回读代次：看门狗复位后陈旧回调作废
    bool m_renderDirty = false;   // 脏驱动：有变化才整链渲染
    QElapsedTimer m_ambientClock; // 环境动态（滚动带/颗粒/余晖）低帧率拍
    QElapsedTimer m_sinceRefresh;
    QElapsedTimer m_clock;    // 运行秒数（噪声/刷新带的时间源）
    QElapsedTimer m_warmClock; // 入场暖机（showEvent 起拍）
    QElapsedTimer m_readbackClock; // 回读看门狗（超时强制复位）
    QSize m_texSize;
    QTimer m_frameTimer;
};
