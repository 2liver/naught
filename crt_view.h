// crt_view.h —— 「显」真光学显示层（自有 QRhi·Metal 离屏渲染 + 回读）。
// 架构：普通 alien QWidget 覆盖层。GPU 用我们自己的 QRhi 实例离屏渲染，
// 回读成 QImage 后由 QPainter 绘制——不经过 QRhiWidget、不创建原生窗口、
// 不参与窗口 backing store 的 RHI 合成。指针事件经 WA_TransparentForMouseEvents
// 天然穿透到真实组件（画布层同款路径）——输入即原生，无转发、无 teardown。
//
// GPU 管线（多通道）：CPU 只在脏帧重拍快照并上传为纹理——
//   P1 余晖合成（persist.frag）：max(快照, 历史A·k1, 历史B·k2)，
//      历史 = 三纹理轮转，k = persist^(dt/80ms) 按时间衰减；
//   P2 辉光（downsample + blurH/blurV ×3）：1/4 图 4×4 盒式降采样 +
//       3 轮盒式模糊（与 Crt::phosphorBloom 同模型）；
//   P3 主着色（crt.frag）：内容（最近邻）+ 辉光（线性 max 叠加）→
//       衍射/束斑/聚焦 → 栅网/扫描线/玻璃 → 回读。
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
class QRhiSampler;

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
    QRhiGraphicsPipeline *buildPipeline(const char *fragName,
                                        QRhiShaderResourceBindings *srb,
                                        QRhiRenderPassDescriptor *rp);
    QRhiTextureRenderTarget *makeRt(QRhiTexture *tex);

    CrtSnapshotSource *m_source = nullptr;
    QRhi *m_r = nullptr;
    QRhiTexture *m_colorTex = nullptr;
    QRhiTextureRenderTarget *m_rt = nullptr;
    QRhiRenderPassDescriptor *m_rp = nullptr;
    QRhiGraphicsPipeline *m_ps = nullptr;          // 主着色管线
    // SRB 随余晖历史轮转：cur = m_histFrame%3（主着色绑定 hist[cur]，
    // 余晖绑定 hist[(cur+2)%3]/hist[(cur+1)%3]，降采样绑定 hist[cur]）
    QRhiShaderResourceBindings *m_srbMain[3] = { nullptr, nullptr, nullptr };
    QRhiShaderResourceBindings *m_srbPersist[3] = { nullptr, nullptr, nullptr };
    QRhiShaderResourceBindings *m_srbDown[3] = { nullptr, nullptr, nullptr };
    // 余晖历史：三纹理轮转（读二写一，无同帧读写冲突）
    QRhiTexture *m_histTex[3] = { nullptr, nullptr, nullptr };
    QRhiTextureRenderTarget *m_histRt[3] = { nullptr, nullptr, nullptr };
    QRhiGraphicsPipeline *m_psPersist = nullptr;
    // 辉光小图（1/4 尺寸 ping-pong）
    QRhiTexture *m_glowA = nullptr;
    QRhiTexture *m_glowB = nullptr;
    QRhiTextureRenderTarget *m_glowRtA = nullptr;
    QRhiTextureRenderTarget *m_glowRtB = nullptr;
    QRhiGraphicsPipeline *m_psDown = nullptr;
    QRhiGraphicsPipeline *m_psBlurH = nullptr;
    QRhiGraphicsPipeline *m_psBlurV = nullptr;
    QRhiShaderResourceBindings *m_srbBlurH = nullptr;
    QRhiShaderResourceBindings *m_srbBlurV = nullptr;
    QRhiTexture *m_snapTex = nullptr; // 快照上传纹理
    QRhiSampler *m_samplerNearest = nullptr;
    QRhiSampler *m_samplerLinear = nullptr;
    QRhiBuffer *m_ubuf = nullptr;
    QImage m_pending; // CPU 合成快照（上传源，仅脏帧重拍）
    QImage m_shown;   // 最近一帧 GPU 输出（paintEvent 绘制）
    bool m_forceNow = false;
    bool m_readbackInFlight = false;
    bool m_rhiUnavailable = false; // 后端可用但管线创建失败：本会话渲染层停用
    int m_readbackGen = 0; // 回读代次：看门狗复位后陈旧回调作废
    bool m_renderDirty = false;   // 脏驱动：有变化才整链渲染
    int m_histFrame = 0;  // 余晖历史帧计数：前 2 帧无历史（权重清零）
    float m_dtMs = 80.0f; // 帧时差：余晖按时间衰减（节奏无关）
    QSize m_texSize;
    QSize m_glowSize;
    qreal m_renderScale = 1.0; // 滚动期半分辨率（运动掩蔽），停稳回全
    QElapsedTimer m_ambientClock; // 环境动态（滚动带/颗粒/余晖）低帧率拍
    QElapsedTimer m_sinceRefresh;
    QElapsedTimer m_frameClock; // 帧时差测量（余晖时间衰减）
    QElapsedTimer m_clock;    // 运行秒数（噪声/刷新带的时间源）
    QElapsedTimer m_warmClock; // 入场暖机（showEvent 起拍）
    QElapsedTimer m_readbackClock; // 回读看门狗（超时强制复位）
    QTimer m_frameTimer;
};
