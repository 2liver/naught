// crt_view.cpp —— QRhiWidget 实现（Metal 原生合成路径）。
#include "crt_view.h"

#include "editor.h"

#include <QFile>
#include <QtGui/rhi/qrhi.h>
#include <QtGui/rhi/qshader.h>

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
    m_tex = r->newTexture(QRhiTexture::RGBA8, QSize(2, 2), 1,
                          QRhiTexture::UsedAsTransferSource | QRhiTexture::Flag());
    m_tex->create();
    // 常量缓冲：view + texSize（std140：两个 vec2）
    m_ubuf = r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
    m_ubuf->create();
    // 管线
    m_ps = r->newGraphicsPipeline();
    m_ps->setShaderStages({
        { QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/crt.vert.qsb")) },
        { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/crt.frag.qsb")) },
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
    m_ps->create();

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
