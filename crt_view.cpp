// crt_view.cpp —— 自有 QRhi（Metal）离屏渲染 + 回读的「显」显示层实现。
// GPU 多通道：P1 余晖合成（三纹理历史轮转，按时间衰减）→ P2 辉光
// （1/4 图 4×4 降采样 + 3 轮盒式模糊）→ P3 主着色（内容最近邻采样 +
// 辉光线性 max 叠加 + 栅网/扫描线/玻璃）→ 回读。CPU 只在脏帧重拍
// 快照并上传为纹理——空闲环境拍零 CPU 逐像素重活。
#include "crt_view.h"

#include "crt_source.h"

#include <QAbstractScrollArea>
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
    m_diagTimer.setInterval(2000);
    connect(&m_diagTimer, &QTimer::timeout, this, &CrtView::diagHeartbeat);
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
    m_diagTimer.start();
}

void CrtView::hideEvent(QHideEvent *)
{
    m_frameTimer.stop();
    m_diagTimer.stop();
}

void CrtView::resizeEvent(QResizeEvent *)
{
    // 纹理尺寸随窗口：重建推迟到帧边界（在途回读完成之后），
    // 避免释放正在被 Metal 命令缓冲引用的纹理
    markDirty(true);
}

void CrtView::diagHeartbeat()
{
    // 冻结诊断（用户报：首行打删打删后画面冻结，离屏无法复现——真机
    // 落盘取证）：回读 4s 不落地 = 冻结段。落盘：管线状态 + 源控件
    // 视口裸渲染亮度（判"视口渲染为空"根因）+ 快照顶部亮度，
    // 并顺手做自愈锤（视口 update + 快照作废 + 管线重置）
    if (!m_lastLanded.isValid() || m_lastLanded.elapsed() < 4000)
        return;
    if (m_diagDumped)
        return;
    m_diagDumped = true;
    long vpLum = -1, pendLum = -1;
    QString vpSize = QStringLiteral("?");
    if (m_source && m_source->sourceWidget()) {
        QWidget *w = m_source->sourceWidget();
        if (auto *sa = qobject_cast<QAbstractScrollArea *>(w)) {
            if (QWidget *vp = sa->viewport()) {
                vpSize = QStringLiteral("%1x%2").arg(vp->width()).arg(vp->height());
                QImage probe(vp->size(), QImage::Format_ARGB32);
                if (!probe.isNull()) {
                    probe.fill(Qt::black);
                    QPainter pp(&probe);
                    vp->render(&pp);
                    pp.end();
                    long mx = 0;
                    for (int y = 0; y < probe.height(); ++y)
                        for (int x = 0; x < probe.width(); ++x) {
                            const QRgb px = probe.pixel(x, y);
                            mx = qMax<long>(mx, qRed(px) + qGreen(px) + qBlue(px));
                        }
                    vpLum = mx;
                }
            }
        }
    }
    if (!m_pending.isNull()) {
        long mx = 0;
        for (int y = 0; y < m_pending.height() / 5; ++y)
            for (int x = 2; x < m_pending.width() - 30; ++x) {
                const QRgb px = m_pending.pixel(x, y);
                mx = qMax<long>(mx, qRed(px) + qGreen(px) + qBlue(px));
            }
        pendLum = mx;
    }
    const QString line = QStringLiteral(
        "FREEZE-DIAG stale=%1ms inFlight=%2 tex=%3x%4 pendingNull=%5 shownNull=%6 "
        "unavail=%7 visible=%8 frameTimer=%9 vp=%10 vpLum=%11 pendingTopLum=%12 "
        "wdFires=%13 wdStreak=%14\n")
        .arg(qint64(m_lastLanded.elapsed()))
        .arg(int(m_readbackInFlight))
        .arg(m_texSize.width()).arg(m_texSize.height())
        .arg(int(m_pending.isNull())).arg(int(m_shown.isNull()))
        .arg(int(m_rhiUnavailable)).arg(int(isVisible()))
        .arg(int(m_frameTimer.isActive()))
        .arg(vpSize).arg(vpLum).arg(pendLum)
        .arg(m_watchdogFires).arg(m_watchdogStreak);
    // 固定 /tmp：macOS 下 QDir::tempPath = /var/folders/.../T（用户找不到）
    QFile f(QStringLiteral("/tmp/naught-freeze-diag.log"));
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(line.toUtf8());
        f.close();
    }
    qWarning("%s", qPrintable(line));
    // 自愈锤：视口重绘（可能打破"视口渲染为空"）+ 快照作废全量重拍
    // + 管线重置。若视口渲染真为空（vpLum 低），锤子也救不活根因——
    // 但日志会把根因照出来
    if (m_source && m_source->sourceWidget()) {
        QWidget *w = m_source->sourceWidget();
        if (auto *sa = qobject_cast<QAbstractScrollArea *>(w))
            if (sa->viewport())
                sa->viewport()->update();
        w->update();
    }
    m_pending = QImage();
    resetPipeline();
    markDirty(true);
}

