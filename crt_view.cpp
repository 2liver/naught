// crt_view.cpp —— 自有 QRhi（Metal）离屏渲染 + 回读的「显」显示层实现。
// GPU 多通道：P1 余晖合成（三纹理历史轮转，按时间衰减）→ P2 辉光
// （1/4 图 4×4 降采样 + 3 轮盒式模糊）→ P3 主着色（内容最近邻采样 +
// 辉光线性 max 叠加 + 栅网/扫描线/玻璃）→ 回读。CPU 只在脏帧重拍
// 快照并上传为纹理——空闲环境拍零 CPU 逐像素重活。
#include "crt_view.h"

#include "crt_source.h"

#include <QDir>
#include <QFile>
#include <QPainter>
#include <QResizeEvent>
#include <QSet>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

static void shaderLog(const QString &s)
{
    // 轮转：日志超 64KB 截断重来（防无限增长）
    QFile f(QDir::tempPath() + QStringLiteral("/naught-crt-shader.log"));
    if (f.size() > 65536)
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    else
        f.open(QIODevice::Append);
    if (f.isOpen()) {
        f.write(s.toUtf8() + "\n");
        f.close();
    }
}

static QShader loadShader(const QString &name)
{
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly))
        return QShader();
    return QShader::fromSerialized(f.readAll());
}

CrtView::CrtView(CrtSnapshotSource *source)
    : QWidget(source ? source->sourceWidget() : nullptr)
    , m_source(source)
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
    if (m_source)
        setGeometry(m_source->sourceRect());
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
    p.setRenderHint(QPainter::SmoothPixmapTransform); // 滚动期半分辨率回读 → 平滑放大
    p.fillRect(rect(), QColor(12, 9, 3));
    if (!m_shown.isNull()) {
        p.drawImage(rect(), m_shown);
    }
}

void CrtView::releaseGpu()
{
    // 资源归属 m_r：rhi 存活期内销毁即可，下一帧 ensureRhi 全量重建
    for (int i = 0; i < 3; ++i) {
        delete m_srbMain[i];
        m_srbMain[i] = nullptr;
        delete m_srbPersist[i];
        m_srbPersist[i] = nullptr;
        delete m_srbDown[i];
        m_srbDown[i] = nullptr;
        delete m_histRt[i];
        m_histRt[i] = nullptr;
        delete m_histTex[i];
        m_histTex[i] = nullptr;
    }
    delete m_srbBlurH;
    m_srbBlurH = nullptr;
    delete m_srbBlurV;
    m_srbBlurV = nullptr;
    delete m_glowRtA;
    m_glowRtA = nullptr;
    delete m_glowRtB;
    m_glowRtB = nullptr;
    delete m_glowA;
    m_glowA = nullptr;
    delete m_glowB;
    m_glowB = nullptr;
    delete m_psPersist;
    m_psPersist = nullptr;
    delete m_psDown;
    m_psDown = nullptr;
    delete m_psBlurH;
    m_psBlurH = nullptr;
    delete m_psBlurV;
    m_psBlurV = nullptr;
    delete m_snapTex;
    m_snapTex = nullptr;
    delete m_samplerNearest;
    m_samplerNearest = nullptr;
    delete m_samplerLinear;
    m_samplerLinear = nullptr;
    delete m_ps;
    m_ps = nullptr;
    delete m_rt;
    m_rt = nullptr;
    delete m_rp;
    m_rp = nullptr;
    delete m_colorTex;
    m_colorTex = nullptr;
    delete m_ubuf;
    m_ubuf = nullptr;
}

bool CrtView::pipelineUsable() const
{
    return m_ps != nullptr && m_r != nullptr && m_r->backend() != QRhi::Null;
}

