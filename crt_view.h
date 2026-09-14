// crt_view.h —— 「显」的真光学显示层（B 路线）：QOpenGLWidget + 片段着色器。
// 输入 = 文字快照纹理；逐像素实现：曲率（视点滑移）、磷粉三色栅、
// 扫描线、视点相关玻璃镜面反光、晕影。鼠标 = 观察者（宝可梦卡牌式倾斜）。
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QPointF>

class Editor;

class CrtView : public QOpenGLWidget {
public:
    explicit CrtView(Editor *editor);
    void markDirty();
    void syncGeometry();
    QImage frameImage() const { return m_pending; }

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    Editor *m_editor = nullptr;
    QOpenGLShaderProgram m_prog;
    QOpenGLTexture *m_tex = nullptr;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    QImage m_pending;
    bool m_texDirty = false;
    QElapsedTimer m_sinceRefresh;
    QSize m_texSize;
    QPointF m_view = QPointF(-0.25, -0.12);
};
