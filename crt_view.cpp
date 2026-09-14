// crt_view.cpp —— 「显」真光学着色器（B 路线）。
#include "crt_view.h"

#include "editor.h"

#include <QElapsedTimer>
#include <QOpenGLExtraFunctions>

// 片段着色器（GLSL 330 core，macOS 4.1 兼容）。
// 模型（CRT-Royale 式解析近似）：
//   1. 曲率 + 视点滑移：画面随观察者位置滑动（宝可梦卡牌倾斜感）
//   2. 磷粉三色栅：3× 水平子像素采样，琥珀光谱权重（R 全亮 + G 0.69 + B 0.06）
//   3. 扫描线
//   4. 视点相关玻璃镜面反光
//   5. 晕影
static const char *kFrag = R"GLSL(
#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform sampler2D tex;
uniform vec2 view;      // 观察者 -1..1
uniform vec2 texSize;

vec2 curve(vec2 uv) {
    vec2 c = uv - 0.5;
    c += view * 0.055;                 // 视差滑移（卡片倾斜）
    float r2 = dot(c, c);
    return c * (1.0 + 0.14 * r2) + 0.5; // 桶形曲率
}

void main() {
    vec2 uv = curve(v_uv);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        frag = vec4(0.012, 0.009, 0.004, 1.0); // 玻璃外的暗角底
        return;
    }
    vec2 px = 1.0 / texSize;

    // 磷粉三色栅：子像素级采样（真彩的物质感）
    float sub = fract(uv.x * texSize.x * 3.0);
    float rMask = smoothstep(0.0, 0.333, sub) * (1.0 - smoothstep(0.333, 0.667, sub));
    float gMask = smoothstep(0.333, 0.667, sub) * (1.0 - smoothstep(0.667, 1.0, sub));
    float bMask = smoothstep(0.667, 1.0, sub);

    vec3 texcol = texture(tex, uv).rgb;
    vec3 phos = vec3(texcol.r, texcol.g * 0.69, texcol.b * 0.06); // 琥珀光谱
    vec3 col = vec3(phos.r * rMask + phos.g * gMask + phos.b * bMask) * 3.0;

    // 扫描线
    float row = fract(uv.y * texSize.y);
    col *= 1.0 - 0.22 * step(0.5, fract(row * 0.5));

    // 视点相关镜面反光（玻璃表面）
    vec2 n = normalize(vec2(view.x * 0.8, 0.6));
    float refl = pow(max(0.0, 1.0 - abs(dot(n, vec2(0.35, 0.94)) - 0.62) * 3.2), 2.0);
    col += vec3(1.0, 0.88, 0.62) * refl * 0.055;

    // 晕影
    float d = length(uv - 0.5) * 1.7;
    col *= 1.0 - 0.45 * smoothstep(0.4, 1.0, d);

    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)GLSL";

static const char *kVert = R"GLSL(
#version 330 core
in vec2 aPos;
in vec2 aUv;
out vec2 v_uv;
void main() {
    v_uv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

CrtView::CrtView(Editor *editor)
    : QOpenGLWidget(editor)
    , m_editor(editor)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setMouseTracking(true);
}

void CrtView::markDirty()
{
    m_texDirty = true;
    update();
}

void CrtView::syncGeometry()
{
    if (m_editor && m_editor->viewport())
        setGeometry(m_editor->viewport()->geometry());
}

void CrtView::initializeGL()
{
    QOpenGLExtraFunctions *f = context()->extraFunctions();
    m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, kVert);
    m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, kFrag);
    m_prog.link();

    const float verts[] = {
        // pos      uv
        -1.f, -1.f, 0.f, 0.f,
         1.f, -1.f, 1.f, 0.f,
        -1.f,  1.f, 0.f, 1.f,
         1.f,  1.f, 1.f, 1.f,
    };
    f->glGenVertexArrays(1, &m_vao);
    f->glGenBuffers(1, &m_vbo);
    f->glBindVertexArray(m_vao);
    f->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    f->glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    m_prog.enableAttributeArray("aPos");
    m_prog.setAttributeBuffer("aPos", GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_prog.enableAttributeArray("aUv");
    m_prog.setAttributeBuffer("aUv", GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    f->glBindVertexArray(0);

    m_tex = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_tex->setMinificationFilter(QOpenGLTexture::Linear);
    m_tex->setMagnificationFilter(QOpenGLTexture::Linear);
    m_tex->setWrapMode(QOpenGLTexture::ClampToEdge);

    m_pending = QImage(2, 2, QImage::Format_ARGB32);
    m_pending.fill(qRgb(12, 9, 3));
    m_texDirty = true;
}

void CrtView::paintGL()
{
    QOpenGLExtraFunctions *f = context()->extraFunctions();
    f->glViewport(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio());
    f->glClearColor(0.012f, 0.009f, 0.004f, 1.f);
    f->glClear(GL_COLOR_BUFFER_BIT);

    // 节流刷新：80ms 一拍（打字/滚动期间）
    if (m_texDirty
        && (!m_sinceRefresh.isValid()
            || m_sinceRefresh.elapsed() >= 80)) {
        m_pending = QImage(m_editor->viewport()->size(), QImage::Format_ARGB32);
        m_pending.fill(qRgb(12, 9, 3));
        m_editor->paintTextSnapshot(m_pending);
        m_tex->destroy();
        m_tex->create();
        m_tex->setData(m_pending, QOpenGLTexture::DontGenerateMipMaps);
        m_texSize = m_pending.size();
        m_texDirty = false;
        m_sinceRefresh.restart();
    }

    // 观察者 = 鼠标（编辑器追踪）；无鼠标 = 略偏左上
    m_view = m_editor->lastMouseViewport();
    if (m_view.x() < 0) {
        m_view = QPointF(-0.25, -0.12);
    } else {
        m_view = QPointF(
            (m_view.x() / qMax(1.0, qreal(m_editor->viewport()->width())) - 0.5) * 2.0,
            (m_view.y() / qMax(1.0, qreal(m_editor->viewport()->height())) - 0.5) * 2.0);
    }

    m_prog.bind();
    m_tex->bind(0);
    m_prog.setUniformValue("tex", 0);
    m_prog.setUniformValue("view", QVector2D(float(m_view.x()), float(m_view.y())));
    m_prog.setUniformValue("texSize", QVector2D(float(m_texSize.width()), float(m_texSize.height())));
    f->glBindVertexArray(m_vao);
    f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    f->glBindVertexArray(0);
    m_prog.release();
}
