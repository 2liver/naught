// crt_view.cpp —— QRhiWidget 实现（Metal 原生合成路径）。
#include "crt_view.h"

#include "editor.h"

#include <QFile>
#include <QtGui/rhi/qrhi.h>
#include <QtGui/rhi/qshader.h>

static void shaderLog(const QString &s)
{
    QFile f(QStringLiteral("/tmp/naught-crt-shader.log"));
    f.open(QIODevice::Append);
    f.write(s.toUtf8() + "\n");
    f.close();
}

static QShader loadShader(const QString &name)
{
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly))
        return QShader();
    return QShader::fromSerialized(f.readAll());
}

CrtView::CrtView(Editor *editor)
    : QRhiWidget(editor)
    , m_editor(editor)
{
    // 原生子窗口：Metal 层由窗口服务器直接合成——纹理列表路径在
    // 本机不合成（黑屏/灰屏的根源）
    setAttribute(Qt::WA_NativeWindow);
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

void CrtView::initialize(QRhiCommandBuffer *)
{
    QRhi *r = rhi();
    // 输入纹理
    // 上传 = 传输**写入**：必须 UsedAsTransferDestination（此前误用 Source，
    // Metal 拒绝写入 → 纹理永远空 → 着色器采不到字）
    m_tex = r->newTexture(QRhiTexture::RGBA8, QSize(2, 2), 1, QRhiTexture::Flag());
    m_tex->create();
    // 常量缓冲：view + texSize（std140：两个 vec2）
    m_ubuf = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
    m_ubuf->create();
    // 管线
    m_ps = r->newGraphicsPipeline();
    const QShader vs = loadShader(QStringLiteral(":/shaders/crt.vert.qsb"));
    const QShader fs = loadShader(QStringLiteral(":/shaders/crt.frag.qsb"));
    shaderLog(QStringLiteral("shaders: vert valid=%1 frag valid=%2 backend=%3")
                  .arg(vs.isValid())
                  .arg(fs.isValid())
                  .arg(QString::fromLatin1(r->backendName())));
    m_ps->setShaderStages({
        { QRhiShaderStage::Vertex, vs },
        { QRhiShaderStage::Fragment, fs },
    });
    QRhiVertexInputLayout vin;
    vin.setBindings({});
    m_ps->setVertexInputLayout(vin);
    m_ps->setSampleCount(1);
    m_ps->setTopology(QRhiGraphicsPipeline::Triangles);
    m_ps->setShaderResourceBindings(m_srb = r->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage
                                                    | QRhiShaderResourceBinding::FragmentStage,
                                                 m_ubuf),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_tex, nullptr),
    });
    m_srb->create();
    m_ps->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!m_ps->create())
        shaderLog(QStringLiteral("PS CREATE FAIL"));
    else
        shaderLog(QStringLiteral("PS CREATE OK"));

    m_pending = QImage(2, 2, QImage::Format_ARGB32);
    m_pending.fill(qRgb(12, 9, 3));
    m_texDirty = true;
}

void CrtView::render(QRhiCommandBuffer *cb)
{
    QRhiResourceUpdateBatch *u = rhi()->nextResourceUpdateBatch();

    // 节流刷新（80ms）：文字快照自绘 + 上传
    if (m_texDirty && (!m_sinceRefresh.isValid() || m_sinceRefresh.elapsed() >= 80)) {
        m_pending = QImage(m_editor->viewport()->size(), QImage::Format_ARGB32);
        m_pending.fill(qRgb(12, 9, 3));
        m_editor->paintTextSnapshot(m_pending);
        const QImage up = m_pending.convertToFormat(QImage::Format_RGBA8888);
        if (m_tex->pixelSize() != up.size()) {
            m_tex->setPixelSize(up.size());
            m_tex->create();
        }
        u->uploadTexture(m_tex, up);
        m_texSize = up.size();
        m_texDirty = false;
        m_sinceRefresh.restart();
        // 取证：输入纹理原图落盘（每次刷新覆盖）
        up.save(QStringLiteral("/tmp/naught-crt-snap2.png"));
        {
            int amber = 0;
            for (int y = 0; y < up.height(); ++y)
                for (int x = 0; x < up.width(); ++x) {
                    const QRgb px = up.pixel(x, y);
                    if (qRed(px) > 150 && qGreen(px) > 80 && qBlue(px) < 90)
                        ++amber;
                }
            static int logged = 0;
            if (logged < 3) {
                shaderLog(QStringLiteral("snapshot %1x%2 amber=%3")
                              .arg(up.width()).arg(up.height()).arg(amber));
                ++logged;
            }
        }
    }

    // 观察者 = 鼠标
    QPointF view = m_editor->lastMouseViewport();
    if (view.x() < 0) {
        view = QPointF(-0.25, -0.12);
    } else {
        view = QPointF(
            (view.x() / qMax(1.0, qreal(m_editor->viewport()->width())) - 0.5) * 2.0,
            (view.y() / qMax(1.0, qreal(m_editor->viewport()->height())) - 0.5) * 2.0);
    }
    const float ub[4] = { float(view.x()), float(view.y()),
                          float(m_texSize.width()), float(m_texSize.height()) };
    u->updateDynamicBuffer(m_ubuf, 0, sizeof(ub), ub);

    cb->beginPass(renderTarget(), QColor::fromRgbF(0.012, 0.009, 0.004),
                  QRhiDepthStencilClearValue(), u);
    cb->setGraphicsPipeline(m_ps);
    cb->setShaderResources(m_srb);
    cb->setViewport(QRhiViewport(0, 0, colorTexture()->pixelSize().width(),
                                 colorTexture()->pixelSize().height()));
    cb->draw(3);
    cb->endPass();
}
