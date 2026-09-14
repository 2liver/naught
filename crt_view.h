// crt_view.h —— 「显」真光学显示层（B 路线·Metal 版）：QRhiWidget + 着色器。
// mac 上走 Metal 原生合成——QOpenGLWidget 的窗口层不合成（黑屏）坑从根上消失。
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointF>
#include <QRhiWidget>

class QRhiCommandBuffer;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiReadbackResult;

class Editor;

class CrtView : public QRhiWidget {
public:
    explicit CrtView(Editor *editor);
    void markDirty(bool force = false);
    void syncGeometry();
    QImage frameImage() const { return m_pending; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    Editor *m_editor = nullptr;
    QRhiTexture *m_tex = nullptr;
    QRhiBuffer *m_pxbuf = nullptr;
    QRhiBuffer *m_ubuf = nullptr;
    QRhiShaderResourceBindings *m_srb = nullptr;
    QRhiGraphicsPipeline *m_ps = nullptr;
    QImage m_pending;
    bool m_texDirty = true;
    bool m_forceNow = false;
    QElapsedTimer m_sinceRefresh;
    QSize m_texSize;
};
