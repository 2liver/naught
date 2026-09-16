// crt_view.cpp —— 自有 QRhi（Metal）离屏渲染 + 回读的「显」显示层实现。
#include "crt_view.h"

#include "editor.h"

#include <QFile>
#include <QPainter>
#include <QResizeEvent>
#include <QtGui/rhi/qrhi.h>
#include <QtGui/rhi/qshader.h>

static void shaderLog(const QString &s)
{
    // 轮转：日志超 64KB 截断重来（防无限增长）
    QFile f(QStringLiteral("/tmp/naught-crt-shader.log"));
    if (f.size() > 65536)
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    else
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
    m_clock.start(); // 运行秒数：噪声/滚动刷新带的时间源
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
    m_renderDirty = true; // 脏驱动：整链（快照+GPU+回读）只在有变化时跑
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
    m_warmClock.start(); // 入场暖机：由暗到亮的一次预热脉冲
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
    if (m_r && m_ps && m_srb && m_pxbuf && m_ubuf)
        return; // 资源完整
    // 半残状态（releaseGpu 释放了子资源但保留 rhi——resize/全屏重建路径
    // 的坑）：整体重建。否则 m_r 存活导致提前返回，管线永远半残，
    // 帧循环从此冻结（全屏后画面冻死的根因）。
    delete m_r;
    m_r = nullptr;
    // 后端探测回退：Metal → Vulkan → OpenGL → Null（跨平台——
    // 旧版硬编码 Metal 且依赖私有头 qrhimetal_p.h，Windows/Linux
    // 直接编译不过）。默认参数即可（Metal 无参 = 系统默认设备）
    // Qt 6.9 已移除桌面 OpenGL 后端；平台不适配的项 create 会快速失败
    const QRhi::Implementation backends[] = {
        QRhi::Metal, QRhi::Vulkan, QRhi::D3D11, QRhi::D3D12,
        QRhi::OpenGLES2, QRhi::Null,
    };
    for (QRhi::Implementation impl : backends) {
        m_r = QRhi::create(impl, nullptr);
        if (m_r)
            break;
    }
    if (!m_r) {
        shaderLog(QStringLiteral("RHI CREATE FAIL"));
        return;
    }
    shaderLog(QStringLiteral("RHI backend: %1").arg(QString::fromLatin1(m_r->backendName())));

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
    // 常量缓冲：view + texSize + timeInfo + 调色板染色（std140：2×vec2 +
    // pad + 3×vec4 = 80 字节）
    m_ubuf = m_r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80);
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
    if (!m_r || !m_ps || !m_srb || !m_pxbuf || !m_ubuf) {
        return;
    }
    // 回读看门狗：全屏过渡等场景下 Metal 回读可能失联——超时强制复位
    if (m_readbackInFlight && m_readbackClock.isValid()
        && m_readbackClock.elapsed() > 800) {
        m_readbackInFlight = false;
        ++m_readbackGen; // 在途回读作废：迟到回调不覆盖新帧
        releaseGpu();
        ensureRhi();
        m_forceNow = true;
        m_sinceRefresh.invalidate();
    }
    if (m_readbackInFlight)
        return;
    // P1 脏驱动：无变化且未到环境拍 → 整链跳过（旧版无脏也每 80ms
    // 全屏快照+GPU+回读，闲置 CPU 与电池被持续吃掉）。环境拍 120ms
    // 保底滚动带/颗粒/余晖的持续推进
    const bool dirty = m_renderDirty || m_forceNow;
    const bool ambient = !m_ambientClock.isValid() || m_ambientClock.elapsed() > 120;
    if (!dirty && !ambient)
        return;
    m_renderDirty = false;
    m_ambientClock.restart();
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
        // P3：增量快照——打字只重画脏区（复用上一帧为底）；无脏区信息
        // （环境拍/首次）走全量兜底。滚动/缩放/换机已标全量
        const bool fullDirty = m_editor->snapshotFullDirty();
        const QRect dirty = m_editor->consumeSnapshotDirty();
        if (fullDirty || m_pending.isNull() || m_pending.size() != m_texSize) {
            m_pending = QImage(m_texSize, QImage::Format_ARGB32);
            m_pending.setDevicePixelRatio(devicePixelRatioF());
            m_pending.fill(m_editor->crtPalette().bg); // 随调色板（M2）
            m_editor->paintTextSnapshot(m_pending);
        } else if (!dirty.isEmpty()) {
            m_editor->paintTextSnapshotRegion(m_pending, dirty);
        } else {
            m_pending = QImage(m_texSize, QImage::Format_ARGB32);
            m_pending.setDevicePixelRatio(devicePixelRatioF());
            m_pending.fill(m_editor->crtPalette().bg);
            m_editor->paintTextSnapshot(m_pending);
        }
        // 滚动期间跳过余晖+辉光重活（每 80ms 一帧的全屏逐像素 + 模糊
        // 是滚动卡顿大户）；停稳后 settle 标记全量重拍，痕迹自愈
        if (!m_editor->isScrolling()) {
            Crt::phosphorPersistence(m_pending, m_prev, m_prev2); // 一期+M4：双指数余晖
            m_prev2 = m_prev; // 上上帧（浅拷贝链：写入时分离）
            m_prev = m_pending; // 上一帧（浅拷贝）
            Crt::phosphorBloom(m_pending, m_editor->crtPalette().glowAlpha); // 二期三件套：真高斯辉光（随调色板）
        }
        // 入场暖机：因子由 shader 按 timeInfo.y 计算（CPU 逐像素循环
        // 曾引发帧循环冻结，已整体移入 GPU）
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
    // 观察者 = 鼠标（视差每帧更新，不受快照节流）；锁定（M1）= 复现鼠标
    // 离开窗口后的"完美视角"（观察者站在屏幕正前方，内容完整不被裁剪）
    QPointF view;
    if (m_editor->crtViewLocked()) {
        view = QPointF(-0.25, -0.12); // 与无鼠标默认分支同值
    } else {
        view = m_editor->lastMouseViewport();
        if (view.x() < 0) {
            view = QPointF(-0.25, -0.12);
        } else {
            view = QPointF(
                (view.x() / qMax(1.0, qreal(m_editor->viewport()->width())) - 0.5) * 2.0,
                (view.y() / qMax(1.0, qreal(m_editor->viewport()->height())) - 0.5) * 2.0);
        }
    }
    const Crt::Palette &pal = m_editor->crtPalette();
    const float ub[20] = { float(view.x()), float(view.y()),
                           float(m_texSize.width()), float(m_texSize.height()),
                           float(m_clock.elapsed() / 1000.0),
                           m_warmClock.isValid() ? float(m_warmClock.elapsed()) : -1.0f,
                           m_editor->screenEntityOn() ? 1.0f : 0.0f,
                           float(m_editor->machine()), // flags: x=屏幕实体, y=机型
                           pal.scanTint.redF(), pal.scanTint.greenF(), pal.scanTint.blueF(), 1.0f,
                           pal.refl.redF(), pal.refl.greenF(), pal.refl.blueF(), 1.0f,
                           pal.dust.redF(), pal.dust.greenF(), pal.dust.blueF(), 1.0f };
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
    m_readbackClock.start();
    const int gen = ++m_readbackGen;
    rb->completed = [this, rb, gen] {
        QMetaObject::invokeMethod(this, [this, rb, gen] {
            if (gen != m_readbackGen) { // 看门狗已复位管线：陈旧回读作废
                delete rb;
                return;
            }
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