QRhiGraphicsPipeline *CrtView::buildPipeline(const char *fragName,
                                             QRhiShaderResourceBindings *srb,
                                             QRhiRenderPassDescriptor *rp)
{
    QRhiGraphicsPipeline *ps = m_r->newGraphicsPipeline();
    const QShader vs = loadShader(QStringLiteral(":/shaders/crt.vert.qsb"));
    const QShader fs = loadShader(QString::fromLatin1(":/shaders/%1.qsb").arg(QLatin1String(fragName)));
    if (!vs.isValid() || !fs.isValid()) {
        shaderLog(QStringLiteral("SHADER LOAD FAIL: %1").arg(QLatin1String(fragName)));
        delete ps;
        return nullptr;
    }
    ps->setShaderStages({
        { QRhiShaderStage::Vertex, vs },
        { QRhiShaderStage::Fragment, fs },
    });
    QRhiVertexInputLayout vin;
    vin.setBindings({});
    ps->setVertexInputLayout(vin);
    ps->setSampleCount(1);
    ps->setTopology(QRhiGraphicsPipeline::Triangles);
    ps->setShaderResourceBindings(srb);
    ps->setRenderPassDescriptor(rp);
    if (!ps->create()) {
        shaderLog(QStringLiteral("PS CREATE FAIL: %1").arg(QLatin1String(fragName)));
        delete ps;
        return nullptr;
    }
    return ps;
}

QRhiTextureRenderTarget *CrtView::makeRt(QRhiTexture *tex)
{
    QRhiTextureRenderTarget *rt = m_r->newTextureRenderTarget({ tex });
    rt->setRenderPassDescriptor(rt->newCompatibleRenderPassDescriptor());
    return rt;
}

