// crt_view.cpp —— 自有 QRhi（Metal）离屏渲染 + 回读的「显」显示层实现。
#include "crt_view.h"

#include "editor.h"

#include <QFile>
#include <QPainter>
#include <QResizeEvent>
#include <QtGui/rhi/qrhi.h>
#include <QtGui/rhi/qshader.h>
#include <QtGui/private/qrhimetal_p.h>

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
    : QWidget(editor)
    , m_editor(editor)
{
    // alien 覆盖层：指针/滚轮/手势全部穿透到真实组件（非原生控件上
    // WA_TransparentForMouseEvents 完全有效——画布层已验证同款路径）。
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
    // 帧循环：视差/光标闪烁/足迹圆点 30fps；快照上传由 80ms 节流
    m_frameTimer.setInterval(33);
    connect(&m_frameTimer, &QTimer::timeout, this, &CrtView::renderFrame);
}

CrtView::~CrtView()
{
    m_frameTimer.stop();
    releaseGpu();
    delete m_r;
}

void CrtView::markDirty(bool force)
{
    if (force)
        m_forceNow = true; // 绕过 80ms 节流（缩放等必须立即重拍）
    update();
}

void CrtView::syncGeometry()
{
    // 整面覆盖：文字区+行号区+滚动条全部进入光栅（无任何"未覆盖"黑区）
    if (m_editor)
        setGeometry(m_editor->rect());
}

void CrtView::showEvent(QShowEvent *)
{
    ensureRhi();
    m_frameTimer.start();
}

void CrtView::hideEvent(QHideEvent *)
{
    m_frameTimer.stop();
}

void CrtView::resizeEvent(QResizeEvent *)
{
    // 纹理尺寸随窗口：重建推迟到帧边界（在途回读完成之后），
    // 避免释放正在被 Metal 命令缓冲引用的纹理
    markDirty(true);
}

void CrtView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(12, 9, 3));
    if (!m_shown.isNull()) {
        p.drawImage(rect(), m_shown);
    }
}

void CrtView::releaseGpu()
{
    // 资源归属 m_r：rhi 存活期内销毁即可，下一帧 ensureRhi 全量重建
    delete m_srb;
    m_srb = nullptr;
    delete m_ps;
    m_ps = nullptr;
    delete m_rt;
    m_rt = nullptr;
    delete m_rp;
    m_rp = nullptr;
    delete m_colorTex;
    m_colorTex = nullptr;
    delete m_pxbuf;
    m_pxbuf = nullptr;
    delete m_ubuf;
    m_ubuf = nullptr;
}

void CrtView::ensureRhi()
{
    if (m_r)
        return;
    QRhiMetalInitParams params;
    m_r = QRhi::create(QRhi::Metal, &params);
    if (!m_r) {
        shaderLog(QStringLiteral("RHI CREATE FAIL"));
        return;
    }

    // 颜色目标（离屏，无交换链）；物理像素级：纹理/缓冲/快照全部按
    // devicePixelRatio 放大，纹路在视网膜上保持真正的 1 物理像素
    const qreal dpr = devicePixelRatioF();
    m_texSize = QSize(qMax(1, int(width() * dpr)), qMax(1, int(height() * dpr)));
    m_colorTex = m_r->newTexture(QRhiTexture::RGBA8, m_texSize, 1,
                                 QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    if (!m_colorTex->create()) {
        shaderLog(QStringLiteral("TEX CREATE FAIL"));
        return;
    }
    m_rt = m_r->newTextureRenderTarget({ m_colorTex });
    m_rp = m_rt->newCompatibleRenderPassDescriptor();
    m_rt->setRenderPassDescriptor(m_rp);

    // 像素输入：存储缓冲（buffer 路径已在 Metal+RHI 上验证）
    const int pxbytes = m_texSize.width() * m_texSize.height() * 4;
    m_pxbuf = m_r->newBuffer(QRhiBuffer::Static, QRhiBuffer::StorageBuffer, pxbytes);
    m_pxbuf->create();
    // 常量缓冲：view + texSize（std140：两个 vec2）
    m_ubuf = m_r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16);
    m_ubuf->create();

    // 管线：全屏三角形
    m_ps = m_r->newGraphicsPipeline();
    const QShader vs = loadShader(QStringLiteral(":/shaders/crt.vert.qsb"));
    const QShader fs = loadShader(QStringLiteral(":/shaders/crt.frag.qsb"));
    shaderLog(QStringLiteral("shaders: vert valid=%1 frag valid=%2 backend=%3")
                  .arg(vs.isValid())
                  .arg(fs.isValid())
                  .arg(QString::fromLatin1(m_r->backendName())));
    m_ps->setShaderStages({
        { QRhiShaderStage::Vertex, vs },
        { QRhiShaderStage::Fragment, fs },
    });
    QRhiVertexInputLayout vin;
    vin.setBindings({});
    m_ps->setVertexInputLayout(vin);
    m_ps->setSampleCount(1);
    m_ps->setTopology(QRhiGraphicsPipeline::Triangles);
    m_ps->setShaderResourceBindings(m_srb = m_r->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage
                                                    | QRhiShaderResourceBinding::FragmentStage,
                                                 m_ubuf),
        QRhiShaderResourceBinding::bufferLoadStore(1, QRhiShaderResourceBinding::FragmentStage,
                                                   m_pxbuf),
    });
    m_srb->create();
    m_ps->setRenderPassDescriptor(m_rp);
    if (!m_ps->create())
        shaderLog(QStringLiteral("PS CREATE FAIL"));
    else
        shaderLog(QStringLiteral("PS CREATE OK"));

    m_texSize = QSize(qMax(1, int(width() * dpr)), qMax(1, int(height() * dpr)));
    m_forceNow = true;
    m_pending = QImage(m_texSize, QImage::Format_ARGB32);
    m_pending.setDevicePixelRatio(dpr);
    m_pending.fill(qRgb(12, 9, 3));
}

