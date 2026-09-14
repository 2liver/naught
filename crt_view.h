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

class Editor;

class CrtView : public QWidget {
public:
    explicit CrtView(Editor *editor);
    ~CrtView() override;
    void markDirty(bool force = false);
    void syncGeometry();
    QImage frameImage() const { return m_pending; }

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void ensureRhi();
    void releaseGpu();
    void renderFrame();

    Editor *m_editor = nullptr;
    QRhi *m_r = nullptr;
    QRhiTexture *m_colorTex = nullptr;
    QRhiTextureRenderTarget *m_rt = nullptr;
    QRhiRenderPassDescriptor *m_rp = nullptr;
    QRhiGraphicsPipeline *m_ps = nullptr;
    QRhiShaderResourceBindings *m_srb = nullptr;
    QRhiBuffer *m_pxbuf = nullptr;
    QRhiBuffer *m_ubuf = nullptr;
    QImage m_pending; // CPU 合成快照（上传源）
    QImage m_shown;   // 最近一帧 GPU 输出（paintEvent 绘制）
    bool m_forceNow = false;
    bool m_readbackInFlight = false;
    QElapsedTimer m_sinceRefresh;
    QSize m_texSize;
    QTimer m_frameTimer;
};