void CrtView::paintEvent(QPaintEvent *)
{
    // 画面活性看门狗（用户报：打字+删除后画面冻结、痕迹删不掉）：
    // 回读长期不落地 = 帧循环卡死 → 强制重置管线 + 快照作废全量重拍。
    // 本函数由光标眨眼（750ms）驱动 = 天然心跳，定时器停了也能自愈
    if (m_lastLanded.isValid() && m_lastLanded.elapsed() > 6000) {
        qWarning("CRT liveness watchdog fired (no readback for %lld ms) — "
                 "force pipeline reset", qint64(m_lastLanded.elapsed()));
        m_pending = QImage(); // 快照作废 → 下一帧全量重拍
        resetPipeline();
        markDirty(true);
    }
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform); // 滚动期半分辨率回读 → 平滑放大
    p.fillRect(rect(), QColor(12, 9, 3));
    if (!m_shown.isNull()) {
        p.drawImage(rect(), m_shown);
    }
    // 光标顶层叠加（用户报光标伪影：旧光标随余晖残留在旧位置——光标
    // 不再进快照/余晖，画在 GPU 帧之上，永不产生残影）。
    // 分机型（用户六轮考据 + retrocomputing 考证）：
    //  C64（机型 2）：真机无硬件光标，KERNAL 屏幕编辑器把光标位字符
    //   在正常/反相间翻动（无硬光标机器的通行假光标做法）。C64 的
    //   "反相" = 前景/背景色互换：字格填字符色（浅蓝），字形以底色
    //   （深蓝）呈现——不是白负片；空格位 = 纯字符色实心块。
    //  其它块光标机（0/1）：Difference 白反相（炽磷亮块，字形负片）。
    //  机型 3（IBM PC 5150）：6845 硬光标默认 = 底缘 2-3 扫描线
    //  下划线（真机 DOS 默认；块状只在插入模式）。
    if (m_source && m_source->cursorVisible()) {
        QRect cell = m_source->cursorCellRect();
        if (!cell.isEmpty()) {
            if (m_source->cursorUnderline()) {
                cell = QRect(cell.x(), cell.bottom() - qMax(2, cell.height() * 18 / 100),
                             cell.width(), qMax(2, cell.height() * 18 / 100));
                p.setCompositionMode(QPainter::CompositionMode_Difference);
                p.fillRect(cell, Qt::white);
            } else if (m_source->config().machine == 2) {
                // C64 真机反相 = 色对调（亮字色填充字格、字形呈底色）
                if (!m_shown.isNull()) {
                    const qreal sx = qreal(m_shown.width()) / qMax(1, width());
                    const qreal sy = qreal(m_shown.height()) / qMax(1, height());
                    const QRect src = QRect(qFloor(cell.x() * sx), qFloor(cell.y() * sy),
                                            qCeil(cell.width() * sx), qCeil(cell.height() * sy))
                                          .intersected(m_shown.rect());
                    if (!src.isEmpty()) {
                        QImage sub = m_shown.copy(src);
                        const Crt::Palette &pp = *m_source->config().palette;
                        const int bgSum = pp.bg.red() + pp.bg.green() + pp.bg.blue();
                        const int inkSum = pp.ink.red() + pp.ink.green() + pp.ink.blue();
                        const int span = qMax(1, inkSum - bgSum);
                        for (int y = 0; y < sub.height(); ++y) {
                            uchar *line = sub.scanLine(y);
                            for (int x = 0; x < sub.width(); ++x) {
                                const int i = x * 4; // RGBA8888：R,G,B,A
                                const int sum = line[i] + line[i + 1] + line[i + 2];
                                // t=0（底色）→ 字符色；t=1（字形）→ 底色
                                const qreal t = qBound(0.0, qreal(sum - bgSum) / qreal(span), 1.0);
                                line[i] = uchar(pp.ink.red() + (pp.bg.red() - pp.ink.red()) * t);
                                line[i + 1] = uchar(pp.ink.green() + (pp.bg.green() - pp.ink.green()) * t);
                                line[i + 2] = uchar(pp.ink.blue() + (pp.bg.blue() - pp.ink.blue()) * t);
                            }
                        }
                        p.drawImage(cell, sub);
                    }
                }
            } else {
                p.setCompositionMode(QPainter::CompositionMode_Difference);
                p.fillRect(cell, Qt::white);
            }
        }
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
    if (m_rhiUnavailable) {
        if (!m_rhiDeadAt.isValid() || m_rhiDeadAt.elapsed() < 3000)
            return; // 限时死亡：3s 内不重建（避免每帧探测）
        m_rhiUnavailable = false; // 周期复活重试（瞬时失败 ≠ 永远失败）
        qWarning("CRT-RHI retry after pipeline failure");
    }
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
        releaseGpu();
        delete m_r;
        m_r = nullptr;
        return;
    }
    m_rt = makeRt(m_colorTex);
    m_rp = m_rt->renderPassDescriptor();

    // 快照上传纹理（脏帧才更新；帧间常驻）
    m_snapTex = m_r->newTexture(QRhiTexture::RGBA8, m_texSize, 1,
                                QRhiTexture::UsedAsTransferSource);
    if (!m_snapTex->create()) {
        shaderLog(QStringLiteral("SNAP TEX CREATE FAIL"));
        releaseGpu();
        delete m_r;
        m_r = nullptr;
        return;
    }

    // 余晖历史：三纹理轮转（读二写一）
    for (int i = 0; i < 3; ++i) {
        m_histTex[i] = m_r->newTexture(QRhiTexture::RGBA8, m_texSize, 1,
                                       QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);
        if (!m_histTex[i]->create()) {
            shaderLog(QStringLiteral("HIST TEX CREATE FAIL"));
            releaseGpu();
            delete m_r;
            m_r = nullptr;
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
        releaseGpu();
        delete m_r;
        m_r = nullptr;
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
        releaseGpu();
        delete m_r;
        m_r = nullptr;
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
        m_rhiDeadAt.start();
        releaseGpu();
        delete m_r;
        m_r = nullptr;
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
    // 画面活性看门狗（帧定时器 33ms 恒跳 = 真心跳；paintEvent 心跳依赖
    // 眨眼，眨眼 1.5s 后休眠 → 冻结时无心跳救不了）：回读长期不落地
    // = 帧循环卡死 → 强制重置管线 + 快照作废全量重拍
    if (m_lastLanded.isValid() && m_lastLanded.elapsed() > 6000) {
        qWarning("CRT liveness watchdog fired (no readback for %lld ms) — "
                 "force pipeline reset", qint64(m_lastLanded.elapsed()));
        m_pending = QImage(); // 快照作废 → 下一帧全量重拍
        resetPipeline();
        markDirty(true);
        m_lastLanded.invalidate(); // 防同帧重复触发
    }
    if (!isVisible())
        return;
    ensureRhi();
    if (!m_r || !m_ps || !m_psPersist || !m_psDown || !m_psBlurH || !m_psBlurV
        || !m_ubuf || !m_snapTex) {
        return;
    }
    // 回读看门狗：全屏过渡等场景下 Metal 回读可能失联——超时强制复位。
    // 轻恢复优先：只作废在途回读 + 强制下一拍重渲染（迟到回调按代际
    // 丢弃）；整管线重建 = 主线程重活（用户实机"卡一秒"的元凶：800ms
    // 超时 + 全量重建）。连续三次轻恢复仍卡（RHI 真失联——全屏换
    // NSWindow 场景）才整管线重建
    if (m_readbackInFlight && m_readbackClock.isValid()
        && m_readbackClock.elapsed() > 800) {
        ++m_watchdogFires;
        ++m_watchdogStreak;
        m_readbackInFlight = false;
        ++m_readbackGen; // 在途回读作废：迟到回调不覆盖新帧
        m_forceNow = true;
        m_sinceRefresh.invalidate();
        if (m_watchdogStreak >= 3) {
            qWarning("CRT-RHI watchdog x%d — full pipeline rebuild", m_watchdogStreak);
            releaseGpu();
            ensureRhi();
            m_watchdogStreak = 0;
            // 重建失败（后端/纹理创建失败 = 半残状态）→ 本帧放弃，
            // 标脏下一帧重试——旧代码继续跑，拿着空纹理/空批次上传
            // = 段错误（用户报：码一行字 + 方向键落光标闪退）
            if (!m_r || !m_ps || !m_snapTex || !m_ubuf) {
                m_renderDirty = true;
                return;
            }
        }
    } else if (m_readbackInFlight) {
        m_watchdogStreak = 0;
    }
    if (m_readbackInFlight)
        return;
    const CrtConfig cfg = m_source->config(); // 每帧配置值快照
    // 无链节流（用户三轮拍板：按下必须即时反馈——旧版 50ms 起拍间隔
    // 把按键延迟叠到 100ms+，是"负优化"）。突发期 GPU/回读的压力
    // 由逐帧串行 + 半分辨率承担
    // P1 脏驱动：无变化且未到环境拍 → 整链跳过。环境拍 120ms 保底
    // 滚动带/颗粒/余晖的持续推进（余晖在 GPU 按时间衰减，零 CPU 重活）
    const bool dirty = m_renderDirty || m_forceNow;
    const bool ambient = !m_ambientClock.isValid() || m_ambientClock.elapsed() > 120;
    if (!dirty && !ambient)
        return;
    m_renderDirty = false;
    m_ambientClock.restart();
    // 滚动期半分辨率：运动掩蔽下 2×2 下采样不可感知，回读数据量 ÷4、
    // GPU 填充 ÷4；掩膜/扫描线锚定的屏幕栅格随目标减半（滚动中不可见），
    // 停稳后 settle 标全量、回全分辨率重拍自愈
    // 半分辨率只用于滚动（滚动会话低频、运动掩蔽下不可感知）。画刷
    // 会话不再切换（用户三轮报：Shift 笔刷松键黑屏一会——半分辨率
    // 切换 = 每会话边界各一次整管线重建 = 黑屏元凶，负优化回退）
    m_renderScale = cfg.scrolling ? 0.5 : 1.0;
    const QSize want(qMax(1, int(width() * devicePixelRatioF() * m_renderScale)),
                     qMax(1, int(height() * devicePixelRatioF() * m_renderScale)));
    if (want != m_texSize) {
        releaseGpu(); // 帧边界安全重建（此刻无在途回读）
        ensureRhi();
        if (!m_r || !m_ps || !m_snapTex || !m_ubuf) {
            m_renderDirty = true;
            return;
        }
        // 重建后跳过本帧的重拍：重建帧会赶上视口布局未就绪（快照失字
        // = 黑帧），且该黑帧的回读串行压制后续帧 → 用户看到"松 Shift
        // 黑屏一会"。下一帧（布局已就绪）重拍，黑帧永不产生
        m_sinceRefresh.invalidate();
        m_renderDirty = true;
        return;
    }

    QRhiResourceUpdateBatch *u = m_r->nextResourceUpdateBatch();
    if (!u) { // 批次分配失败：本帧放弃，下一帧重试（防御，不崩）
        m_renderDirty = true;
        return;
    }

    // 快照：合成真实组件（80ms 节流；force 立即）。环境拍（无脏）复用
    // 上一拍快照——文本不重拍，GPU 侧余晖照常推进
    // 快照节流只拦环境拍（无脏时复用上一拍）；脏帧（打字/删除/选区）
    // 即时重拍——增量脏区便宜，所见即所得（旧版 80ms 节流把按键
    // 的落字延迟到 ~100ms，用户报"不是所见即所得"）
    const bool throttled = !dirty && m_sinceRefresh.isValid()
                           && m_sinceRefresh.elapsed() < 80;
    if (m_forceNow || !throttled) {
        // 环境拍同样重拍（旧版环境拍跳过重拍 = 管线重建后的第一帧若
        // 赶上视口布局未就绪，快照失字且永远不自愈——用户报"松 Shift
        // 黑屏一会"的残因。环境拍 120ms 一次，全量合成可承受；
        // 正确性优先于这笔 CPU）
        const bool needRepaint = dirty || ambient || m_pending.isNull()
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
            // 黑帧拦截（用户报"松 Shift 黑屏一会"的根修）：快照顶部
            // 失字（视口渲染在重建/布局瞬间为空——Qt 内部时序）时不
            // 上传、标脏下一帧重试——黑帧永不进入余晖历史、永不落地
            {
                long mx = 0;
                for (int y = 0; y < m_pending.height() / 5; ++y)
                    for (int x = 2; x < m_pending.width() - 30; ++x) {
                        const QRgb pxx = m_pending.pixel(x, y);
                        mx = qMax<long>(mx, qRed(pxx) + qGreen(pxx) + qBlue(pxx));
                    }
                if (mx < 60 && m_pending.height() > 100) {
                    // 拦截已知的百毫秒级布局瞬态（"松 Shift 黑屏一会"的
                    // 残因）；连续失字超过 1.5s = 快照真的坏了 → 如实
                    // 上传（用户报：画面冻结在旧帧、痕迹删不掉、换机
                    // 底色不变——拦截死循环是嫌疑人之一）
                    if (!m_darkSince.isValid())
                        m_darkSince.start();
                    if (m_darkSince.elapsed() < 1500) {
                        m_renderDirty = true;
                        return; // 重试下一帧
                    }
                    qWarning("CRT dark snapshot for %lld ms — uploading as-is "
                             "(anti-freeze: 快照长期失字时如实落地，不冻结旧帧)",
                             qint64(m_darkSince.elapsed()));
                } else {
                    m_darkSince.invalidate();
                }
            }
            QImage up = m_pending.convertToFormat(QImage::Format_RGBA8888);
            up.setDevicePixelRatio(1.0); // 上传按原始像素：QRhi 尊重图像 DPR
            if (m_snapTex && !up.isNull())
                u->uploadTexture(m_snapTex, up);
            else { // 纹理半残/图像空：本帧放弃，下一帧重试（防御，不崩）
                m_renderDirty = true;
                return;
            }
        }
        m_forceNow = false;
        m_sinceRefresh.restart();
    }
    // 观察者 = 鼠标（视差每帧更新，不受快照节流）；锁定（M1）= 观察者
    // 正对屏幕中心：视差偏移为零，电子图像不位移、不倾斜（铁律 4：
    // 不毁打字——非零偏移会让整幅内容整体偏移，观感为"弧斜"）
    QPointF view;
    if (cfg.viewLocked) {
        view = QPointF(0.0, 0.0);
    } else {
        view = cfg.lastMouse;
        if (view.x() < 0) {
            view = QPointF(0.0, 0.0);
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
    // 视图移动期（鼠标拖动视差）或视图跳变帧（锁切换/解锁——观察者
    // 位置突变）：历史清零 = 纯快照——旧位置的磷光幽灵当场熄灭，文字
    // 跟随鼠标移动时零拖影（用户报的"动鼠标出倒影"根修；宽窗口大
    // 字号下拖影带可达 6 条，4 倍速衰减压不住）。viewJump 兜住
    // viewMoving 150ms 时钟与渲染节奏的竞态（慢机/ASAN 下锁后第一
    // 帧迟到、时钟已过期 → 旧偏移 hist 与新帧混合成双影带）
    const bool viewJump = qAbs(view.x() - m_lastView.x())
                          + qAbs(view.y() - m_lastView.y()) > 0.005;
    m_lastView = view;
    const bool machineJump = int(cfg.machine) != m_lastMachine;
    m_lastMachine = int(cfg.machine);
    // 跳变/移动后连续 3 帧纯快照：三个历史槽全部被当前帧内容覆盖，
    // 才允许余晖权重恢复——否则槽里滞留的旧位置内容会在恢复瞬间
    // 复活成双影带（DPR2/慢机实测 bands=3）
    if (cfg.viewMoving || viewJump || machineJump)
        m_sinceViewChange = 0;
    else if (m_sinceViewChange < 3)
        ++m_sinceViewChange;
    // 滚动期余晖清零（用户报：⌃⇧⌘T 滚动时滚动条旁灰块伪影——余晖
    // 的 max 模型把中灰把手的旧位置涂抹成残影；旧版只跳辉光不关余晖）
    const bool histPrimed = m_histFrame >= 2 && m_sinceViewChange >= 3
                            && !cfg.scrolling && !cfg.fading;
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


            img.setDevicePixelRatio(devicePixelRatioF()); // 物理像素：1:1 落屏
            m_shown = std::move(img);
            m_maxReadbackMs = qMax(m_maxReadbackMs, int(m_readbackClock.elapsed()));
            m_readbackInFlight = false;
            m_lastLanded.restart(); // 画面活性心跳
            m_diagDumped = false;
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