void CrtView::ensureRhi()
{
    if (m_rhiUnavailable)
        return; // 已判定无可用管线：不再每帧重建（toggleCrt 关闭重开也不复活）
    if (m_r && m_ps && m_psPersist && m_psDown && m_psBlurH && m_psBlurV && m_ubuf)
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
    // NAUGHT_RHI_SKIP=metal,vulkan：环境逃生阀（无 GPU CI 上软件
    // Vulkan/lavapipe 可能崩在 Qt 内部，探测期无法防御——跳过即可）
    const QSet<QString> skipSet = [] {
        QSet<QString> s;
        const QStringList parts = qEnvironmentVariable("NAUGHT_RHI_SKIP")
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &p : parts)
            s.insert(p.trimmed().toLower());
        return s;
    }();
    const struct { QRhi::Implementation impl; const char *name; } backends[] = {
        { QRhi::Metal, "metal" }, { QRhi::Vulkan, "vulkan" },
        { QRhi::D3D11, "d3d11" }, { QRhi::D3D12, "d3d12" },
        { QRhi::OpenGLES2, "gles2" }, { QRhi::Null, "null" },
    };
    for (const auto &b : backends) {
        if (skipSet.contains(QLatin1String(b.name)))
            continue;
        m_r = QRhi::create(b.impl, nullptr);
        if (m_r) {
            qWarning("CRT-RHI backend: %s", b.name);
            break;
        }
    }
    if (!m_r) {
        shaderLog(QStringLiteral("RHI CREATE FAIL"));
        return;
    }
    shaderLog(QStringLiteral("RHI backend: %1").arg(QString::fromLatin1(m_r->backendName())));

    // 颜色目标（离屏，无交换链）；物理像素级：纹理/缓冲/快照全部按
    // devicePixelRatio 放大，纹路在视网膜上保持真正的 1 物理像素
    const qreal dpr = devicePixelRatioF();
    m_texSize = QSize(qMax(1, int(width() * dpr * m_renderScale)),
                      qMax(1, int(height() * dpr * m_renderScale)));
    m_glowSize = QSize(qMax(1, m_texSize.width() / 4), qMax(1, m_texSize.height() / 4));

    m_colorTex = m_r->newTexture(QRhiTexture::RGBA8, m_texSize, 1,
                                 QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    if (!m_colorTex->create()) {
        shaderLog(QStringLiteral("TEX CREATE FAIL"));
        return;
    }
    m_rt = makeRt(m_colorTex);
    m_rp = m_rt->renderPassDescriptor();

    // 快照上传纹理（脏帧才更新；帧间常驻）
    m_snapTex = m_r->newTexture(QRhiTexture::RGBA8, m_texSize, 1,
                                QRhiTexture::UsedAsTransferSource);
    if (!m_snapTex->create()) {
        shaderLog(QStringLiteral("SNAP TEX CREATE FAIL"));
        return;
    }

    // 余晖历史：三纹理轮转（读二写一）
    for (int i = 0; i < 3; ++i) {
        m_histTex[i] = m_r->newTexture(QRhiTexture::RGBA8, m_texSize, 1,
                                       QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
        if (!m_histTex[i]->create()) {
            shaderLog(QStringLiteral("HIST TEX CREATE FAIL"));
            return;
        }
        m_histRt[i] = makeRt(m_histTex[i]);
    }

    // 辉光小图 ping-pong
    m_glowA = m_r->newTexture(QRhiTexture::RGBA8, m_glowSize, 1,
                              QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    m_glowB = m_r->newTexture(QRhiTexture::RGBA8, m_glowSize, 1,
                              QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
    if (!m_glowA->create() || !m_glowB->create()) {
        shaderLog(QStringLiteral("GLOW TEX CREATE FAIL"));
        return;
    }
    m_glowRtA = makeRt(m_glowA);
    m_glowRtB = makeRt(m_glowB);

    m_samplerNearest = m_r->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                       QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    m_samplerLinear = m_r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    if (!m_samplerNearest->create() || !m_samplerLinear->create()) {
        shaderLog(QStringLiteral("SAMPLER CREATE FAIL"));
        return;
    }

    // 常量缓冲：view + texSize + timeInfo + flags + 调色板 + 余晖 + 辉光
    // （std140：12×vec4 = 192 字节）
    m_ubuf = m_r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 192);
    m_ubuf->create();

    // SRB：主着色 ×3（内容 = 轮转历史）、余晖 ×3（快照 + 读二历史）、
    // 降采样 ×3（读轮转历史）、模糊 ×2（ping-pong）
    for (int i = 0; i < 3; ++i) {
        m_srbMain[i] = m_r->newShaderResourceBindings();
        m_srbMain[i]->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage,
                                                     m_ubuf),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      m_histTex[i], m_samplerNearest),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                      m_glowA, m_samplerLinear),
        });
        m_srbMain[i]->create();

        m_srbPersist[i] = m_r->newShaderResourceBindings();
        m_srbPersist[i]->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage
                                                        | QRhiShaderResourceBinding::FragmentStage,
                                                     m_ubuf),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      m_snapTex, m_samplerLinear),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                      m_histTex[(i + 2) % 3], m_samplerLinear),
            QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage,
                                                      m_histTex[(i + 1) % 3], m_samplerLinear),
        });
        m_srbPersist[i]->create();

        m_srbDown[i] = m_r->newShaderResourceBindings();
        m_srbDown[i]->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage
                                                        | QRhiShaderResourceBinding::FragmentStage,
                                                     m_ubuf),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      m_histTex[i], m_samplerLinear),
        });
        m_srbDown[i]->create();
    }
    m_srbBlurH = m_r->newShaderResourceBindings();
    m_srbBlurH->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage
                                                    | QRhiShaderResourceBinding::FragmentStage,
                                                 m_ubuf),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_glowA, m_samplerLinear),
    });
    m_srbBlurH->create();
    m_srbBlurV = m_r->newShaderResourceBindings();
    m_srbBlurV->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage
                                                    | QRhiShaderResourceBinding::FragmentStage,
                                                 m_ubuf),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_glowB, m_samplerLinear),
    });
    m_srbBlurV->create();

    // 管线（任一步失败 = 整体停用渲染层，优雅降级不崩）
    m_ps = buildPipeline("crt.frag", m_srbMain[0], m_rp);
    m_psPersist = buildPipeline("persist.frag", m_srbPersist[0],
                                m_histRt[0]->renderPassDescriptor());
    m_psDown = buildPipeline("downsample.frag", m_srbDown[0],
                             m_glowRtA->renderPassDescriptor());
    m_psBlurH = buildPipeline("blurH.frag", m_srbBlurH,
                              m_glowRtB->renderPassDescriptor());
    m_psBlurV = buildPipeline("blurV.frag", m_srbBlurV,
                              m_glowRtA->renderPassDescriptor());
    if (!m_ps || !m_psPersist || !m_psDown || !m_psBlurH || !m_psBlurV) {
        shaderLog(QStringLiteral("PIPELINE SET INCOMPLETE — render layer disabled"));
        qWarning("CRT-RHI pipeline create FAIL — render layer disabled");
        m_rhiUnavailable = true;
        return;
    }
    shaderLog(QStringLiteral("PS CREATE OK x5"));

    m_histFrame = 0;          // 重建后前两帧无历史（余晖权重清零）
    m_forceNow = true;
    m_pending = QImage(m_texSize, QImage::Format_ARGB32);
    m_pending.setDevicePixelRatio(dpr * m_renderScale);
    m_pending.fill(qRgb(12, 9, 3));
}