void CrtView::renderFrame()
{
    if (!isVisible())
        return;
    ensureRhi();
    if (!m_r || !m_ps || !m_srb || !m_pxbuf || !m_ubuf || m_readbackInFlight)
        return;
    const QSize want(qMax(1, int(width() * devicePixelRatioF())),
                     qMax(1, int(height() * devicePixelRatioF())));
    if (want != m_texSize) {
        releaseGpu(); // 帧边界安全重建（此刻无在途回读）
        ensureRhi();
        if (!m_ps)
            return;
    }

    QRhiResourceUpdateBatch *u = m_r->nextResourceUpdateBatch();

    // 快照：合成真实组件（80ms 节流；force 立即）。帧循环每拍都标脏，
    // 由节流决定真实上传节奏——光标闪烁/足迹圆点/滚动条淡出都在其中
    const bool throttled = m_sinceRefresh.isValid() && m_sinceRefresh.elapsed() < 80;
    if (m_forceNow || !throttled) {
        m_pending = QImage(m_texSize, QImage::Format_ARGB32);
        m_pending.setDevicePixelRatio(devicePixelRatioF());
        m_pending.fill(qRgb(12, 9, 3));
        m_editor->paintTextSnapshot(m_pending);
        Crt::phosphorPersistence(m_pending, m_prev); // 一期：磷粉余晖（滚动残影）
        m_prev = m_pending; // 浅拷贝：下帧余晖源 = 本帧无辉光内容（写入时分离）
        Crt::phosphorBloom(m_pending); // 二期三件套：真高斯辉光（快照重建时烘焙）
        {
            const QImage up = m_pending.convertToFormat(QImage::Format_RGBA8888);
            const int nbytes = up.sizeInBytes();
            if (nbytes > 0 && nbytes <= m_pxbuf->size()) {
                u->uploadStaticBuffer(m_pxbuf, 0, size_t(nbytes), up.constBits());
            } else if (nbytes > m_pxbuf->size()) {
                // 窗口变大（罕见）：重建存储缓冲与 SRB
                m_pxbuf->destroy();
                m_pxbuf->setSize(nbytes);
                m_pxbuf->create();
                m_srb->destroy();
                m_srb->create();
                u->uploadStaticBuffer(m_pxbuf, 0, size_t(nbytes), up.constBits());
            }
            m_texSize = up.size();
        }
        m_forceNow = false;
        m_sinceRefresh.restart();
    }
    // 观察者 = 鼠标（视差每帧更新，不受快照节流）
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

    QRhiCommandBuffer *cb = nullptr;
    if (m_r->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        return;
    cb->beginPass(m_rt, QColor::fromRgbF(0.012, 0.009, 0.004),
                  QRhiDepthStencilClearValue(), u);
    cb->setGraphicsPipeline(m_ps);
    cb->setShaderResources(m_srb);
    cb->setViewport(QRhiViewport(0, 0, float(m_texSize.width()), float(m_texSize.height())));
    cb->draw(3);
    cb->endPass();

    // 回读：GPU 帧 → CPU 图像（Metal 完成回调线程不碰 widget 状态，
    // 一律排队回主线程处理）
    QRhiReadbackResult *rb = new QRhiReadbackResult;
    m_readbackInFlight = true;
    rb->completed = [this, rb] {
        QMetaObject::invokeMethod(this, [this, rb] {
            QImage img(m_texSize, QImage::Format_RGBA8888);
            if (!img.isNull() && !rb->data.isEmpty())
                memcpy(img.bits(), rb->data.constData(),
                       qMin(size_t(img.sizeInBytes()), size_t(rb->data.size())));
            img.setDevicePixelRatio(devicePixelRatioF()); // 物理像素：1:1 落屏
            m_shown = std::move(img);
            m_readbackInFlight = false;
            delete rb;
            update();
        }, Qt::QueuedConnection);
    };
    QRhiResourceUpdateBatch *ru = m_r->nextResourceUpdateBatch();
    ru->readBackTexture(m_colorTex, rb);
    cb->resourceUpdate(ru);
    m_r->endOffscreenFrame();
}