void CrtView::renderFrame()
{
    if (!isVisible())
        return;
    ensureRhi();
    if (!m_r || !m_ps || !m_psPersist || !m_psDown || !m_psBlurH || !m_psBlurV
        || !m_ubuf || !m_snapTex) {
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
    // P1 脏驱动：无变化且未到环境拍 → 整链跳过。环境拍 120ms 保底
    // 滚动带/颗粒/余晖的持续推进（余晖在 GPU 按时间衰减，零 CPU 重活）
    const bool dirty = m_renderDirty || m_forceNow;
    const bool ambient = !m_ambientClock.isValid() || m_ambientClock.elapsed() > 120;
    if (!dirty && !ambient)
        return;
    m_renderDirty = false;
    m_ambientClock.restart();
    const CrtConfig cfg = m_source->config(); // 每帧配置值快照
    // 滚动期半分辨率：运动掩蔽下 2×2 下采样不可感知，回读数据量 ÷4、
    // GPU 填充 ÷4；掩膜/扫描线锚定的屏幕栅格随目标减半（滚动中不可见），
    // 停稳后 settle 标全量、回全分辨率重拍自愈
    m_renderScale = cfg.scrolling ? 0.5 : 1.0;
    const QSize want(qMax(1, int(width() * devicePixelRatioF() * m_renderScale)),
                     qMax(1, int(height() * devicePixelRatioF() * m_renderScale)));
    if (want != m_texSize) {
        releaseGpu(); // 帧边界安全重建（此刻无在途回读）
        ensureRhi();
        if (!m_ps)
            return;
    }

    QRhiResourceUpdateBatch *u = m_r->nextResourceUpdateBatch();

    // 快照：合成真实组件（80ms 节流；force 立即）。环境拍（无脏）复用
    // 上一拍快照——文本不重拍，GPU 侧余晖照常推进
    const bool throttled = m_sinceRefresh.isValid() && m_sinceRefresh.elapsed() < 80;
    if (m_forceNow || !throttled) {
        const bool needRepaint = dirty || m_pending.isNull()
            || m_pending.size() != m_texSize;
        if (needRepaint) {
            // P3：增量快照——打字只重画脏区（复用上一帧为底）；无脏区信息
            // （首次）走全量兜底。滚动/缩放/换机已标全量
            const CrtSnapshotSource::SnapDirty snap = m_source->consumeSnapshotDirty();
            if (snap.full || m_pending.isNull() || m_pending.size() != m_texSize) {
                m_pending = QImage(m_texSize, QImage::Format_ARGB32);
                m_pending.setDevicePixelRatio(devicePixelRatioF() * m_renderScale);
                m_pending.fill(cfg.palette->bg); // 随调色板（M2）
                m_source->paintTextSnapshot(m_pending);
            } else if (!snap.rect.isEmpty()) {
                m_source->paintTextSnapshotRegion(m_pending, snap.rect);
            } else {
                m_pending = QImage(m_texSize, QImage::Format_ARGB32);
                m_pending.setDevicePixelRatio(devicePixelRatioF() * m_renderScale);
                m_pending.fill(cfg.palette->bg);
                m_source->paintTextSnapshot(m_pending);
            }
            QImage up = m_pending.convertToFormat(QImage::Format_RGBA8888);
            up.setDevicePixelRatio(1.0); // 上传按原始像素：QRhi 尊重图像 DPR

            u->uploadTexture(m_snapTex, up);
        }
        m_forceNow = false;
        m_sinceRefresh.restart();
    }
    // 观察者 = 鼠标（视差每帧更新，不受快照节流）；锁定（M1）= 复现鼠标
    // 离开窗口后的"完美视角"（观察者站在屏幕正前方，内容完整不被裁剪）
    QPointF view;
    if (cfg.viewLocked) {
        view = QPointF(-0.25, -0.12); // 与无鼠标默认分支同值
    } else {
        view = cfg.lastMouse;
        if (view.x() < 0) {
            view = QPointF(-0.25, -0.12);
        } else {
            view = QPointF(
                (view.x() / qMax(1.0, qreal(m_source->sourceViewportSize().width())) - 0.5) * 2.0,
                (view.y() / qMax(1.0, qreal(m_source->sourceViewportSize().height())) - 0.5) * 2.0);
        }
    }
    const Crt::Palette &pal = *cfg.palette;
    // 帧时差：余晖按时间衰减（与渲染节奏无关——CPU 版按 80ms 烘拍
    // 量化，GPU 版更接近连续物理）
    if (m_frameClock.isValid())
        m_dtMs = float(qBound(1, int(m_frameClock.restart()), 500));
    else
        m_frameClock.start();
    // 余晖权重（前两帧历史纹理未初始化 → 清零 = 无历史）
    const bool histPrimed = m_histFrame >= 2;
    const float k1[4] = { histPrimed ? float(pal.persist1[2]) : 0.0f,
                          histPrimed ? float(pal.persist1[1]) : 0.0f,
                          histPrimed ? float(pal.persist1[0]) : 0.0f, 1.0f };
    const float k2[4] = { histPrimed ? float(pal.persist2[2]) : 0.0f,
                          histPrimed ? float(pal.persist2[1]) : 0.0f,
                          histPrimed ? float(pal.persist2[0]) : 0.0f, 1.0f };
    // 滚动期跳辉光（运动掩蔽下不可感知；旧 CPU 版滚动亦跳过 bloom）
    const bool skipGlow = cfg.scrolling;
    // 视图移动期：余晖幽灵 4 倍速衰减——视差把整段文字位移时，
    // 旧位置的幽灵快速退场，不留下用户报的"倒影"双影
    const float ghostDt = cfg.viewMoving ? m_dtMs * 4.0f : m_dtMs;
    const float ub[48] = { float(view.x()), float(view.y()),
                           float(m_texSize.width()), float(m_texSize.height()),
                           float(m_clock.elapsed() / 1000.0),
                           m_warmClock.isValid() ? float(m_warmClock.elapsed()) : -1.0f,
                           cfg.screenEntity ? 1.0f : 0.0f,
                           float(cfg.machine), // flags: x=屏幕实体, y=机型
                           pal.scanTint.redF(), pal.scanTint.greenF(), pal.scanTint.blueF(), 1.0f,
                           pal.refl.redF(), pal.refl.greenF(), pal.refl.blueF(), 1.0f,
                           pal.dust.redF(), pal.dust.greenF(), pal.dust.blueF(), 1.0f,
                           k1[0], k1[1], k1[2], 1.0f,
                           k2[0], k2[1], k2[2], 1.0f,
                           skipGlow ? 0.0f : float(pal.glowAlpha),
                           1.0f / float(m_glowSize.width()), 1.0f / float(m_glowSize.height()),
                           ghostDt };
    u->updateDynamicBuffer(m_ubuf, 0, sizeof(ub), ub);

    const int cur = m_histFrame % 3;
    const QRhiViewport vpFull(0, 0, float(m_texSize.width()), float(m_texSize.height()));
    const QRhiViewport vpGlow(0, 0, float(m_glowSize.width()), float(m_glowSize.height()));

    QRhiCommandBuffer *cb = nullptr;
    if (m_r->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        return;
    // P1 余晖合成 → hist[cur]
    cb->beginPass(m_histRt[cur], QColor(0, 0, 0), QRhiDepthStencilClearValue(), u);
    cb->setGraphicsPipeline(m_psPersist);
    cb->setShaderResources(m_srbPersist[cur]);
    cb->setViewport(vpFull);
    cb->draw(3);
    cb->endPass();
    if (!skipGlow) {
        // P2a 辉光降采样 → glowA
        cb->beginPass(m_glowRtA, QColor(0, 0, 0), QRhiDepthStencilClearValue(), nullptr);
        cb->setGraphicsPipeline(m_psDown);
        cb->setShaderResources(m_srbDown[cur]);
        cb->setViewport(vpGlow);
        cb->draw(3);
        cb->endPass();
        // P2b 三轮盒式模糊（H: glowA→glowB；V: glowB→glowA）——与
        // Crt::phosphorBloom 的 gaussianBlur(2, 3) 同模型
        for (int i = 0; i < 3; ++i) {
            cb->beginPass(m_glowRtB, QColor(0, 0, 0), QRhiDepthStencilClearValue(), nullptr);
            cb->setGraphicsPipeline(m_psBlurH);
            cb->setShaderResources(m_srbBlurH);
            cb->setViewport(vpGlow);
            cb->draw(3);
            cb->endPass();
            cb->beginPass(m_glowRtA, QColor(0, 0, 0), QRhiDepthStencilClearValue(), nullptr);
            cb->setGraphicsPipeline(m_psBlurV);
            cb->setShaderResources(m_srbBlurV);
            cb->setViewport(vpGlow);
            cb->draw(3);
            cb->endPass();
        }
    }
    // P3 主着色 → m_colorTex
    cb->beginPass(m_rt, QColor::fromRgbF(0.012, 0.009, 0.004), QRhiDepthStencilClearValue(), nullptr);
    cb->setGraphicsPipeline(m_ps);
    cb->setShaderResources(m_srbMain[cur]);
    cb->setViewport(vpFull);
    cb->draw(3);
    cb->endPass();

    // 回读：GPU 帧 → CPU 图像（Metal 完成回调线程不碰 widget 状态，
    // 一律排队回主线程处理）
    QRhiReadbackResult *rb = new QRhiReadbackResult;
    m_readbackInFlight = true;
    m_readbackClock.start();
    const int gen = ++m_readbackGen;
    const QSize readSize = m_texSize; // 值捕获：回读期间 resize 不改目标尺寸
    rb->completed = [this, rb, gen, readSize] {
        QMetaObject::invokeMethod(this, [this, rb, gen, readSize] {
            if (gen != m_readbackGen) { // 看门狗已复位管线：陈旧回读作废
                delete rb;
                return;
            }
            QImage img(readSize, QImage::Format_RGBA8888);
            if (!img.isNull() && !rb->data.isEmpty())
                memcpy(img.bits(), rb->data.constData(),
                       qMin(size_t(img.sizeInBytes()), size_t(rb->data.size())));
            img = img.mirrored(false, true); // 回读行序自底向上：垂直镜像归位

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

    ++m_histFrame;
}
